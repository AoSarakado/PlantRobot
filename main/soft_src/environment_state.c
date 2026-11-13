#include "environment_state.h"
#include "aht20.h"
#include "light_module.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static environment_t s_environment;
static SemaphoreHandle_t s_lock;

esp_err_t environment_state_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    return s_lock ? ESP_OK : ESP_ERR_NO_MEM;
}

environment_t environment_snapshot(void)
{
    environment_t copy;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    copy = s_environment;
    xSemaphoreGive(s_lock);
    return copy;
}

void environment_sensor_task(void *arg)
{
    while (1) {
        esp_err_t temp_err = aht20_read();
        esp_err_t light_err = light_read();
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (temp_err == ESP_OK) {
            const aht20_data_t *data = aht20_get_data();
            s_environment.temperature = data->temperature;
            s_environment.humidity = data->humidity;
            s_environment.temperature_valid = true;
        }
        if (light_err == ESP_OK) {
            const light_data_t *data = light_get_data();
            s_environment.brightness = data->brightness;
            s_environment.light_raw = data->raw;
            s_environment.light_valid = true;
        }
        xSemaphoreGive(s_lock);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
