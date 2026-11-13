#include "analog_capture.h"
#include "esp_adc/adc_continuous.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <string.h>

#define MIC_CHANNEL ADC_CHANNEL_1
#define LIGHT_CHANNEL ADC_CHANNEL_0
#define ADC_TOTAL_RATE 32000
#define ADC_BATCH_BYTES 1024

static adc_continuous_handle_t s_adc;
static SemaphoreHandle_t s_lock;
static int16_t s_pending[ADC_BATCH_BYTES / 8];
static size_t s_pending_pos;
static size_t s_pending_count;
static int s_light_raw;
static bool s_light_valid;
static int32_t s_bias_q8 = 2048 << 8;
/* read_batch() is called only while s_lock is held. Keep DMA parsing buffers
 * outside the caller's FreeRTOS stack (rec_task has a 4 KB stack). */
static uint8_t s_raw_batch[ADC_BATCH_BYTES];
static adc_continuous_data_t s_parsed_batch[ADC_BATCH_BYTES / SOC_ADC_DIGI_RESULT_BYTES];

esp_err_t analog_capture_init(void)
{
    if (s_adc) return ESP_OK;
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    adc_continuous_handle_cfg_t handle_cfg = {
        .max_store_buf_size = 8192,
        .conv_frame_size = ADC_BATCH_BYTES,
        .flags.flush_pool = true,
    };
    esp_err_t err = adc_continuous_new_handle(&handle_cfg, &s_adc);
    if (err != ESP_OK) return err;
    adc_digi_pattern_config_t patterns[2] = {
        {.atten = ADC_ATTEN_DB_12, .channel = MIC_CHANNEL, .unit = ADC_UNIT_1,
         .bit_width = ADC_BITWIDTH_12},
        {.atten = ADC_ATTEN_DB_12, .channel = LIGHT_CHANNEL, .unit = ADC_UNIT_1,
         .bit_width = ADC_BITWIDTH_12},
    };
    adc_continuous_config_t config = {
        .sample_freq_hz = ADC_TOTAL_RATE,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2,
        .pattern_num = 2,
        .adc_pattern = patterns,
    };
    err = adc_continuous_config(s_adc, &config);
    if (err == ESP_OK) err = adc_continuous_start(s_adc);
    if (err != ESP_OK) {
        adc_continuous_deinit(s_adc);
        s_adc = NULL;
        return err;
    }
    return ESP_OK;
}

static esp_err_t read_batch(uint32_t timeout_ms)
{
    uint32_t bytes = 0, count = 0;
    esp_err_t err = adc_continuous_read(s_adc, s_raw_batch, sizeof(s_raw_batch), &bytes, timeout_ms);
    if (err != ESP_OK) return err;
    err = adc_continuous_parse_data(s_adc, s_raw_batch, bytes, s_parsed_batch, &count);
    if (err != ESP_OK) return err;
    uint32_t light_sum = 0, light_count = 0;
    s_pending_count = 0;
    s_pending_pos = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (!s_parsed_batch[i].valid || s_parsed_batch[i].unit != ADC_UNIT_1) continue;
        if (s_parsed_batch[i].channel == LIGHT_CHANNEL) {
            light_sum += s_parsed_batch[i].raw_data;
            light_count++;
        } else if (s_parsed_batch[i].channel == MIC_CHANNEL &&
                   s_pending_count < sizeof(s_pending) / sizeof(s_pending[0])) {
            int32_t raw_mic = s_parsed_batch[i].raw_data;
            s_bias_q8 += ((raw_mic << 8) - s_bias_q8) >> 8;
            int sample = raw_mic - (s_bias_q8 >> 8);
            s_pending[s_pending_count++] = (int16_t)(sample * 16);
        }
    }
    if (light_count) {
        s_light_raw = light_sum / light_count;
        s_light_valid = true;
    }
    return s_pending_count ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t analog_capture_mic_read(int16_t *samples, size_t count, uint32_t timeout_ms)
{
    if (!s_adc || !samples || !count) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(timeout_ms + 50)) != pdTRUE)
        return ESP_ERR_TIMEOUT;
    size_t filled = 0;
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    esp_err_t err = ESP_OK;
    while (filled < count) {
        if (s_pending_pos == s_pending_count) {
            uint32_t remaining = (deadline - xTaskGetTickCount()) * portTICK_PERIOD_MS;
            if ((int32_t)(deadline - xTaskGetTickCount()) <= 0) {
                err = ESP_ERR_TIMEOUT;
                break;
            }
            err = read_batch(remaining);
            if (err != ESP_OK) break;
        }
        size_t available = s_pending_count - s_pending_pos;
        size_t take = count - filled < available ? count - filled : available;
        memcpy(samples + filled, s_pending + s_pending_pos, take * sizeof(int16_t));
        filled += take;
        s_pending_pos += take;
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t analog_capture_light_read(int *raw)
{
    if (!raw || !s_adc) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(250)) != pdTRUE) return ESP_ERR_TIMEOUT;
    if (!s_light_valid) {
        esp_err_t err = read_batch(200);
        if (err != ESP_OK) {
            xSemaphoreGive(s_lock);
            return err;
        }
    }
    *raw = s_light_raw;
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t analog_capture_mic_levels(int *minimum, int *maximum)
{
    if (!minimum || !maximum) return ESP_ERR_INVALID_ARG;
    int16_t samples[480];
    esp_err_t err = analog_capture_mic_read(samples, 480, 500);
    if (err != ESP_OK) return err;
    int low = 32767, high = -32768;
    for (size_t i = 0; i < 480; i++) {
        if (samples[i] < low) low = samples[i];
        if (samples[i] > high) high = samples[i];
    }
    *minimum = low;
    *maximum = high;
    return ESP_OK;
}
