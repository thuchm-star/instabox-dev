#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "drivers/radar_ld2420.h"
#include "console/radar_console.h"

static const char *TAG = "radar_cli";

#define CLI_STACK 4096
/* Below task_radar (5): avoid starving UART poll / logs while waiting for keys. */
#define CLI_PRIO  3
#define CLI_MAX_LINE 128
#define CAL_SAMPLE_MS 100
/*
 * Cal: trig/hold = max_energy + margin (nền phòng trống).
 * Trigger > hold → tạo vùng trễ (hysteresis) theo đơn vị năng lượng gate: năng lượng phải vượt
 * trigger để “vào”, xuống dưới hold mới “ra” (theo FSM module). Khoảng (trig_margin - hold_margin)
 * quá nhỏ → dao động nhiễu dễ làm nhấp nháy; quá lớn → cần tụt năng lượng nhiều mới hết báo.
 */
#define CAL_DEF_TRIG_MARGIN 5000
#define CAL_DEF_HOLD_MARGIN 3000
#define CAL_MIN_TRIG_HOLD_GAP 500 /* tối thiểu (trig_margin - hold_margin) */

static uint32_t s_cal_trigger[LD2420_NUM_GATES];
static uint32_t s_cal_hold[LD2420_NUM_GATES];
static bool s_cal_ready;

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
    printf("  radar cal_thres <seconds> [trig_margin] [hold_margin]\n");
    printf("      (default +%d / +%d, gap %d; ví dụ: cal_thres 30 2500 1400)\n",
           CAL_DEF_TRIG_MARGIN, CAL_DEF_HOLD_MARGIN,
           CAL_DEF_TRIG_MARGIN - CAL_DEF_HOLD_MARGIN);
    printf("  radar apply_thres [start_gate] [end_gate]\n");
    printf("  radar factory_thres\n");
    printf("  radar uart_mode energy|simple\n");
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
           strcmp(s, "cal_thres") == 0 ||
           strcmp(s, "apply_thres") == 0 ||
           strcmp(s, "factory_thres") == 0 ||
           strcmp(s, "uart_mode") == 0 ||
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

    if (strcmp(sub, "cal_thres") == 0) {
        if (argc < 3 || argc > 5) {
            printf("usage: radar cal_thres <seconds> [trig_margin] [hold_margin]\n");
            return;
        }
        int sec = atoi(argv[2]);
        int trig_margin = (argc >= 4) ? atoi(argv[3]) : CAL_DEF_TRIG_MARGIN;
        int hold_margin = (argc >= 5) ? atoi(argv[4]) : CAL_DEF_HOLD_MARGIN;
        if (sec <= 0 || sec > 120) {
            printf("seconds must be 1..120\n");
            return;
        }
        if (trig_margin < 0 || hold_margin < 0) {
            printf("margins must be >= 0\n");
            return;
        }
        if (hold_margin > trig_margin) {
            printf("hold_margin must be <= trig_margin\n");
            return;
        }
        if (trig_margin - hold_margin < CAL_MIN_TRIG_HOLD_GAP) {
            printf("trig_margin - hold_margin must be >= %d (hysteresis; too close = noisy flip)\n",
                   CAL_MIN_TRIG_HOLD_GAP);
            return;
        }

        bool uart_energy_for_cal = false;
        if (ld2420_set_uart_output_mode(LD2420_UART_OUT_ENERGY) == ESP_OK) {
            uart_energy_for_cal = true;
            printf("UART -> energy mode (per-gate samples)\n");
        } else {
            printf("UART -> energy mode failed (cmd 0x0012); sampling current stream only\n");
        }
        vTaskDelay(pdMS_TO_TICKS(450));

        uint16_t max_energy[LD2420_NUM_GATES] = {0};
        TickType_t t_end = xTaskGetTickCount() + pdMS_TO_TICKS(sec * 1000);
        printf("Calibrating for %d s... keep area empty (no motion)\n", sec);
        while ((int32_t)(t_end - xTaskGetTickCount()) > 0) {
            ld2420_state_t st;
            ld2420_get_state(&st);
            for (int g = 0; g < LD2420_NUM_GATES; g++) {
                if (st.gate_energy[g] > max_energy[g]) {
                    max_energy[g] = st.gate_energy[g];
                }
            }
            vTaskDelay(pdMS_TO_TICKS(CAL_SAMPLE_MS));
        }

        if (uart_energy_for_cal) {
            if (ld2420_set_uart_output_mode(LD2420_UART_OUT_SIMPLE) != ESP_OK) {
                printf("WARN: restore text UART failed — run: radar uart_mode simple\n");
            } else {
                printf("UART -> simple (text) mode restored\n");
            }
            ld2420_ensure_normal_mode();
        }

        bool any = false;
        for (int g = 0; g < LD2420_NUM_GATES; g++) {
            if (max_energy[g] != 0) {
                any = true;
                break;
            }
        }
        if (!any) {
            printf(
                "No gate energy seen (all max=0). Causes:\n"
                "  - Room not empty: radar still reports Range/ON (clear people/objects).\n"
                "  - Firmware rejected UART energy mode (0x0012) or no F4.. frames after switch.\n");
            s_cal_ready = false;
            printf("Calibration NOT stored — fix conditions and run cal_thres again.\n");
            return;
        }

        /* Gates with no samples (e.g. text Range only in gate 0): keep current module
         * thresholds — do not use 0+margin (600/300) which is far too sensitive. */
        ld2420_config_t cur;
        esp_err_t rd = ld2420_read_config(&cur);
        int kept = 0;
        for (int g = 0; g < LD2420_NUM_GATES; g++) {
            if (max_energy[g] > 0) {
                s_cal_trigger[g] = (uint32_t)max_energy[g] + (uint32_t)trig_margin;
                s_cal_hold[g] = (uint32_t)max_energy[g] + (uint32_t)hold_margin;
            } else if (rd == ESP_OK) {
                s_cal_trigger[g] = cur.trigger[g];
                s_cal_hold[g] = cur.hold[g];
                kept++;
            } else {
                s_cal_trigger[g] = 20000U;
                s_cal_hold[g] = 10000U;
                kept++;
            }
        }
        if (kept > 0) {
            printf(
                "%d gate(s) had no energy samples — %s.\n", kept,
                rd == ESP_OK ? "keeping existing radar trigger/hold for those"
                             : "using safe defaults (read_config failed)");
        }

        s_cal_ready = true;
        printf("Calibration done:\n");
        for (int g = 0; g < LD2420_NUM_GATES; g++) {
            printf("  gate %2d: max_energy=%5u -> trig=%lu hold=%lu\n",
                   g, (unsigned)max_energy[g],
                   (unsigned long)s_cal_trigger[g],
                   (unsigned long)s_cal_hold[g]);
        }
        printf("Run: radar apply_thres   (or radar apply_thres <start> <end>)\n");
        return;
    }

    if (strcmp(sub, "apply_thres") == 0) {
        if (!s_cal_ready) {
            printf("No calibrated thresholds. Run: radar cal_thres <seconds>\n");
            return;
        }
        int g0 = 0;
        int g1 = LD2420_NUM_GATES - 1;
        if (argc == 4) {
            g0 = atoi(argv[2]);
            g1 = atoi(argv[3]);
        } else if (argc != 2) {
            printf("usage: radar apply_thres [start_gate] [end_gate]\n");
            return;
        }
        if (g0 < 0 || g1 >= LD2420_NUM_GATES || g0 > g1) {
            printf("gate range invalid (0..15)\n");
            return;
        }

        /* Fast path: batch write via full-config snapshot (single command session). */
        ld2420_config_t cfg;
        esp_err_t e = ld2420_read_config(&cfg);
        if (e == ESP_OK) {
            for (int g = g0; g <= g1; g++) {
                uint32_t tr = s_cal_trigger[g] > 65535U ? 65535U : s_cal_trigger[g];
                uint32_t ho = s_cal_hold[g] > 65535U ? 65535U : s_cal_hold[g];
                cfg.trigger[g] = tr;
                cfg.hold[g] = ho;
            }
            e = ld2420_write_config(&cfg);
            if (e == ESP_OK) {
                printf("apply_thres gates %d..%d: %s (batch)\n", g0, g1, esp_err_to_name(e));
                return;
            }
            printf("apply_thres batch failed: %s, fallback to per-gate...\n", esp_err_to_name(e));
        } else {
            printf("read_config before batch failed: %s, fallback to per-gate...\n", esp_err_to_name(e));
        }

        /* Fallback path: write gate-by-gate for robustness and pinpoint failures. */
        for (int g = g0; g <= g1; g++) {
            uint32_t tr = s_cal_trigger[g] > 65535U ? 65535U : s_cal_trigger[g];
            uint32_t ho = s_cal_hold[g] > 65535U ? 65535U : s_cal_hold[g];
            e = ld2420_set_gate_sensitivity((uint8_t)g, tr, ho);
            if (e != ESP_OK) {
                printf("apply_thres fallback failed at gate %d: %s\n", g, esp_err_to_name(e));
                return;
            }
        }
        printf("apply_thres gates %d..%d: %s (fallback)\n", g0, g1, esp_err_to_name(ESP_OK));
        return;
    }

    if (strcmp(sub, "factory_thres") == 0) {
        static const uint32_t factory_trig[LD2420_NUM_GATES] = {
            65535, 65535, 65535, 65535, 65535, 65535,
            45000, 30000, 20000, 15000, 10000, 8000,
            5000,  3000,  2000,  1000
        };
        static const uint32_t factory_hold[LD2420_NUM_GATES] = {
            65535, 65535, 65535, 65535, 65535, 65535,
            30000, 20000, 15000, 10000, 8000,  5000,
            3000,  2000,  1000,  500
        };
        printf("Restoring factory-like thresholds...\n");
        int ok_cnt = 0;
        for (int g = 0; g < LD2420_NUM_GATES; g++) {
            esp_err_t e = ld2420_set_gate_sensitivity(
                (uint8_t)g, factory_trig[g], factory_hold[g]);
            if (e == ESP_OK) {
                ok_cnt++;
            } else {
                printf("  gate %d failed: %s\n", g, esp_err_to_name(e));
            }
        }
        printf("factory_thres: %d/%d gates OK\n", ok_cnt, LD2420_NUM_GATES);
        return;
    }

    if (strcmp(sub, "uart_mode") == 0) {
        if (argc != 3) {
            printf("usage: radar uart_mode energy|simple\n");
            return;
        }
        uint16_t m;
        if (strcmp(argv[2], "energy") == 0) {
            m = LD2420_UART_OUT_ENERGY;
        } else if (strcmp(argv[2], "simple") == 0 || strcmp(argv[2], "text") == 0) {
            m = LD2420_UART_OUT_SIMPLE;
        } else {
            printf("second arg: energy | simple (or text)\n");
            return;
        }
        esp_err_t e = ld2420_set_uart_output_mode(m);
        printf("uart_mode: %s\n", esp_err_to_name(e));
        if (e == ESP_OK) {
            ld2420_ensure_normal_mode();
        }
        return;
    }

    if (strcmp(sub, "poll") == 0) {
        ld2420_state_t st;
        ld2420_get_state(&st);
        printf("present=%d range_cm=%d health=%d\n",
               (int)st.present, st.range_cm, (int)st.health);
        for (int g = 0; g < LD2420_NUM_GATES; g += 4) {
            printf("energy[%d..%d]: %u %u %u %u\n", g, g + 3,
                   (unsigned)st.gate_energy[g],   (unsigned)st.gate_energy[g+1],
                   (unsigned)st.gate_energy[g+2], (unsigned)st.gate_energy[g+3]);
        }
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

    /* Blocking getchar() can wedge VFS/console with logging on the same UART. */
    int in = fileno(stdin);
    if (in >= 0) {
        int fl = fcntl(in, F_GETFL, 0);
        if (fl >= 0) {
            fcntl(in, F_SETFL, fl | O_NONBLOCK);
        }
    }

    print_help();
    printf("Type command then ENTER. Example: radar read\n");
    fflush(stdout);

    for (;;) {
        unsigned char cbyte;
        int n = (in >= 0) ? (int)read(in, &cbyte, 1) : -1;
        int ch;
        if (n == 1) {
            ch = (int)cbyte;
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        } else {
            vTaskDelay(pdMS_TO_TICKS(50));
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
