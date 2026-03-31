/**
 * Instabox Kiosk Firmware — ESP32-S3
 *
 * app_main: khởi tạo hardware, start các task.
 * Mỗi task nằm trong firmware/tasks/
 */
#include <stdio.h>
#include "nvs_flash.h"
#include "esp_log.h"

#include "board_config.h"
#include "drivers/radar_ld2420.h"
#include "drivers/led.h"
#include "tasks/task_radar.h"

static const char *TAG = "main";

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    radar_ld2420_init();
    led_init();

    printf("\n\n");
    ESP_LOGW(TAG, "========================================");
    ESP_LOGW(TAG, "  RADAR LD2420 — TEST NHAN DIEN NGUOI");
    ESP_LOGW(TAG, "  GPIO OUT : %d", BOARD_GPIO_LD2420_PRESENCE);
    ESP_LOGW(TAG, "  UART2 TX : %d   RX : %d   BAUD : %d",
             BOARD_GPIO_UART2_TX, BOARD_GPIO_UART2_RX, BOARD_UART2_BAUD);
    ESP_LOGW(TAG, "========================================");

    task_radar_start();
}
