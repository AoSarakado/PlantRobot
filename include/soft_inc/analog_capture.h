#ifndef ANALOG_CAPTURE_H
#define ANALOG_CAPTURE_H

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/* Owns ADC1 continuous mode: microphone GPIO2 and light sensor GPIO1. */
esp_err_t analog_capture_init(void);
esp_err_t analog_capture_mic_read(int16_t *samples, size_t count, uint32_t timeout_ms);
esp_err_t analog_capture_light_read(int *raw);
esp_err_t analog_capture_mic_levels(int *minimum, int *maximum);

#endif
