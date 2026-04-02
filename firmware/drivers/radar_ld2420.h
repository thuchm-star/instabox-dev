#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ═══════════════════════════════════════════════════════════
 *  HLK-LD2420 24 GHz Human-Presence Radar Driver
 *
 *  Detection : GPIO (binary) + UART (text / energy frames)
 *  Config    : UART binary protocol (FD FC FB FA … 04 03 02 01)
 *  Range     : 16 gates × 0.7 m each  →  up to 11.2 m
 * ═══════════════════════════════════════════════════════════ */

#define LD2420_NUM_GATES   16
#define LD2420_GATE_CM     70      /* distance per gate ≈ 0.7 m */

/* ── Health ────────────────────────────────────────────── */

typedef enum {
    LD2420_HEALTH_NOT_INIT = 0,
    LD2420_HEALTH_OK,              /* UART receiving valid data */
    LD2420_HEALTH_GPIO_ONLY,       /* GPIO works, UART silent */
} ld2420_health_t;

/* ── Real-time detection state (filled by ld2420_poll) ── */

typedef struct {
    bool            present;                        /* GPIO: person detected     */
    int             range_cm;                       /* -1 no data, 0 clear, >0  */
    uint16_t        gate_energy[LD2420_NUM_GATES];  /* latest per-gate energy    */
    ld2420_health_t health;
} ld2420_state_t;

/* ── Module configuration ───────────────────────────── */

typedef struct {
    uint32_t min_gate;                          /* 0 – 15                       */
    uint32_t max_gate;                          /* 0 – 15                       */
    uint32_t timeout_s;                         /* hold time after detection (s) */
    uint32_t trigger[LD2420_NUM_GATES];         /* per-gate trigger threshold   */
    uint32_t hold[LD2420_NUM_GATES];            /* per-gate maintain threshold  */
} ld2420_config_t;

/* ── Lifecycle ──────────────────────────────────────── */

esp_err_t ld2420_init(void);

/* ── Normal-mode polling (call in task loop, ~50 ms) ── */

bool ld2420_poll(ld2420_state_t *out);
bool ld2420_gpio_present(void);
void ld2420_ensure_normal_mode(void);

/* ── Command-mode configuration (thread-safe) ───────── *
 *  Each function opens → executes → closes command mode. *
 *  Safe to call from any task; internally mutex-guarded. */

esp_err_t ld2420_read_version(char *buf, size_t buf_size);
esp_err_t ld2420_read_config(ld2420_config_t *cfg);
esp_err_t ld2420_write_config(const ld2420_config_t *cfg);
esp_err_t ld2420_set_gate_range(uint32_t min_gate, uint32_t max_gate);
esp_err_t ld2420_set_timeout(uint32_t seconds);
esp_err_t ld2420_set_gate_sensitivity(uint8_t gate,
                                      uint32_t trigger_val,
                                      uint32_t hold_val);
esp_err_t ld2420_reboot(void);

/* UART normal-mode data format (command 0x0012, see vendor / ESPHome LD2420). */
#define LD2420_UART_OUT_SIMPLE  0x0064u  /* ON/OFF/Range text */
#define LD2420_UART_OUT_ENERGY  0x0004u  /* binary F4 F3 F2 F1 … energy frames */

esp_err_t ld2420_set_uart_output_mode(uint16_t mode);

/* ── Diagnostics ────────────────────────────────────── */

esp_err_t ld2420_log_config(void);

/* Get latest state snapshot (filled by task_radar's ld2420_poll loop).
 * Safe to call from any task — reads cached state, no UART access. */
void ld2420_get_state(ld2420_state_t *out);

#ifdef __cplusplus
}
#endif
