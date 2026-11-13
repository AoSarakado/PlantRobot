#ifndef MG90S_H
#define MG90S_H

#include "esp_err.h"

#define SERVO_GPIO        15
#define SERVO_MIN_ANGLE   0
#define SERVO_MAX_ANGLE   180

esp_err_t servo_init(void);
esp_err_t servo_set_angle(int target_angle, int delay_ms);
int servo_get_angle(void);

#endif
