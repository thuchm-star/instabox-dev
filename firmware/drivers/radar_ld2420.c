#include <stdint.h>
#include <stdbool.h>
#include "driver/uart.h"
#include "driver/gpio.h"
#include "board_config.h"

#define UART_RX_BUF_SIZE  1024
#define UART_TX_BUF_SIZE  256

static bool s_initialized;

void radar_ld2420_init(void) {
    /* Chân OUT/OT2 của LD2420 → GPIO input (HIGH = có người trong khu vực) */
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << BOARD_GPIO_LD2420_PRESENCE),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    /* UART2 dùng khi cần cấu hình LD2420 (độ nhạy, vùng...) qua giao thức */
    uart_config_t uart_config = {
        .baud_rate = BOARD_UART2_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t err = uart_param_config(BOARD_UART2_NUM, &uart_config);
    if (err != ESP_OK) return;
    err = uart_set_pin(BOARD_UART2_NUM, BOARD_GPIO_UART2_TX, BOARD_GPIO_UART2_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) return;
    err = uart_driver_install(BOARD_UART2_NUM, UART_RX_BUF_SIZE, UART_TX_BUF_SIZE, 0, NULL, 0);
    if (err != ESP_OK) return;
    s_initialized = true;
}

bool radar_ld2420_person_present(void) {
    if (!s_initialized) {
        return false;
    }
    /* LD2420 OUT pin: HIGH = có người trong khu vực giám sát */
    return gpio_get_level(BOARD_GPIO_LD2420_PRESENCE) == 1;
}
