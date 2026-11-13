#ifndef LIGHT_MODULE_H
#define LIGHT_MODULE_H

#include <stdint.h>
#include "esp_err.h"
#include "hal/adc_types.h"

#define LIGHT_ADC_GPIO     1        /* AO 接 GPIO1 */
#define LIGHT_ADC_CHANNEL  ADC_CHANNEL_0

typedef struct {
    int raw;          /* 0~4095，越小越亮 */
    int brightness;   /* 0~100% */
} light_data_t;

esp_err_t light_init(void);
esp_err_t light_read(void);
const light_data_t *light_get_data(void);

#endif
