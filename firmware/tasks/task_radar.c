#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"

#include "board_config.h"
#include "drivers/radar_ld2420.h"
#include "drivers/led.h"
#include "tasks/task_radar.h"

static const char *TAG = "task_radar";

#define PRINT_INTERVAL_MS  2000

static void task_radar_fn(void *pv)
{
    (void)pv;

    radar_ld2420_ensure_normal_mode();
    radar_ld2420_print_config();

    ESP_LOGW(TAG, "=== Reading UART text + GPIO ===");

    radar_ld2420_state_t state;
    radar_ld2420_state_t last = { .person = false, .range_cm = -1 };
    TickType_t last_print = 0;

    for (;;) {
        bool changed = radar_ld2420_poll(&state);
        led_set(state.person);

        TickType_t now = xTaskGetTickCount();
        bool time_to_print = ((now - last_print) * portTICK_PERIOD_MS >= PRINT_INTERVAL_MS);

        if (changed || time_to_print) {
            last = state;
            last_print = now;

            int gpio = gpio_get_level(BOARD_GPIO_LD2420_PRESENCE);

            if (state.person) {
                if (state.range_cm > 0)
                    ESP_LOGW(TAG, ">>> CO NGUOI | Range: %d cm (%.1f m) | GPIO=%d <<<",
                             state.range_cm, state.range_cm / 100.0f, gpio);
                else
                    ESP_LOGW(TAG, ">>> CO NGUOI | GPIO=%d <<<", gpio);
            } else {
                ESP_LOGI(TAG, "    Khong co nguoi | GPIO=%d", gpio);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void task_radar_start(void)
{
    xTaskCreatePinnedToCore(task_radar_fn, "radar", 8192, NULL, 5, NULL, 1);
}
