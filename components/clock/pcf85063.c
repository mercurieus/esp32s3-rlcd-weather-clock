#include "pcf85063.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "PCF85063";

#define PCF85063_ADDR       0x51
#define REG_CTRL1           0x00
#define REG_SECONDS         0x04

static i2c_master_bus_handle_t s_bus = NULL;
static i2c_master_dev_handle_t s_dev = NULL;

static inline uint8_t bin2bcd(int val) { return (uint8_t)(((val / 10) << 4) | (val % 10)); }
static inline int bcd2bin(uint8_t val) { return (int)((val >> 4) * 10 + (val & 0x0F)); }

esp_err_t Pcf85063_Init(gpio_num_t sda_pin, gpio_num_t scl_pin)
{
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = sda_pin,
        .scl_io_num = scl_pin,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus_config, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2C bus: %s", esp_err_to_name(err));
        return err;
    }

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PCF85063_ADDR,
        .scl_speed_hz = 100000,
    };
    err = i2c_master_bus_add_device(s_bus, &dev_config, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device: %s", esp_err_to_name(err));
        return err;
    }

    uint8_t buf[2] = { REG_CTRL1, 0x00 };
    err = i2c_master_transmit(s_dev, buf, sizeof(buf), 1000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "PCF85063 not responding on I2C: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "PCF85063 initialized");
    return ESP_OK;
}

esp_err_t Pcf85063_SetTime(const struct tm *t)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;

    uint8_t buf[8];
    buf[0] = REG_SECONDS;
    buf[1] = bin2bcd(t->tm_sec) & 0x7F;
    buf[2] = bin2bcd(t->tm_min);
    buf[3] = bin2bcd(t->tm_hour);
    buf[4] = bin2bcd(t->tm_mday);
    buf[5] = bin2bcd(t->tm_wday);
    buf[6] = bin2bcd(t->tm_mon + 1);
    buf[7] = bin2bcd(t->tm_year % 100);

    esp_err_t err = i2c_master_transmit(s_dev, buf, sizeof(buf), 1000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write time to RTC: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t Pcf85063_GetTime(struct tm *t)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;

    uint8_t reg = REG_SECONDS;
    uint8_t data[7];
    esp_err_t err = i2c_master_transmit_receive(s_dev, &reg, 1, data, sizeof(data), 1000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read time from RTC: %s", esp_err_to_name(err));
        return err;
    }

    memset(t, 0, sizeof(*t));
    t->tm_sec  = bcd2bin(data[0] & 0x7F);
    t->tm_min  = bcd2bin(data[1] & 0x7F);
    t->tm_hour = bcd2bin(data[2] & 0x3F);
    t->tm_mday = bcd2bin(data[3] & 0x3F);
    t->tm_wday = data[4] & 0x07;
    t->tm_mon  = bcd2bin(data[5] & 0x1F) - 1;
    t->tm_year = bcd2bin(data[6]) + 100;

    return ESP_OK;
}

i2c_master_bus_handle_t Pcf85063_GetBusHandle(void)
{
    return s_bus;
}