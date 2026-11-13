#include "asr_client.h"
#include "mic_module.h"
#include "sdkconfig.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CONNECTED_BIT BIT0
#define RECORD_SAMPLES (MIC_SAMPLE_RATE * 3)

static EventGroupHandle_t s_wifi_events;
static bool s_wifi_started;
static volatile bool s_busy;

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event = data;
        printf("stt: Wi-Fi disconnected, reason=%u\r\n", (unsigned)event->reason);
        xEventGroupClearBits(s_wifi_events, CONNECTED_BIT);
    }
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
        xEventGroupSetBits(s_wifi_events, CONNECTED_BIT);
}

static esp_err_t wifi_connect(void)
{
    if (!CONFIG_PLANT_WIFI_SSID[0]) return ESP_ERR_INVALID_STATE;
    if (!s_wifi_started) {
        ESP_ERROR_CHECK(nvs_flash_init());
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        s_wifi_events = xEventGroupCreate();
        if (!s_wifi_events) return ESP_ERR_NO_MEM;
        if (!esp_netif_create_default_wifi_sta()) return ESP_ERR_NO_MEM;
        wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&init));
        ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, wifi_event, NULL));
        ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL));
        wifi_config_t config = {0};
        strlcpy((char *)config.sta.ssid, CONFIG_PLANT_WIFI_SSID, sizeof(config.sta.ssid));
        strlcpy((char *)config.sta.password, CONFIG_PLANT_WIFI_PASSWORD, sizeof(config.sta.password));
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
        ESP_ERROR_CHECK(esp_wifi_start());
        s_wifi_started = true;
    }
    if (!(xEventGroupGetBits(s_wifi_events) & CONNECTED_BIT))
        ESP_ERROR_CHECK(esp_wifi_connect());
    EventBits_t bits = xEventGroupWaitBits(s_wifi_events, CONNECTED_BIT, pdFALSE,
                                           pdTRUE, pdMS_TO_TICKS(15000));
    return bits & CONNECTED_BIT ? ESP_OK : ESP_ERR_TIMEOUT;
}

typedef struct {
    char text[512];
    size_t length;
    bool overflow;
} response_t;

static esp_err_t http_event(esp_http_client_event_t *event)
{
    if (event->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
    response_t *response = event->user_data;
    if (response->length + event->data_len >= sizeof(response->text)) {
        response->overflow = true;
        return ESP_FAIL;
    }
    memcpy(response->text + response->length, event->data, event->data_len);
    response->length += event->data_len;
    response->text[response->length] = '\0';
    return ESP_OK;
}

static esp_err_t transcribe(const int16_t *pcm, size_t count, response_t *response)
{
    if (!CONFIG_PLANT_ASR_URL[0]) return ESP_ERR_INVALID_STATE;
    esp_http_client_config_t config = {
        .url = CONFIG_PLANT_ASR_URL,
        .event_handler = http_event,
        .user_data = response,
        .timeout_ms = 60000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_ERR_NO_MEM;
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/octet-stream");
    esp_http_client_set_post_field(client, (const char *)pcm, count * sizeof(int16_t));
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK) return err;
    if (response->overflow || status != 200) return ESP_FAIL;
    return ESP_OK;
}

static void asr_task(void *arg)
{
    esp_err_t err = ESP_OK;
    int16_t *audio = NULL;
    if (!mic_rec_start()) {
        err = ESP_ERR_INVALID_STATE;
        goto done;
    }
    while (mic_recording()) vTaskDelay(pdMS_TO_TICKS(50));
    size_t count = mic_rec_samples();
    if (count != RECORD_SAMPLES) {
        err = ESP_ERR_INVALID_SIZE;
        goto done;
    }
    audio = heap_caps_malloc(count * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!audio) {
        err = ESP_ERR_NO_MEM;
        goto done;
    }
    if (mic_rec_copy(audio, count) != count) {
        err = ESP_ERR_INVALID_STATE;
        goto done;
    }
    err = wifi_connect();
    if (err != ESP_OK) goto done;
    response_t response = {0};
    err = transcribe(audio, count, &response);
    if (err == ESP_OK) printf("stt: %s\r\n", response.text[0] ? response.text : "[empty]");
done:
    if (err != ESP_OK) printf("stt: failed 0x%x\r\n", err);
    free(audio);
    s_busy = false;
    vTaskDelete(NULL);
}

esp_err_t asr_client_start(void)
{
    if (s_busy || mic_recording() || mic_rec_playing() || mic_monitor_on())
        return ESP_ERR_INVALID_STATE;
    if (!CONFIG_PLANT_ASR_URL[0] || !CONFIG_PLANT_WIFI_SSID[0])
        return ESP_ERR_INVALID_STATE;
    s_busy = true;
    if (xTaskCreatePinnedToCore(asr_task, "asr", 8192, NULL, 4, NULL, 0) != pdPASS) {
        s_busy = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool asr_client_busy(void) { return s_busy; }
