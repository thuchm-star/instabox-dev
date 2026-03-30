#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "board_config.h"
#include "sensor_i2c.h"

#define SHTC3_I2C_ADDR         0x70
#define I2C_MASTER_TIMEOUT_MS  500

static const char *TAG = "shtc3";

/* Các lệnh cơ bản của SHTC3 (datasheet) */
#define SHTC3_CMD_WAKE       0x3517
#define SHTC3_CMD_SLEEP      0xB098
/* Single shot, clock stretching disabled, T first, normal power */
#define SHTC3_CMD_MEAS_T_RH  0x7866

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

void shtc3_init(void) {
    sensor_i2c_init();
    if (sensor_i2c_ready()) {
        s_initialized = true;
    }
}

bool shtc3_read(float *temperature_c, float *humidity_percent) {
    if (!temperature_c || !humidity_percent) {
        ESP_LOGE(TAG, "NULL pointer passed to shtc3_read");
        return false;
    }
    if (!s_initialized) {
        ESP_LOGE(TAG, "SHTC3 not initialized");
        return false;
    }

    esp_err_t err;
    uint8_t buf[6];

    /* Wake-up */
    uint8_t wake_cmd[2] = { (uint8_t)(SHTC3_CMD_WAKE >> 8), (uint8_t)(SHTC3_CMD_WAKE & 0xFF) };
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (SHTC3_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, wake_cmd, 2, true);
    i2c_master_stop(cmd);
    err = i2c_master_cmd_begin(BOARD_I2C_NUM, cmd, pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wake cmd failed: 0x%x", (unsigned)err);
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(1));

    /* Gửi lệnh đo T & RH */
    uint8_t meas_cmd[2] = { (uint8_t)(SHTC3_CMD_MEAS_T_RH >> 8), (uint8_t)(SHTC3_CMD_MEAS_T_RH & 0xFF) };
    cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (SHTC3_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, meas_cmd, 2, true);
    i2c_master_stop(cmd);
    err = i2c_master_cmd_begin(BOARD_I2C_NUM, cmd, pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "measure cmd failed: 0x%x", (unsigned)err);
        return false;
    }

    /* Thời gian đo: datasheet typ 12.1 ms, để dư 25 ms cho bus ổn định */
    vTaskDelay(pdMS_TO_TICKS(25));

    /* Đọc 6 byte: T(2) + CRC + RH(2) + CRC (thử tối đa 2 lần) */
    for (int attempt = 0; attempt < 2; attempt++) {
        cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (SHTC3_I2C_ADDR << 1) | I2C_MASTER_READ, true);
        i2c_master_read(cmd, buf, 5, I2C_MASTER_ACK);
        i2c_master_read_byte(cmd, &buf[5], I2C_MASTER_NACK);
        i2c_master_stop(cmd);
        err = i2c_master_cmd_begin(BOARD_I2C_NUM, cmd, pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
        i2c_cmd_link_delete(cmd);
        if (err == ESP_OK) {
            break;
        }
        if (attempt == 0) {
            vTaskDelay(pdMS_TO_TICKS(5));
        } else {
            ESP_LOGE(TAG, "read data failed: 0x%x", (unsigned)err);
            return false;
        }
    }

    uint16_t raw_t = ((uint16_t)buf[0] << 8) | buf[1];
    uint8_t crc_t = buf[2];
    uint16_t raw_rh = ((uint16_t)buf[3] << 8) | buf[4];
    uint8_t crc_rh = buf[5];

    if (crc8_sht(raw_t) != crc_t || crc8_sht(raw_rh) != crc_rh) {
        ESP_LOGE(TAG, "CRC check failed (T or RH)");
        return false;
    }

    /* Công thức từ datasheet: T = -45 + 175 * raw / 65535, RH = 100 * raw / 65535 */
    *temperature_c = -45.0f + 175.0f * ((float)raw_t / 65535.0f);
    *humidity_percent = 100.0f * ((float)raw_rh / 65535.0f);

    *temperature_c += SHTC3_TEMP_OFFSET_C;
    *humidity_percent += SHTC3_RH_OFFSET_PCT;
    if (*humidity_percent < 0.0f) *humidity_percent = 0.0f;
    if (*humidity_percent > 100.0f) *humidity_percent = 100.0f;

    ESP_LOGI(TAG, "T=%.2f C, RH=%.2f %%", *temperature_c, *humidity_percent);

    return true;
}

