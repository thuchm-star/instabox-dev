#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "board_config.h"
#include "drivers/radar_ld2420.h"
#include "drivers/led.h"
#include "tasks/task_radar.h"

static const char *TAG = "task_radar";
static TickType_t s_last_range_log_tick;
static TickType_t s_last_range_emit_tick;

/* ── Tunables ──────────────────────────────────────── *
 *  LD2420 already applies "absence report delay" (param 0x0004, seconds)
 *  in firmware — that is the main hold-after-loss. Software debounce here
 *  is only to filter UART/GPIO glitches, not to duplicate that delay. */

/** Đọc radar + log terminal / emit RANGE_UPDATE tối đa 5 Hz */
#define RADAR_REPORT_PERIOD_MS 200
#define POLL_MS                RADAR_REPORT_PERIOD_MS
#define DEBOUNCE_ENTER_MS      200     /* anti-glitch before ENTER         */
#define DEBOUNCE_LEAVE_MS      100     /* anti-glitch after module says go  */
#define HEARTBEAT_MS           RADAR_REPORT_PERIOD_MS   /* [hb] mỗi 0,2 s   */

/* ── Callback ──────────────────────────────────────── */

static radar_event_cb_t s_cb;

void task_radar_set_callback(radar_event_cb_t cb) { s_cb = cb; }

static void emit(const radar_event_t *evt)
{
    if (s_cb) s_cb(evt);
}

/* ── Default handler: LED + log ────────────────────── */

static void default_handler(const radar_event_t *evt)
{
    switch (evt->type) {
    case RADAR_EVT_PERSON_ENTER:
        led_set(true);
        ESP_LOGW(TAG, ">>> PERSON ENTER | range %d cm (%.1f m)",
                 evt->range_cm, evt->range_cm / 100.0f);
        break;

    case RADAR_EVT_PERSON_LEAVE:
        led_set(false);
        ESP_LOGI(TAG, "    PERSON LEAVE  (was present %lu ms)",
                 (unsigned long)evt->hold_ms);
        break;

    case RADAR_EVT_RANGE_UPDATE:
        if (evt->range_cm > 0) {
            TickType_t now = xTaskGetTickCount();
            if ((now - s_last_range_log_tick) * portTICK_PERIOD_MS >= 1000) {
                s_last_range_log_tick = now;
                ESP_LOGI(TAG, "    range update: %d cm (%.1f m)",
                         evt->range_cm, evt->range_cm / 100.0f);
            }
        }
        break;

    case RADAR_EVT_HEARTBEAT:
        if (evt->present)
            ESP_LOGI(TAG, "[hb] present %lu ms | range %d cm",
                     (unsigned long)evt->hold_ms, evt->range_cm);
        else
            ESP_LOGI(TAG, "[hb] clear   %lu ms",
                     (unsigned long)evt->hold_ms);
        break;
    }
}

/* ── Debounce state machine ────────────────────────── */

typedef enum {
    DEB_CLEAR,          /* confirmed: nobody          */
    DEB_MAYBE_ENTER,    /* raw=present, counting up   */
    DEB_PRESENT,        /* confirmed: person          */
    DEB_MAYBE_LEAVE,    /* raw=clear,  counting up    */
} deb_state_t;

/* ── Task ──────────────────────────────────────────── */

static void task_radar_fn(void *pv)
{
    (void)pv;

    ld2420_ensure_normal_mode();
    ld2420_log_config();
    ld2420_ensure_normal_mode();
    ESP_LOGI(TAG, "Radar ready: report every %d ms; glitch enter %d ms / leave %d ms",
             RADAR_REPORT_PERIOD_MS, DEBOUNCE_ENTER_MS, DEBOUNCE_LEAVE_MS);

    if (!s_cb) task_radar_set_callback(default_handler);

    deb_state_t  deb        = DEB_CLEAR;
    TickType_t   deb_start  = 0;           /* when transition candidate began */
    TickType_t   state_time = xTaskGetTickCount(); /* when confirmed state began */
    TickType_t   last_hb    = xTaskGetTickCount();
    int          last_range = -1;
    ld2420_state_t raw;

    for (;;) {
        ld2420_poll(&raw);
        bool raw_present = raw.present || (raw.range_cm > 0);
        TickType_t now = xTaskGetTickCount();

        /* ── Debounce FSM ─────────────────────────── */
        switch (deb) {

        case DEB_CLEAR:
            if (raw_present) {
                deb = DEB_MAYBE_ENTER;
                deb_start = now;
            }
            break;

        case DEB_MAYBE_ENTER:
            if (!raw_present) {
                deb = DEB_CLEAR;
            } else if ((now - deb_start) * portTICK_PERIOD_MS >= DEBOUNCE_ENTER_MS) {
                deb = DEB_PRESENT;
                state_time = now;
                last_range = raw.range_cm;
                s_last_range_emit_tick = now;
                radar_event_t ev = {
                    .type     = RADAR_EVT_PERSON_ENTER,
                    .present  = true,
                    .range_cm = raw.range_cm,
                    .hold_ms  = 0,
                };
                emit(&ev);
            }
            break;

        case DEB_PRESENT:
            if (!raw_present) {
                deb = DEB_MAYBE_LEAVE;
                deb_start = now;
            } else if (raw.range_cm > 0 && raw.range_cm != last_range) {
                if ((now - s_last_range_emit_tick) * portTICK_PERIOD_MS >= RADAR_REPORT_PERIOD_MS) {
                    s_last_range_emit_tick = now;
                    last_range = raw.range_cm;
                    radar_event_t ev = {
                        .type     = RADAR_EVT_RANGE_UPDATE,
                        .present  = true,
                        .range_cm = raw.range_cm,
                        .hold_ms  = (now - state_time) * portTICK_PERIOD_MS,
                    };
                    emit(&ev);
                }
            }
            break;

        case DEB_MAYBE_LEAVE:
            if (raw_present) {
                deb = DEB_PRESENT;
            } else if ((now - deb_start) * portTICK_PERIOD_MS >= DEBOUNCE_LEAVE_MS) {
                deb = DEB_CLEAR;
                uint32_t held = (now - state_time) * portTICK_PERIOD_MS;
                state_time = now;
                last_range = -1;
                radar_event_t ev = {
                    .type     = RADAR_EVT_PERSON_LEAVE,
                    .present  = false,
                    .range_cm = 0,
                    .hold_ms  = held,
                };
                emit(&ev);
            }
            break;
        }

        /* ── Periodic heartbeat ───────────────────── */
        if ((now - last_hb) * portTICK_PERIOD_MS >= HEARTBEAT_MS) {
            last_hb = now;
            bool confirmed = (deb == DEB_PRESENT || deb == DEB_MAYBE_LEAVE);
            radar_event_t ev = {
                .type     = RADAR_EVT_HEARTBEAT,
                .present  = confirmed,
                .range_cm = raw.range_cm,
                .hold_ms  = (now - state_time) * portTICK_PERIOD_MS,
            };
            emit(&ev);
        }

        /* ── LED tracks debounced state ───────────── */
        led_set(deb == DEB_PRESENT || deb == DEB_MAYBE_LEAVE);

        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}

void task_radar_start(void)
{
    xTaskCreatePinnedToCore(task_radar_fn, "radar", 8192, NULL, 5, NULL, 1);
}
