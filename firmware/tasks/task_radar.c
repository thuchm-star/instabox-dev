#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "board_config.h"
#include "drivers/radar_ld2420.h"
#include "drivers/led.h"
#include "tasks/task_radar.h"

static const char *TAG = "task_radar";

#define PRINT_INTERVAL_MS  5000

static void task_radar_fn(void *pv)
{
    (void)pv;

    radar_ld2420_ensure_normal_mode();
    radar_ld2420_print_config();

    radar_ld2420_state_t state;
    radar_health_t last_health = RADAR_NOT_INIT;
    TickType_t last_print = 0;

    for (;;) {
        bool changed = radar_ld2420_poll(&state);
        led_set(state.person);

        if (state.health != last_health) {
            last_health = state.health;
            if (state.health == RADAR_GPIO_ONLY)
                ESP_LOGW(TAG, "GPIO only — no UART data");
            else if (state.health == RADAR_OK)
                ESP_LOGI(TAG, "UART + GPIO active");
        }

        TickType_t now = xTaskGetTickCount();
        bool time_to_print = ((now - last_print) * portTICK_PERIOD_MS >= PRINT_INTERVAL_MS);

        if (changed || time_to_print) {
            last_print = now;

            if (state.person) {
                if (state.range_cm > 0)
                    ESP_LOGW(TAG, ">>> CO NGUOI | Range: %d cm (%.1f m) <<<",
                             state.range_cm, state.range_cm / 100.0f);
                else
                    ESP_LOGW(TAG, ">>> CO NGUOI <<<");
            } else {
                ESP_LOGI(TAG, "    Khong co nguoi");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void task_radar_start(void)
{
    xTaskCreatePinnedToCore(task_radar_fn, "radar", 8192, NULL, 5, NULL, 1);
}
