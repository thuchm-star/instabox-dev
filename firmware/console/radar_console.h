#pragma once

#include "esp_err.h"

/* Minimal runtime CLI on monitor UART.
 * Type commands in idf.py monitor, for example:
 *   radar read
 *   radar timeout 10
 *   radar range 0 8
 *   radar uart_mode energy | simple */
esp_err_t radar_console_start(void);
