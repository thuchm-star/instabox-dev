#include "unity.h"
#include "../drivers/sht30.h"

// Test driver SHT30 – đọc và in nhiệt độ/độ ẩm.

void test_sht30_read_returns_bool(void) {
    float temp = 0.0f;
    float hum = 0.0f;

    bool ok = sht30_read(&temp, &hum);
    TEST_ASSERT_TRUE_MESSAGE(ok, "sht30_read() phải trả về true (đọc thành công)");

    char msg[96];
    snprintf(msg, sizeof(msg), "SHT30: T = %.2f C, RH = %.2f %%", temp, hum);
    TEST_MESSAGE(msg);
}

