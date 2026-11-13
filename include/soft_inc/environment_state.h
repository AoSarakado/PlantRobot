#ifndef ENVIRONMENT_STATE_H
#define ENVIRONMENT_STATE_H

#include <stdbool.h>
#include "esp_err.h"

typedef struct {
    float temperature;
    float humidity;
    int brightness;
    int light_raw;
    bool temperature_valid;
    bool light_valid;
} environment_t;

esp_err_t environment_state_init(void);
environment_t environment_snapshot(void);
void environment_sensor_task(void *arg);

#endif
