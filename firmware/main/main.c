/**
 * Instabox Kiosk Firmware — ESP32-S3
 *
 * app_main: init hardware, start tasks.
 * Tasks: firmware/tasks/
 * Drivers: firmware/drivers/
 */
#include <stdio.h>
#include "nvs_flash.h"
#include "esp_log.h"

#include "board_config.h"
#include "drivers/radar_ld2420.h"
#include "drivers/sht30.h"
#include "drivers/led.h"
#include "tasks/task_radar.h"
#include "tasks/task_env.h"

static const char *TAG = "main";

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // radar_ld2420_init();
    sht30_init();
    // led_init();

    printf("\n\n");
    ESP_LOGW(TAG, "========================================");
    // ESP_LOGW(TAG, "  INSTABOX KIOSK FIRMWARE");
    // ESP_LOGW(TAG, "  Radar  : GPIO=%d  UART TX=%d RX=%d @ %d",
    //          BOARD_GPIO_LD2420_PRESENCE,
    //          BOARD_GPIO_UART2_TX, BOARD_GPIO_UART2_RX, BOARD_UART2_BAUD);
    ESP_LOGW(TAG, "  SHT30  : I2C SDA=%d SCL=%d @ %d Hz",
             BOARD_GPIO_I2C_SDA, BOARD_GPIO_I2C_SCL, BOARD_I2C_FREQ_HZ);
    ESP_LOGW(TAG, "========================================");

    // task_radar_start();
    task_env_start();
}
