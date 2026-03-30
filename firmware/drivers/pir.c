#include <stdbool.h>
#include "driver/gpio.h"
#include "board_config.h"

static bool s_initialized;

void pir_init(void) {
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << BOARD_GPIO_PIR),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    s_initialized = true;
}

bool pir_motion_detected(void) {
    if (!s_initialized) {
        return false;
    }
    /* PIR thường output HIGH khi có motion */
    return gpio_get_level(BOARD_GPIO_PIR) == 1;
}
