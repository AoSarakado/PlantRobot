#include "voice_interaction.h"
#include "environment_state.h"
#include "mic_module.h"
#include "tts_module.h"
#include "asr_client.h"
#include "esp_vad.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

void voice_interaction_task(void *arg)
{
    vad_handle_t vad = vad_create_with_param(VAD_MODE_3, MIC_SAMPLE_RATE, 30, 300, 600);
    configASSERT(vad != NULL);
    int16_t samples[MIC_FRAME_SAMPLES];
    bool heard_speech = false;
    while (1) {
        if (asr_client_busy() || tts_busy() || tts_pending() || mic_monitor_on() ||
            mic_recording() || mic_rec_playing()) {
            heard_speech = false;
            vad_reset_trigger(vad);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (mic_read_frame(samples, MIC_FRAME_SAMPLES) != ESP_OK) continue;
        vad_state_t state = vad_process_with_trigger(vad, samples);
        if (state == VAD_SPEECH) {
            heard_speech = true;
        } else if (heard_speech) {
            heard_speech = false;
            environment_t env = environment_snapshot();
            if (!env.temperature_valid || !env.light_valid) continue;
            float temp = env.temperature;
            float abs_temp = temp < 0 ? -temp : temp;
            char reply[160];
            snprintf(reply, sizeof(reply), "现在光照百分之%d，温度%s%d点%d度，湿度百分之%d。",
                     env.brightness, temp < 0 ? "零下" : "", (int)abs_temp,
                     (int)(abs_temp * 10) % 10, (int)env.humidity);
            if (!tts_say(reply, 3)) printf("voice: TTS queue failed\r\n");
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}
