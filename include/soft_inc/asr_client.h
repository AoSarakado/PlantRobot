#ifndef ASR_CLIENT_H
#define ASR_CLIENT_H

#include "esp_err.h"
#include <stdbool.h>

/* Starts a three-second capture and sends raw 16 kHz mono PCM to the PC. */
esp_err_t asr_client_start(void);
bool asr_client_busy(void);

#endif
