/**
 * @file audio_module.c  MAX98357A I2S0 功放（哑巴功放，送什么播什么）
 */
#include "audio_module.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <math.h>
#include "freertos/semphr.h"

static SemaphoreHandle_t s_tx_mux = NULL;
static i2s_chan_handle_t s_tx;

static void flush_silence(void)
{
    static int16_t silence[1600 * 2];   // 100ms @16kHz > DMA 总量
    memset(silence, 0, sizeof(silence));
    size_t written;
    i2s_channel_write(s_tx, silence, sizeof(silence), &written, portMAX_DELAY);
}

esp_err_t audio_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 6;
    chan_cfg.dma_frame_num = 240;
    chan_cfg.auto_clear = true;
    if (i2s_new_channel(&chan_cfg, &s_tx, NULL) != ESP_OK) return ESP_FAIL;

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = AUDIO_BCLK_GPIO,
            .ws   = AUDIO_LRC_GPIO,
            .dout = AUDIO_DIN_GPIO,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = { 0 },
        },
    };
    if (i2s_channel_init_std_mode(s_tx, &std_cfg) != ESP_OK) return ESP_FAIL;
    if (i2s_channel_enable(s_tx) != ESP_OK) return ESP_FAIL;
     s_tx_mux = xSemaphoreCreateMutex();
    return ESP_OK;
}

esp_err_t audio_play_frame(const int16_t *mono_samples, size_t sample_cnt)
{
    if (!mono_samples || sample_cnt == 0) return ESP_ERR_INVALID_ARG;
    if (!s_tx_mux) return ESP_FAIL;

    if (xSemaphoreTake(s_tx_mux, pdMS_TO_TICKS(500)) != pdTRUE) {
        printf("audio: tx busy\r\n");
        return ESP_FAIL;
    }

    static int16_t stereo_buf[512 * 2];
    size_t done = 0, written;
    esp_err_t ret = ESP_OK;
    while (done < sample_cnt) {
        size_t n = sample_cnt - done;
        if (n > 512) n = 512;
        for (size_t i = 0; i < n; i++) {
            stereo_buf[i * 2]     = mono_samples[done + i];
            stereo_buf[i * 2 + 1] = mono_samples[done + i];
        }
        esp_err_t err = i2s_channel_write(s_tx, stereo_buf, n * 2 * sizeof(int16_t),
                                          &written, portMAX_DELAY);
        if (err != ESP_OK) {
            printf("audio: write ERR 0x%x at %zu/%zu\r\n", err, done, sample_cnt);
            ret = ESP_FAIL;
            break;
        }
        done += n;
    }
    xSemaphoreGive(s_tx_mux);
    return ret;
}


esp_err_t audio_play_end(void)
{
    flush_silence();        // 把 DMA 尾巴顶出去，之后总线保持送静音
    return ESP_OK;          // 不再 disable
}

esp_err_t audio_deinit(void)
{
    if (s_tx) {
        i2s_channel_disable(s_tx);
        i2s_del_channel(s_tx);
        s_tx = NULL;
    }
    return ESP_OK;
}

esp_err_t audio_play_pcm(const int16_t *samples, size_t n_samples)
{
    if (!samples || n_samples == 0) return ESP_ERR_INVALID_ARG;
    esp_err_t err = audio_play_frame(samples, n_samples);
    if (err != ESP_OK) return err;
    return audio_play_end();
}

/* 440Hz 测试音 2 秒 */
void play_test_tone(void)
{
    static int16_t chunk[1600];                 // 100ms @16kHz
    const int total = AUDIO_SAMPLE_RATE * 2;
    const int fade  = AUDIO_SAMPLE_RATE / 50;   // 首尾 20ms 渐变
    const float step = 2.0f * 3.14159f * 440.0f / AUDIO_SAMPLE_RATE;
    float phase = 0.0f;

    for (int done = 0; done < total; done += 1600) {
        int n = total - done;
        if (n > 1600) n = 1600;
        for (int i = 0; i < n; i++) {
            int idx = done + i;
            float env = 1.0f;
            if (idx < fade)         env = (float)idx / fade;
            if (idx > total - fade) env = (float)(total - idx) / fade;
            chunk[i] = (int16_t)(8000 * env * sinf(phase));
            phase += step;
            if (phase >= 2.0f * 3.14159f) phase -= 2.0f * 3.14159f;
        }
        audio_play_frame(chunk, n);
    }
    audio_play_end();
}
