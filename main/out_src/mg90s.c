/**
 * @file mg90s.c  MG90S 舵机（LEDC 50Hz，0.5~2.5ms 脉宽）
 */
#include "mg90s.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SERVO_FREQ_HZ     50
#define SERVO_RESOLUTION  LEDC_TIMER_14_BIT
#define SERVO_TIMER       LEDC_TIMER_0
#define SERVO_CHANNEL     LEDC_CHANNEL_0
#define SERVO_SPEED_MODE  LEDC_LOW_SPEED_MODE
#define PULSE_MIN_US      500
#define PULSE_MAX_US      2500
#define PERIOD_US         20000
#define FRAME_MS          20

static int s_current_angle = 90;

static uint32_t angle_to_duty(int angle)
{
    uint32_t pulse_us = PULSE_MIN_US +
        (uint32_t)angle * (PULSE_MAX_US - PULSE_MIN_US) / (SERVO_MAX_ANGLE - SERVO_MIN_ANGLE);
    return (uint32_t)((uint64_t)pulse_us * (1 << SERVO_RESOLUTION) / PERIOD_US);
}

static void servo_hw_write(int angle)
{
    ledc_set_duty(SERVO_SPEED_MODE, SERVO_CHANNEL, angle_to_duty(angle));
    ledc_update_duty(SERVO_SPEED_MODE, SERVO_CHANNEL);
}

esp_err_t servo_init(void)
{
    ledc_timer_config_t timer_conf = {
        .speed_mode      = SERVO_SPEED_MODE,
        .timer_num       = SERVO_TIMER,
        .duty_resolution = SERVO_RESOLUTION,
        .freq_hz         = SERVO_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    if (ledc_timer_config(&timer_conf) != ESP_OK) return ESP_FAIL;

    ledc_channel_config_t ch_conf = {
        .speed_mode = SERVO_SPEED_MODE,
        .channel    = SERVO_CHANNEL,
        .timer_sel  = SERVO_TIMER,
        .intr_type  = LEDC_INTR_DISABLE,
        .gpio_num   = SERVO_GPIO,
        .duty       = angle_to_duty(90),
        .hpoint     = 0,
    };
    if (ledc_channel_config(&ch_conf) != ESP_OK) return ESP_FAIL;

    s_current_angle = 90;
    return ESP_OK;
}

esp_err_t servo_set_angle(int target_angle, int delay_ms)
{
    if (target_angle < SERVO_MIN_ANGLE) target_angle = SERVO_MIN_ANGLE;
    if (target_angle > SERVO_MAX_ANGLE) target_angle = SERVO_MAX_ANGLE;

    int delta = target_angle - s_current_angle;
    if (delta == 0) return ESP_OK;

    if (delay_ms < FRAME_MS) {
        servo_hw_write(target_angle);
        s_current_angle = target_angle;
        return ESP_OK;
    }

    int frames = delay_ms / FRAME_MS;
    if (frames < 1) frames = 1;
    float step = (float)delta / frames;
    float pos  = (float)s_current_angle;

    for (int i = 0; i < frames; i++) {
        pos += step;
        int ang = (int)(pos + 0.5f);
        if (i == frames - 1) ang = target_angle;
        servo_hw_write(ang);
        vTaskDelay(pdMS_TO_TICKS(FRAME_MS));
    }
    s_current_angle = target_angle;
    return ESP_OK;
}

int servo_get_angle(void) { return s_current_angle; }
