/* GY-MAX4466 ADC microphone: recording, playback, monitor and diagnostics. */
#include "mic_module.h"
#include "analog_capture.h"
#include "audio_module.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MIC_REC_SECONDS 3
#define REC_MAX (MIC_SAMPLE_RATE * MIC_REC_SECONDS)

static int16_t s_rec_buf[REC_MAX];
static volatile uint32_t s_rec_cnt;
static bool s_ready;
static volatile bool s_monitor_on;
static volatile bool s_rec_on;
static volatile bool s_play_on;
static volatile bool s_debug_on;
static volatile int32_t s_last_peak = -1;
static uint32_t s_ok, s_fail;

static bool mic_idle(void) { return !s_rec_on && !s_play_on; }

esp_err_t mic_init(void)
{
    esp_err_t err = analog_capture_init();
    if (err != ESP_OK) return err;
    s_ready = true;
    printf("mic_init: GY-MAX4466 OUT=GPIO%d rate=%d rec=%ds\r\n",
           MIC_ADC_GPIO, MIC_SAMPLE_RATE, MIC_REC_SECONDS);
    return ESP_OK;
}

static int32_t peak_of(const int16_t *samples, size_t count)
{
    int32_t peak = 0;
    for (size_t i = 0; i < count; i++) {
        int32_t value = samples[i] < 0 ? -samples[i] : samples[i];
        if (value > peak) peak = value;
    }
    return peak;
}

static void rec_task(void *arg)
{
    int16_t frame[MIC_FRAME_SAMPLES];
    printf("rec: start (%ds)\r\n", MIC_REC_SECONDS);
    s_rec_cnt = 0;
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS((MIC_REC_SECONDS + 2) * 1000);
    while (s_rec_on && s_rec_cnt < REC_MAX && xTaskGetTickCount() < deadline) {
        if (analog_capture_mic_read(frame, MIC_FRAME_SAMPLES, 500) != ESP_OK) {
            s_fail++;
            continue;
        }
        s_ok++;
        memcpy(&s_rec_buf[s_rec_cnt], frame, sizeof(frame));
        s_rec_cnt += MIC_FRAME_SAMPLES;
    }
    bool full = s_rec_cnt == REC_MAX;
    s_rec_on = false;
    printf("rec: done, %lu samples (%lu ms)%s\r\n",
           (unsigned long)s_rec_cnt,
           (unsigned long)(s_rec_cnt * 1000 / MIC_SAMPLE_RATE),
           full ? " [buffer full]" : " [capture incomplete]");
    vTaskDelete(NULL);
}

static void play_task(void *arg)
{
    uint32_t pos = 0;
    while (pos < s_rec_cnt) {
        uint32_t count = s_rec_cnt - pos;
        if (count > MIC_FRAME_SAMPLES) count = MIC_FRAME_SAMPLES;
        if (audio_play_frame(&s_rec_buf[pos], count) != ESP_OK) break;
        pos += count;
    }
    audio_play_end();
    s_play_on = false;
    printf("play: done\r\n");
    vTaskDelete(NULL);
}

static void monitor_task(void *arg)
{
    int16_t frame[MIC_FRAME_SAMPLES];
    while (s_monitor_on) {
        if (analog_capture_mic_read(frame, MIC_FRAME_SAMPLES, 500) != ESP_OK) {
            s_fail++;
            continue;
        }
        s_ok++;
        s_last_peak = peak_of(frame, MIC_FRAME_SAMPLES);
        audio_play_frame(frame, MIC_FRAME_SAMPLES);
    }
    audio_play_end();
    vTaskDelete(NULL);
}

bool mic_rec_start(void)
{
    if (!s_ready || !mic_idle() || s_monitor_on || s_debug_on) return false;
    s_rec_on = true;
    if (xTaskCreatePinnedToCore(rec_task, "rec", 4096, NULL, 4, NULL, 1) != pdPASS) {
        s_rec_on = false;
        return false;
    }
    return true;
}

bool mic_play_start(void)
{
    if (!s_ready || !mic_idle() || s_monitor_on || !s_rec_cnt) return false;
    s_play_on = true;
    if (xTaskCreatePinnedToCore(play_task, "play", 4096, NULL, 4, NULL, 1) != pdPASS) {
        s_play_on = false;
        return false;
    }
    return true;
}

bool mic_monitor_toggle(void)
{
    if (s_monitor_on) { s_monitor_on = false; return false; }
    if (!s_ready || !mic_idle() || s_debug_on) return false;
    s_monitor_on = true;
    if (xTaskCreatePinnedToCore(monitor_task, "micp", 4096, NULL, 4, NULL, 1) != pdPASS) {
        s_monitor_on = false;
    }
    return s_monitor_on;
}

esp_err_t mic_read_frame(int16_t *samples, size_t count)
{
    if (!samples || count != MIC_FRAME_SAMPLES) return ESP_ERR_INVALID_ARG;
    if (!s_ready || !mic_idle() || s_monitor_on || s_debug_on) return ESP_ERR_INVALID_STATE;
    return analog_capture_mic_read(samples, count, 200);
}

int32_t mic_probe_peak(void)
{
    if (s_monitor_on) return s_last_peak;
    int16_t frame[MIC_FRAME_SAMPLES];
    if (mic_read_frame(frame, MIC_FRAME_SAMPLES) != ESP_OK) return -1;
    s_last_peak = peak_of(frame, MIC_FRAME_SAMPLES);
    return s_last_peak;
}

void mic_debug_raw(void)
{
    if (!s_ready || !mic_idle() || s_monitor_on) {
        printf("micraw: microphone busy or not initialized\r\n");
        return;
    }
    s_debug_on = true;
    int minimum, maximum;
    esp_err_t err = analog_capture_mic_levels(&minimum, &maximum);
    s_debug_on = false;
    if (err != ESP_OK) printf("micraw: ADC read failed 0x%x\r\n", err);
    else printf("micraw: ADC GPIO%d centered min=%d max=%d span=%d\r\n",
                MIC_ADC_GPIO, minimum, maximum, maximum - minimum);
}

bool mic_recording(void) { return s_rec_on; }
bool mic_rec_playing(void) { return s_play_on; }
bool mic_monitor_on(void) { return s_monitor_on; }
uint32_t mic_rec_samples(void) { return s_rec_cnt; }
uint32_t mic_capture_ok(void) { return s_ok; }
uint32_t mic_capture_fail(void) { return s_fail; }
int32_t mic_last_peak(void) { return s_last_peak; }

size_t mic_rec_copy(int16_t *destination, size_t capacity)
{
    if (!destination || s_rec_on || s_play_on || capacity < s_rec_cnt) return 0;
    size_t count = s_rec_cnt;
    memcpy(destination, s_rec_buf, count * sizeof(int16_t));
    return count;
}
