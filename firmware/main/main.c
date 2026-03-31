/**
 * Instabox Kiosk Firmware — ESP32-S3
 *
 * Chế độ test đơn giản: CHỈ dùng radar LD2420.
 * Đọc chân GPIO OUT của LD2420, hiển thị CÓ/KHÔNG CÓ người trên serial terminal.
 * LED sáng khi có người.
 */
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/uart.h"

#include "board_config.h"
#include "drivers/radar_ld2420.h"
#include "drivers/led.h"

static const char *TAG = "radar";

#define LD2420_FRAME_MAX  32

static bool parse_ld2420_frame(const uint8_t *buf, int len,
                               bool *presence, uint8_t *zone)
{
    for (int i = 0; i <= len - 12; i++) {
        if (buf[i]   == 0xAA && buf[i+1] == 0xFF &&
            buf[i+2] == 0x03 && buf[i+3] == 0x00) {
            *presence = (buf[i+4] & 0x01);
            *zone     = buf[i+5];
            return true;
        }
    }
    return false;
}

/* ── Task: Đọc radar + hiển thị terminal ────────── */
static void task_radar(void *pv)
{
    (void)pv;

    bool last_person = false;
    bool first_print = true;

    uint8_t frame[LD2420_FRAME_MAX];
    int     idx = 0;
    uint8_t byte;

    for (;;) {
        bool person = false;
        uint8_t zone = 0;
        bool got_data = false;

        int n = uart_read_bytes(BOARD_UART2_NUM, &byte, 1, pdMS_TO_TICKS(10));
        if (n <= 0) {
            person = radar_ld2420_person_present();
            got_data = true;
        } else {
            if (idx < LD2420_FRAME_MAX) {
                frame[idx++] = byte;
            }
            if (idx >= 4 &&
                frame[idx-4] == 0xFD && frame[idx-3] == 0xFC &&
                frame[idx-2] == 0xFB && frame[idx-1] == 0xFA) {
                got_data = parse_ld2420_frame(frame, idx, &person, &zone);
                idx = 0;
            }
        }

        if (got_data && (person != last_person || first_print)) {
            first_print = false;
            last_person = person;
            led_set(person);

            if (person) {
                ESP_LOGW(TAG, ">>> CÓ NGƯỜI  (zone %u) <<<", (unsigned)zone);
            } else {
                ESP_LOGI(TAG, "    Không có người");
            }
        }

        if (!got_data) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

/* ═══════════════════════════════════════════════════
 *  APP MAIN
 * ═══════════════════════════════════════════════════ */
void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    radar_ld2420_init();
    led_init();

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  Radar LD2420 — Test nhận diện người");
    ESP_LOGI(TAG, "  GPIO OUT: %d  |  UART2 TX:%d RX:%d",
             BOARD_GPIO_LD2420_PRESENCE, BOARD_GPIO_UART2_TX, BOARD_GPIO_UART2_RX);
    ESP_LOGI(TAG, "========================================");

    xTaskCreatePinnedToCore(task_radar, "radar", 4096, NULL, 5, NULL, 1);
}
