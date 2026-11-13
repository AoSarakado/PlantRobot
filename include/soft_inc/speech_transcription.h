#ifndef SPEECH_TRANSCRIPTION_H
#define SPEECH_TRANSCRIPTION_H

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/* 输入为 16 kHz、16-bit、单声道 PCM；输出为 UTF-8 文本。 */
typedef struct {
    esp_err_t (*transcribe)(const int16_t *pcm, size_t sample_count,
                            char *text, size_t text_capacity);
} speech_transcription_provider_t;

#endif
