/**
 * Pin mapping for ESP32-S3-DevKitC-1 (Instabox kiosk firmware).
 *
 * Chân an toàn: tránh strapping (0, 3, 45, 46), USB (43/44 UART, 19/20 native USB),
 * và trên một số board octal flash/PSRAM thì 35–37 bị chiếm.
 * Tham khảo: ESP32-S3-DevKitC-1 pinout, Espressif docs.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- LED (output) --- */
#define BOARD_GPIO_LED              2

/* --- Reed switch - cửa (input, pull-up; LOW = đóng, HIGH = mở) --- */
#define BOARD_GPIO_REED_SWITCH      4

/* --- PIR motion (input) --- */
#define BOARD_GPIO_PIR              5

/* --- I2C: SHT30 / SHTC3 (SDA, SCL). 50 kHz dễ chịu hơn khi không có pull-up ngoài. --- */
#define BOARD_GPIO_I2C_SDA          8
#define BOARD_GPIO_I2C_SCL           9
#define BOARD_I2C_NUM                0
#define BOARD_I2C_FREQ_HZ            50000

/* Hiệu chỉnh SHTC3 nếu chênh với nhiệt kế tham chiếu (tự đốt/vị trí). Đặt 0.0f nếu không dùng. */
#define SHTC3_TEMP_OFFSET_C          2.0f   /* SHTC3 đọc thấp hơn 2 độ → cộng 2 */
#define SHTC3_RH_OFFSET_PCT          0.0f

/* --- I2S: MAX98357A / audio (BCLK, LRCK, DOUT) --- */
#define BOARD_GPIO_I2S_BCLK         10
#define BOARD_GPIO_I2S_LRCK         11
#define BOARD_GPIO_I2S_DOUT         12
#define BOARD_I2S_NUM               0

/* --- LD2420 radar: UART (TX, RX) + chân OUT báo có người (HIGH = có người) --- */
#define BOARD_GPIO_UART2_TX         17
#define BOARD_GPIO_UART2_RX         18
#define BOARD_GPIO_LD2420_PRESENCE  6   /* Nối chân OUT/OT2 của LD2420 */
#define BOARD_UART2_NUM             2
#define BOARD_UART2_BAUD            256000

/* --- UART1: gửi JSON lên RPi5 (TX only, 115200 baud) --- */
#define BOARD_GPIO_UART1_TX         15
#define BOARD_GPIO_UART1_RX         16   /* không dùng nhưng khai báo sẵn */
#define BOARD_UART1_NUM             1
#define BOARD_UART1_BAUD            115200

/* --- Watchdog ngoại (output, toggle để feed) --- */
#define BOARD_GPIO_WATCHDOG         38

#ifdef __cplusplus
}
#endif
