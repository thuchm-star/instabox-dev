#include <stdbool.h>
#include "driver/gpio.h"
#include "board_config.h"

static bool s_initialized;

void reed_switch_init(void) {
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << BOARD_GPIO_REED_SWITCH),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    s_initialized = true;
}

bool reed_switch_is_open(void) {
    if (!s_initialized) {
        return false;
    }
    /* Reed mở (cửa mở) = không nối GND = đọc HIGH do pull-up */
    return gpio_get_level(BOARD_GPIO_REED_SWITCH) == 1;
}
