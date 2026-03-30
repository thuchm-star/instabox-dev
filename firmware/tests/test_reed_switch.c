#include "unity.h"
#include "../drivers/reed_switch.h"

// Test ở mức compile + logic. Để kiểm tra thật, hãy quan sát trạng thái door_open/closed
// qua MQTT event khi mở/đóng cửa (TC-REED-01/02 trong TEST_PLAN.md).

void setUp(void) {
    reed_switch_init();
}

void tearDown(void) {
}

void test_reed_switch_api_compiles(void) {
    // Gọi hàm để chắc chắn không crash; hành vi thật phụ thuộc hardware.
    bool open = reed_switch_is_open();
    (void)open;

    TEST_PASS_MESSAGE("reed_switch_init/is_open chạy OK; kiểm tra trạng thái cửa qua event MQTT trên board thật.");
}

