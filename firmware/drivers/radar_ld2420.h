#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    RADAR_OK,               /* UART text hoat dong, data hop le */
    RADAR_GPIO_ONLY,        /* Chi co GPIO, UART khong nhan data */
    RADAR_DISCONNECTED,     /* Khong co tin hieu (GPIO=0, UART=0) */
    RADAR_NOT_INIT,
} radar_health_t;

typedef struct {
    bool          person;
    int           range_cm;     /* -1 = chua co data, 0 = OFF, >0 = khoang cach */
    radar_health_t health;
} radar_ld2420_state_t;

void radar_ld2420_init(void);
bool radar_ld2420_person_present(void);

/* Poll UART text + GPIO. Goi lien tuc trong loop.
 * Tra ve true khi state thay doi. */
bool radar_ld2420_poll(radar_ld2420_state_t *state);

void radar_ld2420_ensure_normal_mode(void);
bool radar_ld2420_print_config(void);
