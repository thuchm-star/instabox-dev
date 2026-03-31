#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "board_config.h"
#include "drivers/radar_ld2420.h"

static const char *TAG = "ld2420";

#define UART_RX_BUF_SIZE  1024
#define UART_TX_BUF_SIZE  256
#define RESP_MAX          64
#define LINE_BUF_SIZE     64

static bool s_initialized;

/* ── Text parser state ───────────────────────────── */

static char s_line_buf[LINE_BUF_SIZE];
static int  s_line_pos;

static radar_ld2420_state_t s_state = {
    .person = false, .range_cm = -1, .health = RADAR_NOT_INIT
};

/* ── Health tracking ─────────────────────────────── */

#define HEALTH_TIMEOUT_MS     30000

static uint32_t s_uart_valid_count;
static TickType_t s_last_uart_valid_tick;

/* ── Init ────────────────────────────────────────── */

void radar_ld2420_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << BOARD_GPIO_LD2420_PRESENCE),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    uart_config_t uart_cfg = {
        .baud_rate  = BOARD_UART2_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    if (uart_param_config(BOARD_UART2_NUM, &uart_cfg) != ESP_OK) return;
    if (uart_set_pin(BOARD_UART2_NUM,
                     BOARD_GPIO_UART2_TX, BOARD_GPIO_UART2_RX,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) return;
    if (uart_driver_install(BOARD_UART2_NUM,
                            UART_RX_BUF_SIZE, UART_TX_BUF_SIZE,
                            0, NULL, 0) != ESP_OK) return;
    s_initialized = true;
}

/* ── GPIO OUT ────────────────────────────────────── */

bool radar_ld2420_person_present(void)
{
    if (!s_initialized) return false;
    return gpio_get_level(BOARD_GPIO_LD2420_PRESENCE) == 1;
}

/* ── UART text poll ("ON"/"OFF"/"Range XXX") ─────── */

static void parse_line(const char *line)
{
    bool valid = false;
    if (strcmp(line, "ON") == 0 || strcmp(line, "OFF") == 0) {
        valid = true;
    } else if (strncmp(line, "Range ", 6) == 0) {
        s_state.range_cm = atoi(&line[6]);
        valid = true;
    }
    if (valid) {
        s_uart_valid_count++;
        s_last_uart_valid_tick = xTaskGetTickCount();
    }
}

bool radar_ld2420_poll(radar_ld2420_state_t *out)
{
    if (!s_initialized) return false;

    radar_ld2420_state_t prev = s_state;

    uint8_t c;
    int n = uart_read_bytes(BOARD_UART2_NUM, &c, 1, pdMS_TO_TICKS(50));

    if (n > 0) {
        if (c == '\n' || c == '\r') {
            if (s_line_pos > 0) {
                s_line_buf[s_line_pos] = '\0';
                parse_line(s_line_buf);
                s_line_pos = 0;
            }
        } else if (c >= 0x20 && c <= 0x7E) {
            if (s_line_pos < LINE_BUF_SIZE - 1)
                s_line_buf[s_line_pos++] = (char)c;
        }
    }

    /* GPIO = presence, UART = range bonus */
    s_state.person = radar_ld2420_person_present();
    if (!s_state.person) s_state.range_cm = 0;

    /* Health */
    TickType_t now = xTaskGetTickCount();
    uint32_t since_ms = (now - s_last_uart_valid_tick) * portTICK_PERIOD_MS;
    if (s_uart_valid_count > 0 && since_ms < HEALTH_TIMEOUT_MS)
        s_state.health = RADAR_OK;
    else
        s_state.health = RADAR_GPIO_ONLY;

    *out = s_state;
    return (s_state.person != prev.person ||
            s_state.range_cm != prev.range_cm ||
            s_state.health != prev.health);
}

/* ── Command protocol (binary) ───────────────────── */
/* Header: FD FC FB FA | len(2B LE) | cmd+data | Tail: 04 03 02 01 */

static bool send_cmd(const uint8_t *cmd, int cmd_len,
                     uint8_t *resp, int *resp_len)
{
    uart_flush_input(BOARD_UART2_NUM);
    uart_write_bytes(BOARD_UART2_NUM, cmd, cmd_len);

    int total = 0;
    TickType_t start = xTaskGetTickCount();
    while ((xTaskGetTickCount() - start) * portTICK_PERIOD_MS < 500) {
        int n = uart_read_bytes(BOARD_UART2_NUM, &resp[total],
                                RESP_MAX - total, pdMS_TO_TICKS(50));
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

static bool open_command_mode(void)
{
    uint8_t cmd[] = {0xFD,0xFC,0xFB,0xFA, 0x04,0x00, 0xFF,0x00,
                     0x01,0x00, 0x04,0x03,0x02,0x01};
    uint8_t resp[RESP_MAX];
    int rlen;
    if (!send_cmd(cmd, sizeof(cmd), resp, &rlen)) return false;
    return (rlen >= 10 && resp[8] == 0x00 && resp[9] == 0x00);
}

static void close_command_mode(void)
{
    uint8_t cmd[] = {0xFD,0xFC,0xFB,0xFA, 0x02,0x00, 0xFE,0x00,
                     0x04,0x03,0x02,0x01};
    uint8_t resp[RESP_MAX];
    int rlen;
    send_cmd(cmd, sizeof(cmd), resp, &rlen);
}

static bool read_version(char *buf, int buf_size)
{
    uint8_t cmd[] = {0xFD,0xFC,0xFB,0xFA, 0x02,0x00, 0x00,0x00,
                     0x04,0x03,0x02,0x01};
    uint8_t resp[RESP_MAX];
    int rlen;
    if (!send_cmd(cmd, sizeof(cmd), resp, &rlen)) return false;
    if (rlen < 14) return false;
    int slen = resp[10] | (resp[11] << 8);
    if (slen <= 0 || slen > buf_size - 1) return false;
    memcpy(buf, &resp[12], slen);
    buf[slen] = '\0';
    return true;
}

static bool read_param(uint16_t name, uint32_t *value)
{
    uint8_t cmd[] = {0xFD,0xFC,0xFB,0xFA, 0x04,0x00, 0x08,0x00,
                     (uint8_t)(name & 0xFF), (uint8_t)(name >> 8),
                     0x04,0x03,0x02,0x01};
    uint8_t resp[RESP_MAX];
    int rlen;
    if (!send_cmd(cmd, sizeof(cmd), resp, &rlen)) return false;
    if (rlen < 14 || resp[8] != 0x00 || resp[9] != 0x00) return false;
    *value = resp[10] | (resp[11] << 8) | (resp[12] << 16) | (resp[13] << 24);
    return true;
}

/* ── Public: ensure normal mode ──────────────────── */

void radar_ld2420_ensure_normal_mode(void)
{
    if (!s_initialized) return;
    close_command_mode();
    vTaskDelay(pdMS_TO_TICKS(100));
    uart_flush_input(BOARD_UART2_NUM);
}

/* ── Public: print config ────────────────────────── */

bool radar_ld2420_print_config(void)
{
    if (!s_initialized) return false;

    if (!open_command_mode()) {
        ESP_LOGW(TAG, "Command mode failed — TX may not be connected");
        close_command_mode();
        return false;
    }

    ESP_LOGW(TAG, "--- LD2420 Configuration ---");

    char ver[32] = "???";
    if (read_version(ver, sizeof(ver)))
        ESP_LOGI(TAG, "  Firmware    : %s", ver);
    else
        ESP_LOGE(TAG, "  Firmware    : read failed");

    uint32_t val;
    if (read_param(0x0000, &val))
        ESP_LOGI(TAG, "  Min gate    : %lu", (unsigned long)val);
    if (read_param(0x0001, &val))
        ESP_LOGI(TAG, "  Max gate    : %lu  (~%.1f m)", (unsigned long)val, val * 0.7f);
    if (read_param(0x0004, &val))
        ESP_LOGI(TAG, "  Delay time  : %lu s", (unsigned long)val);

    ESP_LOGW(TAG, "--- Trigger / Maintain thresholds ---");
    for (uint16_t g = 0; g <= 0x0F; g++) {
        uint32_t trig = 0, maint = 0;
        read_param(0x0010 + g, &trig);
        read_param(0x0020 + g, &maint);
        ESP_LOGI(TAG, "  Gate %2u: trigger=%5lu  maintain=%5lu",
                 (unsigned)g, (unsigned long)trig, (unsigned long)maint);
    }
    ESP_LOGW(TAG, "------------------------------------");

    close_command_mode();
    uart_flush_input(BOARD_UART2_NUM);
    return true;
}
