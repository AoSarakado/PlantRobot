/**
 * @file light_module.c  光敏 AO → ADC1 (GPIO1)，raw 越小越亮
 */
#include "light_module.h"
#include "analog_capture.h"
#include <stdio.h>

static light_data_t light_data = { 0 };
esp_err_t light_init(void)
{
    return analog_capture_init();
}

esp_err_t light_read(void)
{
    int raw = 0;
    esp_err_t err = analog_capture_light_read(&raw);
    if (err != ESP_OK) return err;
    light_data.raw = raw;
    light_data.brightness = (4095 - raw) * 100 / 4095;
    return ESP_OK;
}

const light_data_t *light_get_data(void) { return &light_data; }
