#include "shtc3.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "SHTC3";

#define SHTC3_ADDR 0x70

static i2c_master_dev_handle_t s_dev = NULL;

static uint8_t crc8(const uint8_t *data, int len)
{
    uint8_t crc = 0xFF;
    for (int i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

esp_err_t Shtc3_Init(i2c_master_bus_handle_t bus)
{
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SHTC3_ADDR,
        .scl_speed_hz = 400000,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &dev_config, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SHTC3 to bus: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t Shtc3_Read(float *temperature_c, float *humidity_percent)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;

    uint8_t wake[2] = { 0x35, 0x17 };
    esp_err_t err = i2c_master_transmit(s_dev, wake, sizeof(wake), 1000);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(1));

    uint8_t meas[2] = { 0x7C, 0xA2 };
    err = i2c_master_transmit(s_dev, meas, sizeof(meas), 1000);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(15));

    uint8_t data[6];
    err = i2c_master_receive(s_dev, data, sizeof(data), 1000);
    if (err != ESP_OK) return err;

    uint8_t sleep_cmd[2] = { 0xB0, 0x98 };
    i2c_master_transmit(s_dev, sleep_cmd, sizeof(sleep_cmd), 1000);

    if (crc8(&data[0], 2) != data[2] || crc8(&data[3], 2) != data[5]) {
        ESP_LOGW(TAG, "CRC error reading SHTC3");
        return ESP_ERR_INVALID_CRC;
    }

    uint16_t raw_t  = (uint16_t)((data[0] << 8) | data[1]);
    uint16_t raw_rh = (uint16_t)((data[3] << 8) | data[4]);

    *temperature_c    = -45.0f + 175.0f * ((float)raw_t / 65536.0f);
    *humidity_percent = 100.0f * ((float)raw_rh / 65536.0f);

    return ESP_OK;
}