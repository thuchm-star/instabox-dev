#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "drivers/sht30.h"
#include "tasks/task_env.h"

static const char *TAG = "task_env";

#define READ_INTERVAL_MS  2000

static void task_env_fn(void *pv)
{
    (void)pv;

    float last_temp = -999.0f;
    float last_humi = -999.0f;

    ESP_LOGW(TAG, "=== Reading SHT30 every %d ms ===", READ_INTERVAL_MS);

    for (;;) {
        float temp, humi;

        if (sht30_read(&temp, &humi)) {
            bool changed = (int)(temp * 10) != (int)(last_temp * 10) ||
                           (int)(humi * 10) != (int)(last_humi * 10);

            if (changed) {
                last_temp = temp;
                last_humi = humi;
                ESP_LOGI(TAG, "Temp: %.1f C  |  Humi: %.1f %%", temp, humi);
            }
        } else {
            ESP_LOGE(TAG, "SHT30 read failed");
        }

        vTaskDelay(pdMS_TO_TICKS(READ_INTERVAL_MS));
    }
}

void task_env_start(void)
{
    xTaskCreatePinnedToCore(task_env_fn, "env", 4096, NULL, 4, NULL, 1);
}
