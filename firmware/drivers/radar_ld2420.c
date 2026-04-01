/*
 * HLK-LD2420 24 GHz Radar Driver — ESP-IDF / FreeRTOS
 *
 * Architecture
 * ────────────
 *  ┌──────────────────────────────────────────────────┐
 *  │  Public API  (ld2420_*)                          │
 *  ├──────────────┬───────────────────────────────────┤
 *  │  Normal-mode │  Command-mode session             │
 *  │  parser      │  (mutex-guarded)                  │
 *  ├──────────────┴───────────────────────────────────┤
 *  │  Transport   (build_frame / transact / parse)    │
 *  ├──────────────────────────────────────────────────┤
 *  │  UART + GPIO  (ESP-IDF driver layer)             │
 *  └──────────────────────────────────────────────────┘
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "driver/gpio.h"

#include "board_config.h"
#include "drivers/radar_ld2420.h"

static const char *TAG = "ld2420";

/* ═══════════════════════════════════════════════════════
 *  Constants
 * ═══════════════════════════════════════════════════════ */

#define UART_PORT         BOARD_UART2_NUM
#define UART_RX_BUF       1024
#define UART_TX_BUF       256
#define GPIO_PRESENCE     BOARD_GPIO_LD2420_PRESENCE

/* Protocol markers */
static const uint8_t HDR_CMD[] = {0xFD, 0xFC, 0xFB, 0xFA};
static const uint8_t FTR_CMD[] = {0x04, 0x03, 0x02, 0x01};
static const uint8_t HDR_NRG[] = {0xF4, 0xF3, 0xF2, 0xF1};

/* Command words (little-endian on wire: low byte first) */
#define CMD_OPEN        0x00FF
#define CMD_CLOSE       0x00FE
#define CMD_VERSION     0x0000
#define CMD_REBOOT      0x0068
#define CMD_READ        0x0008
#define CMD_WRITE       0x0007

/* Parameter names */
#define PAR_MIN_GATE    0x0000
#define PAR_MAX_GATE    0x0001
#define PAR_TIMEOUT     0x0004
#define PAR_TRIG_BASE   0x0010      /* 0x10 – 0x1F */
#define PAR_HOLD_BASE   0x0020      /* 0x20 – 0x2F */

/* Timing */
#define CMD_TIMEOUT_MS  500
#define CMD_RETRY       3
#define HEALTH_MS       5000

/* Buffers */
#define FRAME_MAX       144         /* largest write batch: 4+2+2+16×6+4 = 108 */
#define RESP_MAX        144         /* largest read batch : 4+2+2+2+16×4+4 = 78 */

/* ═══════════════════════════════════════════════════════
 *  Driver state
 * ═══════════════════════════════════════════════════════ */

static bool              s_init;
static SemaphoreHandle_t s_mtx;

static ld2420_state_t    s_state = {
    .present = false, .range_cm = -1, .health = LD2420_HEALTH_NOT_INIT
};

static ld2420_config_t   s_cfg;
static bool              s_cfg_loaded;

static uint32_t          s_uart_cnt;
static TickType_t        s_uart_tick;

/* ═══════════════════════════════════════════════════════
 *  Normal-mode stream parser
 * ═══════════════════════════════════════════════════════ */

typedef enum { PS_IDLE, PS_NRG_LEN, PS_NRG_DATA } ps_t;

static ps_t    s_ps;
static uint8_t s_pbuf[64];
static int     s_ppos, s_pexp;
static int     s_nhdr;                  /* energy header match index */

#define LINE_MAX 64
static char    s_line[LINE_MAX];
static int     s_lpos;

/* ── text: ON / OFF / Range NNN ────────────────────── */

static void on_text(const char *ln)
{
    bool ok = false;
    if (strcmp(ln, "ON") == 0) {
        ok = true;
    } else if (strcmp(ln, "OFF") == 0) {
        s_state.range_cm = 0;
        ok = true;
    } else if (strncmp(ln, "Range ", 6) == 0) {
        s_state.range_cm = atoi(&ln[6]);
        ok = true;
    }
    if (ok) { s_uart_cnt++; s_uart_tick = xTaskGetTickCount(); }
}

/* ── energy frame: 3 status + 16×2 gate LE ─────────── */

static void on_energy(const uint8_t *d, int len)
{
    if (len < 35) return;

    int gmin = (int)s_cfg.min_gate;
    int gmax = (int)s_cfg.max_gate;
    if (gmax >= LD2420_NUM_GATES) gmax = LD2420_NUM_GATES - 1;
    int farthest = -1;

    for (int g = 0; g < LD2420_NUM_GATES; g++) {
        uint16_t e = (uint16_t)(d[3 + g * 2] | (d[3 + g * 2 + 1] << 8));
        s_state.gate_energy[g] = e;
        if (g >= gmin && g <= gmax) {
            uint32_t th = s_cfg_loaded ? s_cfg.trigger[g] : 250;
            if (e > th) farthest = g;
        }
    }

    s_state.range_cm = (farthest >= 0) ? (farthest + 1) * LD2420_GATE_CM : 0;
    s_uart_cnt++;
    s_uart_tick = xTaskGetTickCount();
}

/* ── byte-level feed (auto-detects text vs energy) ─── */

static void feed(uint8_t c)
{
    if (c == HDR_NRG[s_nhdr]) {
        if (++s_nhdr == 4) {
            s_ps   = PS_NRG_LEN;
            s_ppos = 0;
            s_nhdr = 0;
        }
        return;
    }
    if (s_nhdr > 0)
        s_nhdr = (c == HDR_NRG[0]) ? 1 : 0;

    switch (s_ps) {
    case PS_NRG_LEN:
        s_pbuf[s_ppos++] = c;
        if (s_ppos == 2) {
            s_pexp = s_pbuf[0] | (s_pbuf[1] << 8);
            if (s_pexp < 1 || s_pexp > 50) { s_ps = PS_IDLE; break; }
            s_ppos = 0;
            s_ps   = PS_NRG_DATA;
        }
        break;

    case PS_NRG_DATA:
        if (s_ppos < (int)sizeof(s_pbuf)) s_pbuf[s_ppos] = c;
        s_ppos++;
        if (s_ppos >= s_pexp) {
            on_energy(s_pbuf, s_ppos);
            s_ps = PS_IDLE;
        }
        break;

    default: /* PS_IDLE — accumulate text */
        if (c == '\n' || c == '\r') {
            if (s_lpos > 0) {
                s_line[s_lpos] = '\0';
                on_text(s_line);
                s_lpos = 0;
            }
        } else if (c >= 0x20 && c <= 0x7E) {
            if (s_lpos < LINE_MAX - 1)
                s_line[s_lpos++] = (char)c;
        }
        break;
    }
}

/* ═══════════════════════════════════════════════════════
 *  Transport layer
 * ═══════════════════════════════════════════════════════ */

static int build_frame(uint8_t *buf, uint16_t cmd,
                       const uint8_t *par, int par_len)
{
    int p = 0;
    memcpy(&buf[p], HDR_CMD, 4);                p += 4;
    uint16_t dl = (uint16_t)(2 + par_len);
    buf[p++] = dl & 0xFF;
    buf[p++] = (dl >> 8) & 0xFF;
    buf[p++] = cmd & 0xFF;
    buf[p++] = (cmd >> 8) & 0xFF;
    if (par_len > 0 && par) { memcpy(&buf[p], par, par_len); p += par_len; }
    memcpy(&buf[p], FTR_CMD, 4);                p += 4;
    return p;
}

static bool transact(const uint8_t *tx, int txl,
                     uint8_t *rx, int rx_max, int *rxl)
{
    uart_flush_input(UART_PORT);
    uart_write_bytes(UART_PORT, tx, txl);

    int tot = 0;
    TickType_t t0 = xTaskGetTickCount();
    while ((xTaskGetTickCount() - t0) * portTICK_PERIOD_MS < CMD_TIMEOUT_MS) {
        int n = uart_read_bytes(UART_PORT, &rx[tot],
                                rx_max - tot, pdMS_TO_TICKS(50));
        if (n > 0) tot += n;
        if (tot >= 10 && memcmp(&rx[tot - 4], FTR_CMD, 4) == 0) {
            *rxl = tot;
            return true;
        }
    }
    *rxl = tot;
    return false;
}

/*
 * Parse command-mode response per HLK doc:
 *   [6:7]  return command (low = echo of sent cmd, high = 0x01)
 *   [8:9]  status: 00 00 = ACK/success, any other = NAK / error from module
 */
static bool parse_resp(const uint8_t *r, int rl, uint16_t sent,
                       const uint8_t **data, int *dlen)
{
    if (rl < 14) return false;
    if (memcmp(r, HDR_CMD, 4) != 0)              return false;
    if (memcmp(&r[rl - 4], FTR_CMD, 4) != 0)     return false;

    uint16_t fl = r[4] | (r[5] << 8);
    if ((int)fl != rl - 10) return false;

    uint16_t rc = r[6] | (r[7] << 8);
    if (rc != (sent | 0x0100)) {
        ESP_LOGW(TAG, "response cmd mismatch: got 0x%04x want 0x%04x",
                 (unsigned)rc, (unsigned)(sent | 0x0100));
        return false;
    }

    if (r[8] != 0x00 || r[9] != 0x00) {
        uint16_t st = (uint16_t)r[8] | ((uint16_t)r[9] << 8);
        ESP_LOGW(TAG, "sensor NAK: status=0x%04x (success=0x0000)", (unsigned)st);
        return false;
    }

    if (data) *data = &r[10];
    if (dlen) *dlen = fl - 4;           /* minus retcmd(2)+status(2) */
    return true;
}

/* ═══════════════════════════════════════════════════════
 *  Command-mode primitives (caller holds s_mtx)
 * ═══════════════════════════════════════════════════════ */

static bool cm_open(void)
{
    uint8_t par[] = {0x01, 0x00};
    uint8_t fr[16], rsp[RESP_MAX];
    int fl = build_frame(fr, CMD_OPEN, par, 2), rl;

    for (int i = 0; i < CMD_RETRY; i++) {
        if (transact(fr, fl, rsp, sizeof(rsp), &rl) &&
            parse_resp(rsp, rl, CMD_OPEN, NULL, NULL))
            return true;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return false;
}

static void cm_close(void)
{
    uint8_t fr[16], rsp[RESP_MAX];
    int fl = build_frame(fr, CMD_CLOSE, NULL, 0), rl;
    transact(fr, fl, rsp, sizeof(rsp), &rl);
}

static bool cm_version(char *buf, size_t bsz)
{
    uint8_t fr[16], rsp[RESP_MAX];
    int fl = build_frame(fr, CMD_VERSION, NULL, 0), rl;
    if (!transact(fr, fl, rsp, sizeof(rsp), &rl)) return false;

    const uint8_t *d; int dl;
    if (!parse_resp(rsp, rl, CMD_VERSION, &d, &dl)) return false;
    if (dl < 2) return false;

    int slen = d[0] | (d[1] << 8);
    if (slen <= 0 || slen > (int)bsz - 1 || slen > dl - 2) return false;
    memcpy(buf, &d[2], slen);
    buf[slen] = '\0';
    return true;
}

static bool cm_reboot(void)
{
    uint8_t fr[16], rsp[RESP_MAX];
    int fl = build_frame(fr, CMD_REBOOT, NULL, 0), rl;
    transact(fr, fl, rsp, sizeof(rsp), &rl);
    return true;
}

/* ── single param read/write ──────────────────────── */

static bool cm_read1(uint16_t name, uint32_t *val)
{
    uint8_t par[] = {name & 0xFF, (name >> 8) & 0xFF};
    uint8_t fr[16], rsp[RESP_MAX];
    int fl = build_frame(fr, CMD_READ, par, 2), rl;
    if (!transact(fr, fl, rsp, sizeof(rsp), &rl)) return false;

    const uint8_t *d; int dl;
    if (!parse_resp(rsp, rl, CMD_READ, &d, &dl) || dl < 4) return false;
    *val = (uint32_t)d[0] | ((uint32_t)d[1] << 8)
         | ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 24);
    return true;
}

static bool cm_write1(uint16_t name, uint32_t val)
{
    uint8_t par[6] = {
        name & 0xFF, (name >> 8) & 0xFF,
        val & 0xFF,  (val >> 8)  & 0xFF,
        (val >> 16) & 0xFF, (val >> 24) & 0xFF
    };
    uint8_t fr[20], rsp[RESP_MAX];
    int fl = build_frame(fr, CMD_WRITE, par, 6), rl;
    if (!transact(fr, fl, rsp, sizeof(rsp), &rl)) return false;
    return parse_resp(rsp, rl, CMD_WRITE, NULL, NULL);
}

/* ── batch param read/write (up to 16 at once) ────── */

static bool cm_read_batch(const uint16_t *names, int cnt, uint32_t *vals)
{
    if (cnt <= 0 || cnt > LD2420_NUM_GATES) return false;

    uint8_t par[LD2420_NUM_GATES * 2];
    for (int i = 0; i < cnt; i++) {
        par[i * 2]     = names[i] & 0xFF;
        par[i * 2 + 1] = (names[i] >> 8) & 0xFF;
    }

    uint8_t fr[FRAME_MAX], rsp[RESP_MAX];
    int fl = build_frame(fr, CMD_READ, par, cnt * 2), rl;
    if (!transact(fr, fl, rsp, sizeof(rsp), &rl)) return false;

    const uint8_t *d; int dl;
    if (!parse_resp(rsp, rl, CMD_READ, &d, &dl) || dl < cnt * 4) return false;

    for (int i = 0; i < cnt; i++) {
        int o = i * 4;
        vals[i] = (uint32_t)d[o] | ((uint32_t)d[o+1] << 8)
                | ((uint32_t)d[o+2] << 16) | ((uint32_t)d[o+3] << 24);
    }
    return true;
}

static bool cm_write_batch(const uint16_t *names,
                           const uint32_t *vals, int cnt)
{
    if (cnt <= 0 || cnt > LD2420_NUM_GATES) return false;

    uint8_t par[LD2420_NUM_GATES * 6];
    for (int i = 0; i < cnt; i++) {
        int o = i * 6;
        par[o+0] = names[i] & 0xFF;
        par[o+1] = (names[i] >> 8) & 0xFF;
        par[o+2] = vals[i] & 0xFF;
        par[o+3] = (vals[i] >> 8) & 0xFF;
        par[o+4] = (vals[i] >> 16) & 0xFF;
        par[o+5] = (vals[i] >> 24) & 0xFF;
    }

    uint8_t fr[FRAME_MAX], rsp[RESP_MAX];
    int fl = build_frame(fr, CMD_WRITE, par, cnt * 6), rl;
    if (!transact(fr, fl, rsp, sizeof(rsp), &rl)) return false;
    return parse_resp(rsp, rl, CMD_WRITE, NULL, NULL);
}

/* ── helper: read all 16 gate thresholds ──────────── */

static bool cm_read_gates(uint16_t base, uint32_t out[LD2420_NUM_GATES])
{
    uint16_t names[LD2420_NUM_GATES];
    for (int g = 0; g < LD2420_NUM_GATES; g++)
        names[g] = base + (uint16_t)g;
    return cm_read_batch(names, LD2420_NUM_GATES, out);
}

static bool cm_write_gates(uint16_t base,
                           const uint32_t vals[LD2420_NUM_GATES])
{
    uint16_t names[LD2420_NUM_GATES];
    for (int g = 0; g < LD2420_NUM_GATES; g++)
        names[g] = base + (uint16_t)g;
    return cm_write_batch(names, vals, LD2420_NUM_GATES);
}

/* ═══════════════════════════════════════════════════════
 *  Command-mode session helpers
 * ═══════════════════════════════════════════════════════ */

static esp_err_t session_begin(void)
{
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    if (!cm_open()) {
        ESP_LOGW(TAG, "Cannot enter command mode (TX wired?)");
        cm_close();
        xSemaphoreGive(s_mtx);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

static void session_end(void)
{
    cm_close();
    uart_flush_input(UART_PORT);
    xSemaphoreGive(s_mtx);
}

/* ═══════════════════════════════════════════════════════
 *  Public — Lifecycle
 * ═══════════════════════════════════════════════════════ */

esp_err_t ld2420_init(void)
{
    if (s_init) return ESP_OK;

    gpio_config_t io = {
        .pin_bit_mask  = 1ULL << GPIO_PRESENCE,
        .mode          = GPIO_MODE_INPUT,
        .pull_up_en    = GPIO_PULLUP_DISABLE,
        .pull_down_en  = GPIO_PULLDOWN_ENABLE,
        .intr_type     = GPIO_INTR_DISABLE,
    };
    esp_err_t e = gpio_config(&io);
    if (e != ESP_OK) { ESP_LOGE(TAG, "GPIO init fail"); return e; }

    uart_config_t uc = {
        .baud_rate  = BOARD_UART2_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    e = uart_param_config(UART_PORT, &uc);
    if (e != ESP_OK) { ESP_LOGE(TAG, "UART config fail"); return e; }

    e = uart_set_pin(UART_PORT,
                     BOARD_GPIO_UART2_TX, BOARD_GPIO_UART2_RX,
                     UART_PIN_NO_CHANGE,  UART_PIN_NO_CHANGE);
    if (e != ESP_OK) { ESP_LOGE(TAG, "UART pin fail"); return e; }

    e = uart_driver_install(UART_PORT, UART_RX_BUF, UART_TX_BUF, 0, NULL, 0);
    if (e != ESP_OK) { ESP_LOGE(TAG, "UART install fail"); return e; }

    s_mtx = xSemaphoreCreateMutex();
    if (!s_mtx) { ESP_LOGE(TAG, "Mutex alloc fail"); return ESP_ERR_NO_MEM; }

    s_cfg.max_gate = LD2420_NUM_GATES - 1;
    s_init = true;

    ESP_LOGI(TAG, "Init OK  UART%d TX=%d RX=%d GPIO=%d",
             UART_PORT, BOARD_GPIO_UART2_TX, BOARD_GPIO_UART2_RX,
             GPIO_PRESENCE);
    return ESP_OK;
}

/* ═══════════════════════════════════════════════════════
 *  Public — Normal-mode operations
 * ═══════════════════════════════════════════════════════ */

bool ld2420_gpio_present(void)
{
    return s_init && gpio_get_level(GPIO_PRESENCE) == 1;
}

bool ld2420_poll(ld2420_state_t *out)
{
    if (!s_init) return false;

    ld2420_state_t prev = s_state;

    if (xSemaphoreTake(s_mtx, 0) == pdTRUE) {
        uint8_t buf[128];
        int n = uart_read_bytes(UART_PORT, buf, sizeof(buf),
                                pdMS_TO_TICKS(50));
        for (int i = 0; i < n; i++) feed(buf[i]);
        xSemaphoreGive(s_mtx);
    }

    s_state.present = ld2420_gpio_present();

    TickType_t now = xTaskGetTickCount();
    uint32_t ms = (now - s_uart_tick) * portTICK_PERIOD_MS;
    if (s_uart_cnt > 0 && ms < HEALTH_MS)
        s_state.health = LD2420_HEALTH_OK;
    else {
        s_state.health = LD2420_HEALTH_GPIO_ONLY;
        s_state.range_cm = 0;
    }

    *out = s_state;
    return (s_state.present  != prev.present  ||
            s_state.range_cm != prev.range_cm ||
            s_state.health   != prev.health);
}

void ld2420_ensure_normal_mode(void)
{
    if (!s_init) return;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    cm_close();
    vTaskDelay(pdMS_TO_TICKS(200));
    uart_flush_input(UART_PORT);
    xSemaphoreGive(s_mtx);
}

/* ═══════════════════════════════════════════════════════
 *  Public — Command-mode API
 * ═══════════════════════════════════════════════════════ */

esp_err_t ld2420_read_version(char *buf, size_t buf_size)
{
    if (!s_init) return ESP_ERR_INVALID_STATE;
    if (!buf || buf_size < 2) return ESP_ERR_INVALID_ARG;

    esp_err_t err = session_begin();
    if (err != ESP_OK) return err;

    bool ok = cm_version(buf, buf_size);
    session_end();
    return ok ? ESP_OK : ESP_FAIL;
}

esp_err_t ld2420_read_config(ld2420_config_t *cfg)
{
    if (!s_init) return ESP_ERR_INVALID_STATE;
    if (!cfg) return ESP_ERR_INVALID_ARG;

    esp_err_t err = session_begin();
    if (err != ESP_OK) return err;

    bool ok = true;
    ok = ok && cm_read1(PAR_MIN_GATE, &cfg->min_gate);
    ok = ok && cm_read1(PAR_MAX_GATE, &cfg->max_gate);
    ok = ok && cm_read1(PAR_TIMEOUT,  &cfg->timeout_s);
    ok = ok && cm_read_gates(PAR_TRIG_BASE, cfg->trigger);
    ok = ok && cm_read_gates(PAR_HOLD_BASE, cfg->hold);

    if (ok) {
        s_cfg = *cfg;
        s_cfg_loaded = true;
    }

    session_end();
    return ok ? ESP_OK : ESP_FAIL;
}

esp_err_t ld2420_write_config(const ld2420_config_t *cfg)
{
    if (!s_init) return ESP_ERR_INVALID_STATE;
    if (!cfg) return ESP_ERR_INVALID_ARG;
    if (cfg->min_gate > 15 || cfg->max_gate > 15)
        return ESP_ERR_INVALID_ARG;

    esp_err_t err = session_begin();
    if (err != ESP_OK) return err;

    bool ok = true;
    ok = ok && cm_write1(PAR_MIN_GATE, cfg->min_gate);
    ok = ok && cm_write1(PAR_MAX_GATE, cfg->max_gate);
    ok = ok && cm_write1(PAR_TIMEOUT,  cfg->timeout_s);
    ok = ok && cm_write_gates(PAR_TRIG_BASE, cfg->trigger);
    ok = ok && cm_write_gates(PAR_HOLD_BASE, cfg->hold);

    if (ok) {
        s_cfg = *cfg;
        s_cfg_loaded = true;
        ESP_LOGI(TAG, "Config written (gates %lu–%lu, timeout %lus)",
                 (unsigned long)cfg->min_gate,
                 (unsigned long)cfg->max_gate,
                 (unsigned long)cfg->timeout_s);
    }

    session_end();
    return ok ? ESP_OK : ESP_FAIL;
}

esp_err_t ld2420_set_gate_range(uint32_t min_gate, uint32_t max_gate)
{
    if (!s_init) return ESP_ERR_INVALID_STATE;
    if (min_gate > 15 || max_gate > 15 || min_gate > max_gate)
        return ESP_ERR_INVALID_ARG;

    esp_err_t err = session_begin();
    if (err != ESP_OK) return err;

    bool ok = cm_write1(PAR_MIN_GATE, min_gate)
           && cm_write1(PAR_MAX_GATE, max_gate);

    if (ok) {
        s_cfg.min_gate = min_gate;
        s_cfg.max_gate = max_gate;
    }

    session_end();
    return ok ? ESP_OK : ESP_FAIL;
}

esp_err_t ld2420_set_timeout(uint32_t seconds)
{
    if (!s_init) return ESP_ERR_INVALID_STATE;

    esp_err_t err = session_begin();
    if (err != ESP_OK) return err;

    bool ok = cm_write1(PAR_TIMEOUT, seconds);
    if (ok) s_cfg.timeout_s = seconds;

    session_end();
    return ok ? ESP_OK : ESP_FAIL;
}

esp_err_t ld2420_set_gate_sensitivity(uint8_t gate,
                                      uint32_t trigger_val,
                                      uint32_t hold_val)
{
    if (!s_init) return ESP_ERR_INVALID_STATE;
    if (gate >= LD2420_NUM_GATES) return ESP_ERR_INVALID_ARG;

    esp_err_t err = session_begin();
    if (err != ESP_OK) return err;

    bool ok = cm_write1(PAR_TRIG_BASE + gate, trigger_val)
           && cm_write1(PAR_HOLD_BASE + gate, hold_val);

    if (ok) {
        s_cfg.trigger[gate] = trigger_val;
        s_cfg.hold[gate]    = hold_val;
    }

    session_end();
    return ok ? ESP_OK : ESP_FAIL;
}

esp_err_t ld2420_reboot(void)
{
    if (!s_init) return ESP_ERR_INVALID_STATE;

    esp_err_t err = session_begin();
    if (err != ESP_OK) return err;

    cm_reboot();

    session_end();
    vTaskDelay(pdMS_TO_TICKS(1000));
    uart_flush_input(UART_PORT);
    return ESP_OK;
}

/* ═══════════════════════════════════════════════════════
 *  Public — Diagnostics
 * ═══════════════════════════════════════════════════════ */

esp_err_t ld2420_log_config(void)
{
    if (!s_init) return ESP_ERR_INVALID_STATE;

    esp_err_t err = session_begin();
    if (err != ESP_OK) return err;

    ESP_LOGW(TAG, "========== LD2420 Configuration ==========");

    char ver[32] = "???";
    if (cm_version(ver, sizeof(ver)))
        ESP_LOGI(TAG, "  Firmware     : %s", ver);
    else
        ESP_LOGE(TAG, "  Firmware     : read failed");

    uint32_t v;
    if (cm_read1(PAR_MIN_GATE, &v)) {
        s_cfg.min_gate = v;
        ESP_LOGI(TAG, "  Min gate     : %lu", (unsigned long)v);
    }
    if (cm_read1(PAR_MAX_GATE, &v)) {
        s_cfg.max_gate = v;
        ESP_LOGI(TAG, "  Max gate     : %lu  (~%.1f m)",
                 (unsigned long)v, v * 0.7f);
    }
    if (cm_read1(PAR_TIMEOUT, &v)) {
        s_cfg.timeout_s = v;
        ESP_LOGI(TAG, "  Timeout      : %lu s", (unsigned long)v);
    }

    ESP_LOGW(TAG, "  Gate | Trigger | Maintain");
    ESP_LOGW(TAG, "  -----+---------+---------");

    s_cfg_loaded = true;
    for (uint16_t g = 0; g < LD2420_NUM_GATES; g++) {
        cm_read1(PAR_TRIG_BASE + g, &s_cfg.trigger[g]);
        cm_read1(PAR_HOLD_BASE + g, &s_cfg.hold[g]);
        ESP_LOGI(TAG, "    %2u | %6lu  | %6lu",
                 (unsigned)g,
                 (unsigned long)s_cfg.trigger[g],
                 (unsigned long)s_cfg.hold[g]);
    }

    ESP_LOGW(TAG, "==========================================");

    session_end();
    return ESP_OK;
}
