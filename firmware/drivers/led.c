#include <stdbool.h>
#include "driver/gpio.h"
#include "board_config.h"

static bool s_initialized;

void led_init(void) {
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << BOARD_GPIO_LED),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level(BOARD_GPIO_LED, 0);
    s_initialized = true;
}

void led_set(bool on) {
    if (s_initialized) {
        gpio_set_level(BOARD_GPIO_LED, on ? 1 : 0);
    }
}
