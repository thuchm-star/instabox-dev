#include "unity.h"
#include "../drivers/led.h"

// LED dùng chân BOARD_GPIO_LED (ESP32-S3-DevKitC-1: GPIO 2). Cần board thực hoặc mock.

void setUp(void) {
    // Được gọi trước mỗi test
    led_init();
}

void tearDown(void) {
    // Được gọi sau mỗi test (không dùng tới hiện tại)
}

void test_led_init_and_set(void) {
    // Hiện tại chỉ kiểm tra là hàm được gọi mà không crash.
    // Khi implement thật, có thể đọc lại trạng thái GPIO để assert.
    led_set(true);
    led_set(false);

    TEST_PASS_MESSAGE("LED init/set executed without crash; verify GPIO 2 on board.");
}

