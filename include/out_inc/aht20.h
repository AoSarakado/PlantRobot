#ifndef AHT20_H
#define AHT20_H

#include "driver/i2c_master.h"

#define AHT20_I2C_ADDR     0x38
#define AHT20_I2C_FREQ_HZ  100000

typedef struct {
    float temperature;   // °C
    float humidity;      // %RH
} aht20_data_t;

esp_err_t aht20_init(i2c_master_bus_handle_t bus);
esp_err_t aht20_read(void);
const aht20_data_t *aht20_get_data(void);

#endif
