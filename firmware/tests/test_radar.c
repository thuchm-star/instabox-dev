#include "unity.h"
#include "../drivers/radar_ld2420.h"
#include "../drivers/pir.h"

void setUp(void) {
    ld2420_init();
    pir_init();
}

void tearDown(void) {
}

void test_radar_and_pir_api_compiles(void) {
    bool radar_person = ld2420_gpio_present();
    bool pir_motion = pir_motion_detected();
    (void)radar_person;
    (void)pir_motion;

    TEST_PASS_MESSAGE("ld2420_gpio_present() va pir_motion_detected() chay OK");
}
