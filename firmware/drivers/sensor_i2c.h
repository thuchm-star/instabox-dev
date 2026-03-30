/**
 * I2C bus dùng chung cho cảm biến (SHT30, SHTC3, ...).
 * Chỉ cài driver một lần; nhiều driver gọi init() an toàn.
 */
#pragma once

#include <stdbool.h>

/** Khởi tạo I2C master (BOARD_I2C_NUM, SDA/SCL từ board_config). No-op nếu đã gọi. */
void sensor_i2c_init(void);

/** Trả về true nếu bus đã sẵn sàng. */
bool sensor_i2c_ready(void);
