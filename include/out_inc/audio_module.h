#ifndef AUDIO_MODULE_H
#define AUDIO_MODULE_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#define AUDIO_SAMPLE_RATE  16000
#define AUDIO_BCLK_GPIO    14
#define AUDIO_LRC_GPIO     21
#define AUDIO_DIN_GPIO     45

esp_err_t audio_init(void);
esp_err_t audio_play_frame(const int16_t *mono_samples, size_t sample_cnt);
esp_err_t audio_play_pcm(const int16_t *samples, size_t n_samples);
esp_err_t audio_play_end(void);
esp_err_t audio_deinit(void);
void play_test_tone(void);

#endif
