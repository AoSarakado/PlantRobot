/**
 * @file tts_module.c  esp-tts 封装
 *
 * 三件事：
 *   1. 从自定义分区取出声音集，mmap 后初始化 esp-tts
 *   2. 文本入队（strdup 拷贝，调用者不用管生命周期）
 *   3. 内部任务流式合成，每段 PCM 交给 sink 回调
 *
 * 不碰 I2S、不碰 UART、不碰任何业务的音频层。
 */
#include "tts_module.h"

#include <string.h>
#include <stdlib.h>


#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_partition.h"

#include "esp_tts.h"
#include "esp_tts_voice_template.h"

#define TTS_MAX_TEXT      192       /* UTF-8 字节数，中文 3 字节/字 → 约 60 字 */
#define TTS_QUEUE_LEN     4
#define TTS_TASK_STACK    8192
#define TTS_TASK_PRIO     4
#define TTS_TASK_CORE     1         /* 放 WORK 核，别跟 WiFi/主任务抢 */


static const char *TAG = "tts_module";

typedef struct {
    char   *text;      /* heap 拷贝 */
    uint8_t speed;
} tts_job_t;

static esp_tts_handle_t s_tts      = NULL;
static tts_sink_t      s_sink      = NULL;
static tts_end_t       s_on_end    = NULL;
static QueueHandle_t   s_q         = NULL;
static volatile bool   s_speaking  = false;
static volatile uint32_t s_done    = 0;
static volatile uint32_t s_drop    = 0;

/* ==================== 合成任务 ==================== */

static void tts_task(void *arg)
{
    tts_job_t job;

    while (1) {
        if (xQueueReceive(s_q, &job, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        s_speaking = true;
        int len = 0;
        uint32_t samples = 0;
        TickType_t t0 = xTaskGetTickCount();   /* goto done 会跳过声明，所以提到 parse 之前 */

        if (!esp_tts_parse_chinese(s_tts, job.text)) {
            ESP_LOGW(TAG, "解析失败（只吃中文/数字）: %s", job.text);
            goto done;
        }

        do {
            int16_t *pcm = (int16_t *)esp_tts_stream_play(s_tts, &len, job.speed);
            if (pcm == NULL || len <= 0) {
                break;
            }
            esp_err_t err = s_sink(pcm, (size_t)len);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "sink 返回 0x%x，本段中止", err);
                break;
            }
            samples += (uint32_t)len;
        } while (len > 0);

done:
        esp_tts_stream_reset(s_tts);
        uint32_t ms = (uint32_t)((xTaskGetTickCount() - t0) * portTICK_PERIOD_MS);
        ESP_LOGI(TAG, "念完 %u 采样 / %u ms（%u 字节）",
                 (unsigned)samples, (unsigned)ms, (unsigned)(samples * 2));
        free(job.text);
        s_done++;
        s_speaking = false;

        if (s_on_end) {
            (void)s_on_end();
        }
    }
}

/* ==================== 初始化 ==================== */

esp_err_t tts_module_init(const char *partition_label, tts_sink_t sink, tts_end_t on_end)
{
    if (sink == NULL || partition_label == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_tts != NULL) {
        return ESP_OK;   /* 重复调用当幂等 */
    }

    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, partition_label);
    if (part == NULL) {
        ESP_LOGE(TAG, "找不到分区 %s，检查 partitions.csv 和烧写", partition_label);
        return ESP_ERR_NOT_FOUND;
    }

    const void *voicedata = NULL;
    esp_partition_mmap_handle_t mmap_handle;
    esp_err_t err = esp_partition_mmap(part, 0, part->size,
                                       ESP_PARTITION_MMAP_DATA,
                                       &voicedata, &mmap_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mmap 失败 0x%x", err);
        return err;
    }

    esp_tts_voice_t *voice = esp_tts_voice_set_init(&esp_tts_voice_template,
                                                    (void *)voicedata);
    if (voice == NULL) {
        ESP_LOGE(TAG, "声音集初始化失败：分区里的 .dat 和库不匹配？");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "分区 %s @ 0x%lx  %lu KB  →  声音集 %s, %d Hz, %d bit",
             partition_label,
             (unsigned long)part->address,
             (unsigned long)(part->size / 1024),
             voice->voice_name, voice->sample_rate, voice->bit_width);

    s_tts = esp_tts_create(voice);
    if (s_tts == NULL) {
        ESP_LOGE(TAG, "esp_tts_create 失败");
        return ESP_FAIL;
    }

    s_q = xQueueCreate(TTS_QUEUE_LEN, sizeof(tts_job_t));
    if (s_q == NULL) {
        ESP_LOGE(TAG, "队列创建失败");
        return ESP_FAIL;
    }

    s_sink   = sink;
    s_on_end = on_end;

    if (xTaskCreatePinnedToCore(tts_task, "tts", TTS_TASK_STACK, NULL,
                                TTS_TASK_PRIO, NULL, TTS_TASK_CORE) != pdPASS) {
        ESP_LOGE(TAG, "任务创建失败");
        return ESP_FAIL;
    }
    return ESP_OK;
}

/* ==================== 对外接口 ==================== */

bool tts_say(const char *utf8_text, uint8_t speed)
{
    if (s_tts == NULL || s_q == NULL || utf8_text == NULL || *utf8_text == '\0') {
        return false;
    }

    size_t n = strlen(utf8_text);
    if (n >= TTS_MAX_TEXT) {
        s_drop++;
        ESP_LOGW(TAG, "文本太长 %u 字节（上限 %d）", (unsigned)n, TTS_MAX_TEXT - 1);
        return false;
    }

    char *copy = (char *)malloc(n + 1);
    if (copy == NULL) {
        s_drop++;
        return false;
    }
    memcpy(copy, utf8_text, n + 1);

    tts_job_t job = { .text = copy, .speed = (speed > 5) ? 5 : speed };
    if (xQueueSend(s_q, &job, 0) != pdTRUE) {
        free(copy);
        s_drop++;
        return false;       /* 队列满：调用者自己决定重试还是丢弃 */
    }
    return true;
}

bool tts_busy(void)
{
    return s_speaking || (s_q != NULL && uxQueueMessagesWaiting(s_q) > 0);
}

uint32_t tts_done_count(void) { return s_done; }
uint32_t tts_drop_count(void) { return s_drop; }

uint32_t tts_pending(void)
{
    return (s_q != NULL) ? (uint32_t)uxQueueMessagesWaiting(s_q) : 0;
}
