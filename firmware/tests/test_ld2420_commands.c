/*
 * LD2420 command-mode HIL suite — logs esp_err_t and payload for each API.
 * Enable: idf.py menuconfig → Instabox firmware → Run LD2420 UART command test…
 */

#include <stdio.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "drivers/radar_ld2420.h"
#include "tests/test_ld2420_commands.h"

static const char *TAG = "ld2420_test";

static void line(const char *title)
{
    ESP_LOGW(TAG, "---------- %s ----------", title);
}

static void ok_err(const char *step, esp_err_t e)
{
    const char *name = esp_err_to_name(e);
    if (e == ESP_OK)
        ESP_LOGI(TAG, "  OK   %-32s  %s", step, name);
    else
        ESP_LOGW(TAG, "  FAIL %-32s  %s (%d)", step, name, (int)e);
}

void ld2420_run_all_command_tests(void)
{
#if !CONFIG_INSTABOX_LD2420_CMD_TEST
    (void)TAG;
    return;
#else

    line("LD2420 command suite (UART2)");
    vTaskDelay(pdMS_TO_TICKS(300));

    /* --- read_version --- */
    {
        char ver[48];
        esp_err_t e = ld2420_read_version(ver, sizeof(ver));
        ok_err("ld2420_read_version", e);
        if (e == ESP_OK)
            ESP_LOGI(TAG, "       response: firmware \"%s\"", ver);
    }

    /* --- read_config --- */
    ld2420_config_t cfg = {0};
    bool cfg_ok = false;
    {
        esp_err_t e = ld2420_read_config(&cfg);
        ok_err("ld2420_read_config", e);
        if (e == ESP_OK) {
            cfg_ok = true;
            ESP_LOGI(TAG, "       response: min_gate=%lu max_gate=%lu timeout_s=%lu",
                     (unsigned long)cfg.min_gate,
                     (unsigned long)cfg.max_gate,
                     (unsigned long)cfg.timeout_s);
            ESP_LOGI(TAG, "       response: gate0 trigger=%lu hold=%lu | gate15 tr=%lu ho=%lu",
                     (unsigned long)cfg.trigger[0], (unsigned long)cfg.hold[0],
                     (unsigned long)cfg.trigger[15], (unsigned long)cfg.hold[15]);
        }
    }

    if (cfg_ok) {
        /* --- write_config (round-trip, same values) --- */
        {
            esp_err_t e = ld2420_write_config(&cfg);
            ok_err("ld2420_write_config (round-trip)", e);
        }

        /* --- set_gate_range (idempotent) --- */
        {
            esp_err_t e = ld2420_set_gate_range(cfg.min_gate, cfg.max_gate);
            ok_err("ld2420_set_gate_range", e);
        }

        /* --- set_timeout (idempotent) --- */
        {
            esp_err_t e = ld2420_set_timeout(cfg.timeout_s);
            ok_err("ld2420_set_timeout", e);
        }

        /* --- set_gate_sensitivity gate 0 (idempotent) --- */
        {
            esp_err_t e = ld2420_set_gate_sensitivity(0, cfg.trigger[0], cfg.hold[0]);
            ok_err("ld2420_set_gate_sensitivity(0)", e);
        }
    } else {
        ESP_LOGW(TAG, "  SKIP write/set_* — read_config failed (check UART TX→radar RX)");
    }

    /* --- log_config (verbose table via driver) --- */
    {
        esp_err_t e = ld2420_log_config();
        ok_err("ld2420_log_config", e);
    }

    /* --- normal mode + poll sample --- */
    ld2420_ensure_normal_mode();
    vTaskDelay(pdMS_TO_TICKS(200));
    {
        ld2420_state_t st;
        int samples = 0;
        for (int i = 0; i < 20; i++) {
            ld2420_poll(&st);
            vTaskDelay(pdMS_TO_TICKS(50));
            samples++;
        }
        ESP_LOGI(TAG, "  POLL sample (after %d reads): present=%d range_cm=%d health=%d",
                 samples, (int)st.present, st.range_cm, (int)st.health);
        ESP_LOGI(TAG, "       gate_energy[0..3]: %u %u %u %u",
                 (unsigned)st.gate_energy[0], (unsigned)st.gate_energy[1],
                 (unsigned)st.gate_energy[2], (unsigned)st.gate_energy[3]);
    }

#if CONFIG_INSTABOX_LD2420_CMD_TEST_REBOOT
    line("ld2420_reboot (radar module)");
    {
        esp_err_t e = ld2420_reboot();
        ok_err("ld2420_reboot", e);
    }
    ld2420_ensure_normal_mode();
    vTaskDelay(pdMS_TO_TICKS(500));
#endif

    line("LD2420 command suite done");
#endif /* CONFIG_INSTABOX_LD2420_CMD_TEST */
}
