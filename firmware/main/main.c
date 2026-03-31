/**
 * Instabox Kiosk Firmware — ESP32-S3
 *
 * Kiến trúc theo GIAM_SAT_KIOSK_BAOCAO_11.md:
 *   Task 1 (Core 1): Đọc LD2420 radar (UART2 frame + GPIO OUT)
 *   Task 2 (Core 1): Đọc SHT30 (I2C, mỗi 2s) + reed switch (10Hz)
 *   Task 3A (Core 0): Presence → gửi ngay khi thay đổi (edge-triggered)
 *   Task 3B (Core 0): Env (temp/humi/door) → gửi mỗi 5 giây
 *
 * KHÔNG dùng WiFi / MQTT. RPi5 xử lý phần mạng.
 */
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_timer.h"

#include "driver/i2c.h"
#include "board_config.h"
#include "drivers/sht30.h"
#include "drivers/reed_switch.h"
#include "drivers/radar_ld2420.h"
#include "drivers/led.h"

static const char *TAG = "main";

/* ── Shared sensor data (protected by mutex) ────── */
typedef struct {
    bool    person;
    uint8_t zone;           /* 0 = không ai, 1–8 = vùng 0.75m */
    float   temp;
    float   humi;
    bool    door_open;      /* trạng thái hiện tại (đã debounce) */
    bool    door_changed;   /* true trong 1 chu kỳ khi vừa chuyển trạng thái */
} sensor_data_t;

static sensor_data_t      g_data = {0};
static SemaphoreHandle_t  g_mtx;
static SemaphoreHandle_t  g_uart_mtx;  /* bảo vệ UART1 TX — tránh 2 task gửi xen nhau */

/* ── LD2420 UART frame parser ───────────────────── */

#define LD2420_FRAME_MAX  32

/**
 * Parse frame LD2420: header AA FF 03 00 ... tail FD FC FB FA
 * Trả về true nếu tìm được frame hợp lệ.
 */
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

/* ── Task 1: Đọc LD2420 radar (Core 1) ─────────── */
static void task_read_radar(void *pv)
{
    (void)pv;

    uint8_t frame[LD2420_FRAME_MAX];
    int     idx = 0;
    uint8_t byte;

    for (;;) {
        int n = uart_read_bytes(BOARD_UART2_NUM, &byte, 1, pdMS_TO_TICKS(10));
        if (n <= 0) {
            /* Không có data từ UART → fallback đọc GPIO OUT */
            bool pres = radar_ld2420_person_present();
            if (xSemaphoreTake(g_mtx, pdMS_TO_TICKS(5)) == pdTRUE) {
                g_data.person = pres;
                if (!pres) g_data.zone = 0;
                xSemaphoreGive(g_mtx);
            }
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        if (idx < LD2420_FRAME_MAX) {
            frame[idx++] = byte;
        }

        /* Kiểm tra tail: FD FC FB FA */
        if (idx >= 4 &&
            frame[idx-4] == 0xFD && frame[idx-3] == 0xFC &&
            frame[idx-2] == 0xFB && frame[idx-1] == 0xFA) {

            bool pres;
            uint8_t zone;
            if (parse_ld2420_frame(frame, idx, &pres, &zone)) {
                if (xSemaphoreTake(g_mtx, pdMS_TO_TICKS(5)) == pdTRUE) {
                    g_data.person = pres;
                    g_data.zone   = zone;
                    xSemaphoreGive(g_mtx);
                }
            }
            idx = 0;
        }
    }
}

/* ── Task 2: Đọc SHT30 + reed switch debounce (Core 1) ── */

#define REED_DEBOUNCE_MS   50   /* bỏ nhiễu rung cơ khí */

static void task_read_env(void *pv)
{
    (void)pv;

    TickType_t last_sht_tick = 0;

    /* Reed switch debounce state */
    bool     door_confirmed    = reed_switch_is_open();
    bool     door_candidate    = door_confirmed;
    TickType_t candidate_since = xTaskGetTickCount();

    for (;;) {
        TickType_t now = xTaskGetTickCount();

        /* ── Reed switch — đọc 10Hz + debounce ─── */
        bool door_raw = reed_switch_is_open();

        if (door_raw != door_candidate) {
            door_candidate  = door_raw;
            candidate_since = now;
        }

        bool changed = false;
        uint32_t stable_ms = (now - candidate_since) * portTICK_PERIOD_MS;

        if (door_candidate != door_confirmed && stable_ms >= REED_DEBOUNCE_MS) {
            door_confirmed = door_candidate;
            changed = true;
            ESP_LOGI("reed", "Door %s (stable %lu ms)",
                     door_confirmed ? "OPENED" : "CLOSED",
                     (unsigned long)stable_ms);
        }

        if (xSemaphoreTake(g_mtx, pdMS_TO_TICKS(5)) == pdTRUE) {
            g_data.door_open    = door_confirmed;
            g_data.door_changed = changed;
            xSemaphoreGive(g_mtx);
        }

        /* ── SHT30 — mỗi 2 giây ──────────────── */
        if ((now - last_sht_tick) * portTICK_PERIOD_MS >= 2000) {
            last_sht_tick = now;
            float t, h;
            if (sht30_read(&t, &h)) {
                if (xSemaphoreTake(g_mtx, pdMS_TO_TICKS(10)) == pdTRUE) {
                    g_data.temp = t;
                    g_data.humi = h;
                    xSemaphoreGive(g_mtx);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/* ── Helpers ─────────────────────────────────────── */

/** Uptime in milliseconds (int64_t, không tràn) */
static int64_t uptime_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static int round1(float v)
{
    return (int)(v * 10.0f + (v >= 0 ? 0.5f : -0.5f));
}

static void uart_send(const char *buf, int len)
{
    if (xSemaphoreTake(g_uart_mtx, pdMS_TO_TICKS(100)) == pdTRUE) {
        uart_write_bytes(BOARD_UART1_NUM, buf, len);
        xSemaphoreGive(g_uart_mtx);
    }
}

/* ── Task 3A: Radar → gửi khi có thay đổi (Core 0) ─ */
static void task_send_presence(void *pv)
{
    (void)pv;

    bool last_person = false;
    uint8_t last_zone = 0;
    char buf[96];

    for (;;) {
        bool person;
        uint8_t zone;

        if (xSemaphoreTake(g_mtx, pdMS_TO_TICKS(5)) == pdTRUE) {
            person = g_data.person;
            zone   = g_data.zone;
            xSemaphoreGive(g_mtx);
        }

        if (person != last_person || zone != last_zone) {
            last_person = person;
            last_zone   = zone;

            int len = snprintf(buf, sizeof(buf),
                "{\"type\":\"presence\",\"person\":%s,\"zone\":%u,\"ts\":%lld}\n",
                person ? "true" : "false",
                (unsigned)zone,
                (long long)uptime_ms());

            uart_send(buf, len);
        }

        led_set(person);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/* ── Task 3B: Env → gửi mỗi 5 giây (Core 0) ──────── */
static void task_send_env(void *pv)
{
    (void)pv;

    char buf[128];

    for (;;) {
        float temp, humi;
        bool door_open, door_changed;

        if (xSemaphoreTake(g_mtx, pdMS_TO_TICKS(10)) == pdTRUE) {
            temp         = g_data.temp;
            humi         = g_data.humi;
            door_open    = g_data.door_open;
            door_changed = g_data.door_changed;
            xSemaphoreGive(g_mtx);
        }

        int len = snprintf(buf, sizeof(buf),
            "{\"type\":\"env\",\"temp\":%.1f,\"humi\":%.1f,"
            "\"door_open\":%s,\"door_changed\":%s,\"ts\":%lld}\n",
            (double)(round1(temp) / 10.0f),
            (double)(round1(humi) / 10.0f),
            door_open    ? "true" : "false",
            door_changed ? "true" : "false",
            (long long)uptime_ms());

        uart_send(buf, len);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

/* ── UART1 init (giao tiếp RPi5) ───────────────── */
static void uart_rpi5_init(void)
{
    uart_config_t cfg = {
        .baud_rate  = BOARD_UART1_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_param_config(BOARD_UART1_NUM, &cfg);
    uart_set_pin(BOARD_UART1_NUM,
                 BOARD_GPIO_UART1_TX, BOARD_GPIO_UART1_RX,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(BOARD_UART1_NUM, 256, 256, 0, NULL, 0);
}

/* ── Watchdog feed ──────────────────────────────── */
static void task_watchdog(void *pv)
{
    (void)pv;
    int level = 0;
    for (;;) {
        level = 1 - level;
        gpio_set_level(BOARD_GPIO_WATCHDOG, level);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/* ═══════════════════════════════════════════════════
 *  APP MAIN
 * ═══════════════════════════════════════════════════ */
void app_main(void)
{
    /* NVS (cần cho một số IDF component) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    /* Hardware init */
    radar_ld2420_init();     /* UART2 @256000 + GPIO OUT */
    sht30_init();            /* I2C SDA=8, SCL=9 */
    reed_switch_init();      /* GPIO 4, pull-up */
    led_init();              /* GPIO 2 */
    uart_rpi5_init();        /* UART1 @115200 → RPi5 */

    /* I2C bus scan — debug: xem thiết bị nào đang có mặt */
    ESP_LOGI(TAG, "I2C scan on SDA=%d SCL=%d ...", BOARD_GPIO_I2C_SDA, BOARD_GPIO_I2C_SCL);
    int found = 0;
    for (uint8_t addr = 0x03; addr <= 0x77; addr++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);
        esp_err_t err = i2c_master_cmd_begin(BOARD_I2C_NUM, cmd, pdMS_TO_TICKS(50));
        i2c_cmd_link_delete(cmd);
        if (err == ESP_OK) {
            ESP_LOGW(TAG, "  I2C device found at 0x%02X", addr);
            found++;
        }
    }
    if (found == 0) {
        ESP_LOGE(TAG, "  No I2C devices found! Check wiring: SDA=GPIO%d, SCL=GPIO%d",
                 BOARD_GPIO_I2C_SDA, BOARD_GPIO_I2C_SCL);
    } else {
        ESP_LOGI(TAG, "  %d I2C device(s) found", found);
    }

    /* Watchdog GPIO */
    gpio_config_t wdt_io = {
        .pin_bit_mask = (1ULL << BOARD_GPIO_WATCHDOG),
        .mode         = GPIO_MODE_OUTPUT,
    };
    gpio_config(&wdt_io);

    ESP_LOGI(TAG, "Hardware initialized — 3 sensor tasks starting");

    /* Mutex */
    g_mtx      = xSemaphoreCreateMutex();
    g_uart_mtx = xSemaphoreCreateMutex();

    /* 2 task đọc (Core 1) + 2 task gửi (Core 0) + watchdog */
    xTaskCreatePinnedToCore(task_read_radar,    "radar",    4096, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(task_read_env,      "env",      3072, NULL, 4, NULL, 1);
    xTaskCreatePinnedToCore(task_send_presence, "tx_pres",  3072, NULL, 3, NULL, 0);
    xTaskCreatePinnedToCore(task_send_env,      "tx_env",   3072, NULL, 3, NULL, 0);
    xTaskCreate(task_watchdog, "wdt", 1024, NULL, 6, NULL);

    ESP_LOGI(TAG, "All tasks running. JSON output on UART1 (GPIO %d) @ %d baud",
             BOARD_GPIO_UART1_TX, BOARD_UART1_BAUD);
}
