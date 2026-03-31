/**
 * Instabox Kiosk Firmware — ESP32-S3
 *
 * Test radar LD2420:
 *   - UART text output @ 115200: "ON\n", "OFF\n", "Range XXX\n"
 *   - GPIO OUT: HIGH = có người
 *   - LED sáng khi có người
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

/* ── LD2420 command protocol (binary) ────────────── */

#define LD2420_RESP_MAX  64

static bool ld2420_send_cmd(const uint8_t *cmd, int cmd_len,
                            uint8_t *resp, int *resp_len)
{
    uart_flush_input(BOARD_UART2_NUM);
    uart_write_bytes(BOARD_UART2_NUM, cmd, cmd_len);

    int total = 0;
    TickType_t start = xTaskGetTickCount();
    while ((xTaskGetTickCount() - start) * portTICK_PERIOD_MS < 500) {
        int n = uart_read_bytes(BOARD_UART2_NUM, &resp[total],
                                LD2420_RESP_MAX - total, pdMS_TO_TICKS(50));
        if (n > 0) total += n;
        if (total >= 4 &&
            resp[total-4] == 0x04 && resp[total-3] == 0x03 &&
            resp[total-2] == 0x02 && resp[total-1] == 0x01) {
            *resp_len = total;
            return true;
        }
    }
    *resp_len = total;
    return false;
}

static bool ld2420_open_command_mode(void)
{
    uint8_t cmd[] = {0xFD,0xFC,0xFB,0xFA, 0x04,0x00, 0xFF,0x00, 0x01,0x00, 0x04,0x03,0x02,0x01};
    uint8_t resp[LD2420_RESP_MAX];
    int rlen;
    if (!ld2420_send_cmd(cmd, sizeof(cmd), resp, &rlen)) return false;
    return (rlen >= 10 && resp[8] == 0x00 && resp[9] == 0x00);
}

static void ld2420_close_command_mode(void)
{
    uint8_t cmd[] = {0xFD,0xFC,0xFB,0xFA, 0x02,0x00, 0xFE,0x00, 0x04,0x03,0x02,0x01};
    uint8_t resp[LD2420_RESP_MAX];
    int rlen;
    ld2420_send_cmd(cmd, sizeof(cmd), resp, &rlen);
}

static bool ld2420_read_version(char *ver_buf, int buf_size)
{
    uint8_t cmd[] = {0xFD,0xFC,0xFB,0xFA, 0x02,0x00, 0x00,0x00, 0x04,0x03,0x02,0x01};
    uint8_t resp[LD2420_RESP_MAX];
    int rlen;
    if (!ld2420_send_cmd(cmd, sizeof(cmd), resp, &rlen)) return false;
    if (rlen < 14) return false;
    int str_len = resp[10] | (resp[11] << 8);
    if (str_len <= 0 || str_len > buf_size - 1) return false;
    memcpy(ver_buf, &resp[12], str_len);
    ver_buf[str_len] = '\0';
    return true;
}

static bool ld2420_read_param(uint16_t param_name, uint32_t *value)
{
    uint8_t cmd[] = {0xFD,0xFC,0xFB,0xFA, 0x04,0x00, 0x08,0x00,
                     (uint8_t)(param_name & 0xFF), (uint8_t)(param_name >> 8),
                     0x04,0x03,0x02,0x01};
    uint8_t resp[LD2420_RESP_MAX];
    int rlen;
    if (!ld2420_send_cmd(cmd, sizeof(cmd), resp, &rlen)) return false;
    if (rlen < 14 || resp[8] != 0x00 || resp[9] != 0x00) return false;
    *value = resp[10] | (resp[11] << 8) | (resp[12] << 16) | (resp[13] << 24);
    return true;
}

static void ld2420_print_config(void)
{
    ESP_LOGW(TAG, "--- LD2420 Configuration ---");

    char ver[32] = "???";
    if (ld2420_read_version(ver, sizeof(ver)))
        ESP_LOGI(TAG, "  Firmware    : %s", ver);
    else
        ESP_LOGE(TAG, "  Firmware    : read failed");

    uint32_t val;
    if (ld2420_read_param(0x0000, &val))
        ESP_LOGI(TAG, "  Min gate    : %lu", (unsigned long)val);
    if (ld2420_read_param(0x0001, &val))
        ESP_LOGI(TAG, "  Max gate    : %lu  (~%.1f m)", (unsigned long)val, val * 0.7f);
    if (ld2420_read_param(0x0004, &val))
        ESP_LOGI(TAG, "  Delay time  : %lu s", (unsigned long)val);

    ESP_LOGW(TAG, "--- Trigger / Maintain thresholds ---");
    for (uint16_t g = 0; g <= 0x0F; g++) {
        uint32_t trig = 0, maint = 0;
        ld2420_read_param(0x0010 + g, &trig);
        ld2420_read_param(0x0020 + g, &maint);
        ESP_LOGI(TAG, "  Gate %2u: trigger=%5lu  maintain=%5lu",
                 (unsigned)g, (unsigned long)trig, (unsigned long)maint);
    }
    ESP_LOGW(TAG, "------------------------------------");
}

/* ── LD2420 UART text parser ("ON", "OFF", "Range XXX") ── */

#define LINE_BUF_SIZE  64

typedef struct {
    bool person;
    int  range_cm;
} ld2420_state_t;

static ld2420_state_t s_state = { .person = false, .range_cm = -1 };

static void parse_ld2420_line(const char *line)
{
    if (strcmp(line, "ON") == 0) {
        s_state.person = true;
    } else if (strcmp(line, "OFF") == 0) {
        s_state.person = false;
        s_state.range_cm = 0;
    } else if (strncmp(line, "Range ", 6) == 0) {
        s_state.range_cm = atoi(&line[6]);
    }
}

/* ── Task: Đọc UART text + GPIO, hiển thị terminal ── */
static void task_radar(void *pv)
{
    (void)pv;

    /* Luôn gửi close command trước — phòng LD2420 bị kẹt ở command mode */
    ESP_LOGI(TAG, "Sending close-command-mode (safety reset)...");
    ld2420_close_command_mode();
    vTaskDelay(pdMS_TO_TICKS(100));
    uart_flush_input(BOARD_UART2_NUM);

    /* Thử đọc config */
    if (ld2420_open_command_mode()) {
        ESP_LOGI(TAG, "Command mode OK");
        ld2420_print_config();
        ld2420_close_command_mode();
        ESP_LOGI(TAG, "Command mode closed — resuming text output");
    } else {
        ESP_LOGW(TAG, "Command mode failed — TX may not be connected");
        /* Gửi close thêm lần nữa phòng trường hợp nó mở nhưng response lỗi */
        ld2420_close_command_mode();
    }

    vTaskDelay(pdMS_TO_TICKS(200));
    uart_flush_input(BOARD_UART2_NUM);
    ESP_LOGW(TAG, "=== Reading UART text + GPIO ===");

    char line_buf[LINE_BUF_SIZE];
    int  line_pos = 0;

    bool last_person = false;
    int  last_range  = -1;
    TickType_t last_print = 0;
    uint32_t uart_rx_count = 0;

    for (;;) {
        uint8_t c;
        int n = uart_read_bytes(BOARD_UART2_NUM, &c, 1, pdMS_TO_TICKS(50));

        if (n > 0) {
            uart_rx_count++;
            if (c == '\n' || c == '\r') {
                if (line_pos > 0) {
                    line_buf[line_pos] = '\0';
                    parse_ld2420_line(line_buf);
                    line_pos = 0;
                }
            } else if (c >= 0x20 && c <= 0x7E) {
                if (line_pos < LINE_BUF_SIZE - 1) {
                    line_buf[line_pos++] = (char)c;
                }
            }
        }

        /* Fallback: nếu chưa nhận UART text nào, dùng GPIO */
        if (n <= 0 && s_state.range_cm < 0) {
            s_state.person = radar_ld2420_person_present();
        }

        led_set(s_state.person);

        TickType_t now = xTaskGetTickCount();
        bool changed = (s_state.person != last_person || s_state.range_cm != last_range);
        bool time_to_print = ((now - last_print) * portTICK_PERIOD_MS >= 2000);

        if (changed || time_to_print) {
            last_person = s_state.person;
            last_range  = s_state.range_cm;
            last_print  = now;

            int gpio_val = gpio_get_level(BOARD_GPIO_LD2420_PRESENCE);

            if (s_state.person) {
                if (s_state.range_cm > 0) {
                    ESP_LOGW(TAG, ">>> CO NGUOI | Range: %d cm (%.1f m) | GPIO=%d <<<",
                             s_state.range_cm, s_state.range_cm / 100.0f, gpio_val);
                } else {
                    ESP_LOGW(TAG, ">>> CO NGUOI | GPIO=%d  [rx=%lu] <<<",
                             gpio_val, (unsigned long)uart_rx_count);
                }
            } else {
                if (s_state.range_cm >= 0) {
                    ESP_LOGI(TAG, "    Khong co nguoi | GPIO=%d", gpio_val);
                } else {
                    ESP_LOGI(TAG, "    Khong co nguoi | GPIO=%d  [rx=%lu, no UART text yet]",
                             gpio_val, (unsigned long)uart_rx_count);
                }
            }
        }

        if (n <= 0) {
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

    printf("\n\n");
    ESP_LOGW(TAG, "========================================");
    ESP_LOGW(TAG, "  RADAR LD2420 — TEST NHAN DIEN NGUOI");
    ESP_LOGW(TAG, "  GPIO OUT : %d", BOARD_GPIO_LD2420_PRESENCE);
    ESP_LOGW(TAG, "  UART2 TX : %d   RX : %d   BAUD : %d",
             BOARD_GPIO_UART2_TX, BOARD_GPIO_UART2_RX, BOARD_UART2_BAUD);
    ESP_LOGW(TAG, "========================================");

    xTaskCreatePinnedToCore(task_radar, "radar", 8192, NULL, 5, NULL, 1);
}
