/**
 * Driver SHT30 (Sensirion) - nhiệt độ & độ ẩm qua I2C.
 * Địa chỉ: 0x44 (mặc định) hoặc 0x45 nếu chân ADDR nối VDD.
 * Lệnh: 0x2C06 = single shot, high repeatability, clock stretching.
 * Công thức: T = -45 + 175*rawT/65535, RH = 100*rawRH/65535.
 */
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "board_config.h"
#include "sensor_i2c.h"

#define SHT30_I2C_ADDR         0x44
#define I2C_MASTER_TIMEOUT_MS  500

/* Single shot, high repeatability, clock stretching */
#define SHT30_CMD_MEASURE      0x2C06

static const char *TAG = "sht30";
static bool s_initialized;

static uint8_t crc8_sht(uint16_t value) {
    uint8_t data[2] = { (uint8_t)(value >> 8), (uint8_t)(value & 0xFF) };
    uint8_t crc = 0xFF;
    for (int i = 0; i < 2; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            if (crc & 0x80) {
                crc = (uint8_t)((crc << 1) ^ 0x31);
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

void sht30_init(void) {
    sensor_i2c_init();
    if (sensor_i2c_ready()) {
        s_initialized = true;
    }
}

bool sht30_read(float *temperature_c, float *humidity_percent) {
    if (!temperature_c || !humidity_percent) {
        ESP_LOGE(TAG, "NULL pointer");
        return false;
    }
    if (!s_initialized) {
        ESP_LOGE(TAG, "SHT30 not initialized");
        return false;
    }

    esp_err_t err;
    uint8_t buf[6];
    uint8_t cmd[2] = { (uint8_t)(SHT30_CMD_MEASURE >> 8), (uint8_t)(SHT30_CMD_MEASURE & 0xFF) };

    /* Gửi lệnh đo */
    i2c_cmd_handle_t h = i2c_cmd_link_create();
    i2c_master_start(h);
    i2c_master_write_byte(h, (SHT30_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(h, cmd, 2, true);
    i2c_master_stop(h);
    err = i2c_master_cmd_begin(BOARD_I2C_NUM, h, pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
    i2c_cmd_link_delete(h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "measure cmd failed: 0x%x", (unsigned)err);
        return false;
    }

    /* SHT30 cần ~15 ms để đo (clock stretching hoặc delay) */
    vTaskDelay(pdMS_TO_TICKS(20));

    /* Đọc 6 byte: T(2)+CRC, RH(2)+CRC */
    h = i2c_cmd_link_create();
    i2c_master_start(h);
    i2c_master_write_byte(h, (SHT30_I2C_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read(h, buf, 5, I2C_MASTER_ACK);
    i2c_master_read_byte(h, &buf[5], I2C_MASTER_NACK);
    i2c_master_stop(h);
    err = i2c_master_cmd_begin(BOARD_I2C_NUM, h, pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
    i2c_cmd_link_delete(h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "read failed: 0x%x", (unsigned)err);
        return false;
    }

    uint16_t raw_t = ((uint16_t)buf[0] << 8) | buf[1];
    uint8_t crc_t = buf[2];
    uint16_t raw_rh = ((uint16_t)buf[3] << 8) | buf[4];
    uint8_t crc_rh = buf[5];

    if (crc8_sht(raw_t) != crc_t || crc8_sht(raw_rh) != crc_rh) {
        ESP_LOGE(TAG, "CRC error");
        return false;
    }

    *temperature_c = -45.0f + 175.0f * ((float)raw_t / 65535.0f);
    *humidity_percent = 100.0f * ((float)raw_rh / 65535.0f);
    ESP_LOGI(TAG, "T=%.2f C, RH=%.2f %%", *temperature_c, *humidity_percent);
    return true;
}
