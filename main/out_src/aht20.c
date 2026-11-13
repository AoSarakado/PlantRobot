/**
 * @file aht20.c  AHT20 温湿度（软复位 → 校准检查 → 触发测量）
 */
#include "aht20.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#define I2C_TIMEOUT_MS  200
#define CMD_CALIBRATE   0xBE
#define CMD_MEASURE     0xAC
#define CMD_SOFT_RESET  0xBA
#define STATUS_CALIB    0x08
#define STATUS_BUSY     0x80

static aht20_data_t aht20_data = { 0 };
static i2c_master_dev_handle_t s_dev = NULL;

static uint8_t crc8(const uint8_t *d, uint32_t len)
{
    uint8_t crc = 0xFF;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= d[i];
        for (uint8_t b = 0; b < 8; b++)
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
    }
    return crc;
}

esp_err_t aht20_read(void)
{
    if (s_dev == NULL) return ESP_ERR_INVALID_STATE;   // init 失败保护

    const uint8_t cmd[3] = { CMD_MEASURE, 0x33, 0x00 };
    if (i2c_master_transmit(s_dev, cmd, 3, I2C_TIMEOUT_MS) != ESP_OK)
        return ESP_FAIL;

    uint8_t status = 0;
    for (int waited = 0; waited < 300; waited += 10) {
        vTaskDelay(pdMS_TO_TICKS(10));
        if (i2c_master_receive(s_dev, &status, 1, I2C_TIMEOUT_MS) != ESP_OK)
            return ESP_FAIL;
        if (!(status & STATUS_BUSY)) break;
    }
    if (status & STATUS_BUSY) return ESP_ERR_TIMEOUT;

    uint8_t raw[7] = {0};
    if (i2c_master_receive(s_dev, raw, 7, I2C_TIMEOUT_MS) != ESP_OK)
        return ESP_FAIL;
    if (crc8(raw, 6) != raw[6]) return ESP_ERR_INVALID_CRC;

    uint32_t h = ((uint32_t)raw[1] << 12) | ((uint32_t)raw[2] << 4) | (raw[3] >> 4);
    uint32_t t = (((uint32_t)raw[3] & 0x0F) << 16) | ((uint32_t)raw[4] << 8) | raw[5];
    aht20_data.humidity    = (float)h / 1048576.0f * 100.0f;
    aht20_data.temperature = (float)t / 1048576.0f * 200.0f - 50.0f;
    return ESP_OK;
}

const aht20_data_t *aht20_get_data(void) { return &aht20_data; }

esp_err_t aht20_init(i2c_master_bus_handle_t bus)
{
    vTaskDelay(pdMS_TO_TICKS(100));    // 上电稳定

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = AHT20_I2C_ADDR,
        .scl_speed_hz    = AHT20_I2C_FREQ_HZ,
    };
    if (i2c_master_bus_add_device(bus, &dev_cfg, &s_dev) != ESP_OK)
        return ESP_FAIL;

    // 软复位：无 ACK 说明接线/供电有问题
    const uint8_t rst = CMD_SOFT_RESET;
    if (i2c_master_transmit(s_dev, &rst, 1, I2C_TIMEOUT_MS) != ESP_OK) {
        s_dev = NULL;
        return ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(20));

    uint8_t status = 0;
    if (i2c_master_receive(s_dev, &status, 1, I2C_TIMEOUT_MS) != ESP_OK)
        return ESP_FAIL;
    if (!(status & STATUS_CALIB)) {
        const uint8_t cal[3] = { CMD_CALIBRATE, 0x08, 0x00 };
        i2c_master_transmit(s_dev, cal, 3, I2C_TIMEOUT_MS);
        vTaskDelay(pdMS_TO_TICKS(40));
        if (i2c_master_receive(s_dev, &status, 1, I2C_TIMEOUT_MS) != ESP_OK)
            return ESP_FAIL;
        if (!(status & STATUS_CALIB)) return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}
