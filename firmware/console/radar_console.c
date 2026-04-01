#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "drivers/radar_ld2420.h"
#include "console/radar_console.h"

static const char *TAG = "radar_cli";

#define CLI_STACK 4096
#define CLI_PRIO  4
#define CLI_MAX_LINE 128

static void print_help(void)
{
    printf("\nRadar commands:\n");
    printf("  radar help\n");
    printf("  radar version\n");
    printf("  radar read\n");
    printf("  radar log\n");
    printf("  radar range <min 0-15> <max 0-15>\n");
    printf("  radar timeout <seconds>\n");
    printf("  radar thresh <gate 0-15> <trigger> <hold>\n");
    printf("  radar poll\n");
    printf("  radar normal\n");
    printf("  radar reboot\n\n");
}

static int split(char *line, char **argv, int max_args)
{
    int argc = 0;
    char *t = strtok(line, " \t");
    while (t && argc < max_args) {
        argv[argc++] = t;
        t = strtok(NULL, " \t");
    }
    return argc;
}

static bool is_subcmd(const char *s)
{
    return strcmp(s, "help") == 0    ||
           strcmp(s, "version") == 0 ||
           strcmp(s, "read") == 0    ||
           strcmp(s, "log") == 0     ||
           strcmp(s, "range") == 0   ||
           strcmp(s, "timeout") == 0 ||
           strcmp(s, "thresh") == 0  ||
           strcmp(s, "poll") == 0    ||
           strcmp(s, "normal") == 0  ||
           strcmp(s, "reboot") == 0;
}

static void exec_radar(int argc, char **argv)
{
    if (argc < 2 || strcmp(argv[1], "help") == 0) {
        print_help();
        return;
    }

    const char *sub = argv[1];
    if (strcmp(sub, "version") == 0) {
        char ver[48];
        esp_err_t e = ld2420_read_version(ver, sizeof(ver));
        printf("read_version: %s\n", esp_err_to_name(e));
        if (e == ESP_OK) printf("firmware: %s\n", ver);
        return;
    }

    if (strcmp(sub, "read") == 0) {
        ld2420_config_t cfg;
        esp_err_t e = ld2420_read_config(&cfg);
        printf("read_config: %s\n", esp_err_to_name(e));
        if (e == ESP_OK) {
            printf("min_gate=%lu max_gate=%lu timeout_s=%lu\n",
                   (unsigned long)cfg.min_gate,
                   (unsigned long)cfg.max_gate,
                   (unsigned long)cfg.timeout_s);
            printf("gate00 trig=%lu hold=%lu | gate15 trig=%lu hold=%lu\n",
                   (unsigned long)cfg.trigger[0], (unsigned long)cfg.hold[0],
                   (unsigned long)cfg.trigger[15], (unsigned long)cfg.hold[15]);
        }
        return;
    }

    if (strcmp(sub, "log") == 0) {
        esp_err_t e = ld2420_log_config();
        printf("log_config: %s\n", esp_err_to_name(e));
        return;
    }

    if (strcmp(sub, "range") == 0) {
        if (argc != 4) {
            printf("usage: radar range <min 0-15> <max 0-15>\n");
            return;
        }
        esp_err_t e = ld2420_set_gate_range((uint32_t)atoi(argv[2]),
                                            (uint32_t)atoi(argv[3]));
        printf("set_gate_range: %s\n", esp_err_to_name(e));
        return;
    }

    if (strcmp(sub, "timeout") == 0) {
        if (argc != 3) {
            printf("usage: radar timeout <seconds>\n");
            return;
        }
        esp_err_t e = ld2420_set_timeout((uint32_t)atoi(argv[2]));
        printf("set_timeout: %s\n", esp_err_to_name(e));
        return;
    }

    if (strcmp(sub, "thresh") == 0) {
        if (argc != 5) {
            printf("usage: radar thresh <gate 0-15> <trigger> <hold>\n");
            return;
        }
        int gate = atoi(argv[2]);
        if (gate < 0 || gate >= LD2420_NUM_GATES) {
            printf("gate must be 0..15\n");
            return;
        }
        esp_err_t e = ld2420_set_gate_sensitivity((uint8_t)gate,
                                                   (uint32_t)atoi(argv[3]),
                                                   (uint32_t)atoi(argv[4]));
        printf("set_gate_sensitivity: %s\n", esp_err_to_name(e));
        return;
    }

    if (strcmp(sub, "poll") == 0) {
        ld2420_state_t st;
        ld2420_poll(&st);
        printf("present=%d range_cm=%d health=%d\n",
               (int)st.present, st.range_cm, (int)st.health);
        printf("energy[0..3]: %u %u %u %u\n",
               (unsigned)st.gate_energy[0], (unsigned)st.gate_energy[1],
               (unsigned)st.gate_energy[2], (unsigned)st.gate_energy[3]);
        return;
    }

    if (strcmp(sub, "normal") == 0) {
        ld2420_ensure_normal_mode();
        printf("normal mode done\n");
        return;
    }

    if (strcmp(sub, "reboot") == 0) {
        esp_err_t e = ld2420_reboot();
        printf("reboot: %s\n", esp_err_to_name(e));
        return;
    }

    print_help();
}

static void radar_cli_task(void *arg)
{
    (void)arg;
    char line[CLI_MAX_LINE];
    int pos = 0;

    print_help();
    printf("Type command then ENTER. Example: radar read\n");

    for (;;) {
        int ch = getchar();
        if (ch == EOF) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (ch == '\r' || ch == '\n') {
            putchar('\r');
            putchar('\n');
            fflush(stdout);
            if (pos == 0) continue;
            line[pos] = '\0';
            pos = 0;

            char *argv[8];
            int argc = split(line, argv, 8);
            if (argc <= 0) continue;
            if (strcmp(argv[0], "radar") == 0) {
                exec_radar(argc, argv);
                continue;
            }
            if (is_subcmd(argv[0])) {
                char *wrap[9];
                wrap[0] = "radar";
                for (int i = 0; i < argc && i < 8; i++) wrap[i + 1] = argv[i];
                exec_radar(argc + 1, wrap);
            }
            continue;
        }

        if (ch == 0x08 || ch == 0x7F) {
            if (pos > 0) {
                pos--;
                putchar('\b'); putchar(' '); putchar('\b');
                fflush(stdout);
            }
            continue;
        }

        if (ch < 0x20 || ch > 0x7E) continue;
        if (pos < CLI_MAX_LINE - 1) {
            line[pos++] = (char)ch;
            putchar(ch);
            fflush(stdout);
        }
    }
}

esp_err_t radar_console_start(void)
{
#if !CONFIG_INSTABOX_RADAR_CONSOLE
    return ESP_OK;
#else
    static bool started;
    if (started) return ESP_OK;
    started = true;

    if (xTaskCreate(radar_cli_task, "radar_cli", CLI_STACK, NULL, CLI_PRIO, NULL) != pdPASS) {
        ESP_LOGE(TAG, "cannot create CLI task");
        return ESP_FAIL;
    }
    ESP_LOGW(TAG, "Radar CLI ready on monitor UART");
    return ESP_OK;
#endif
}
