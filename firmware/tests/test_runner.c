// Test runner: SHT30 + SHTC3.
// Hiện chỉ chạy test SHTC3 (đang chỉ cắm SHTC3). Bỏ comment RUN_TEST(test_sht30_...) khi cắm SHT30.
// Chạy: cd firmware && idf.py build flash monitor

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "unity.h"
#include "../drivers/sht30.h"
#include "../drivers/shtc3.h"

void test_sht30_read_returns_bool(void);
void test_shtc3_read_returns_bool(void);

void setUp(void) {
    sht30_init();
    shtc3_init();
}

void tearDown(void) {
}

static void run_all_tests(void) {
    UNITY_BEGIN();
    // RUN_TEST(test_sht30_read_returns_bool);   /* Bật khi đã cắm SHT30 */
    RUN_TEST(test_shtc3_read_returns_bool);
    UNITY_END();
}

void app_main(void) {
    run_all_tests();
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
