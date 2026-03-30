/**
 * Test cảm biến SHTC3 (breakout 4 chân: VCC, SCL, SDA, GND).
 * Nối: VCC→3V3, GND→GND, SDA→GPIO8, SCL→GPIO9 (theo board_config.h).
 */
#include "unity.h"
#include "../drivers/shtc3.h"

void test_shtc3_read_returns_bool(void) {
    float temp = 0.0f;
    float hum = 0.0f;

    bool ok = shtc3_read(&temp, &hum);
    TEST_ASSERT_TRUE_MESSAGE(ok, "shtc3_read() phải trả về true (đọc thành công)");

    char msg[96];
    snprintf(msg, sizeof(msg), "SHTC3: T = %.2f C, RH = %.2f %%", temp, hum);
    TEST_MESSAGE(msg);
}
