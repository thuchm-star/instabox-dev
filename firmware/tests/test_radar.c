#include "unity.h"
#include "../drivers/radar_ld2420.h"
#include "../drivers/pir.h"

// Test mức logic cho radar/PIR: đảm bảo API tồn tại và có thể được gọi.
// Hành vi phát hiện người thực tế cần test theo TC-RADAR-01/02 và TC-PIR-01.

void setUp(void) {
    radar_ld2420_init();
    pir_init();
}

void tearDown(void) {
}

void test_radar_and_pir_api_compiles(void) {
    bool radar_person = radar_ld2420_person_present();
    bool pir_motion = pir_motion_detected();
    (void)radar_person;
    (void)pir_motion;

    TEST_PASS_MESSAGE("radar_ld2420_person_present() và pir_motion_detected() chạy OK; test phát hiện người trên board theo TEST_PLAN.");
}

