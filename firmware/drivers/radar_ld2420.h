#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool person;
    int  range_cm;      /* -1 = chưa có data, 0 = OFF, >0 = khoảng cách */
} radar_ld2420_state_t;

/* Init GPIO OUT + UART2 */
void radar_ld2420_init(void);

/* Đọc trực tiếp chân GPIO OUT (HIGH = có người) */
bool radar_ld2420_person_present(void);

/* Đọc 1 byte UART, parse text "ON"/"OFF"/"Range XXX". Gọi liên tục trong loop.
 * Trả về true khi state thay đổi. */
bool radar_ld2420_poll(radar_ld2420_state_t *state);

/* Command protocol: đảm bảo LD2420 ở normal mode (thoát command mode nếu bị kẹt) */
void radar_ld2420_ensure_normal_mode(void);

/* Command protocol: đọc và in config ra log (version, gates, thresholds).
 * Trả về true nếu đọc thành công. */
bool radar_ld2420_print_config(void);
