/**
 * I2C bus dùng chung cho cảm biến. Chỉ i2c_driver_install một lần.
 */
#include "sensor_i2c.h"
#include "board_config.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "sensor_i2c";
static bool s_initialized;

void sensor_i2c_init(void) {
    if (s_initialized) {
        return;
    }
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = BOARD_GPIO_I2C_SDA,
        .scl_io_num = BOARD_GPIO_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = BOARD_I2C_FREQ_HZ,
    };
    esp_err_t err = i2c_param_config(BOARD_I2C_NUM, &conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_param_config failed: 0x%x", (unsigned)err);
        return;
    }
    err = i2c_driver_install(BOARD_I2C_NUM, conf.mode, 0, 0, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_driver_install failed: 0x%x", (unsigned)err);
        return;
    }
    s_initialized = true;
    vTaskDelay(pdMS_TO_TICKS(10));  /* Để bus ổn định trước giao dịch đầu */
}

bool sensor_i2c_ready(void) {
    return s_initialized;
}
