#include "unity.h"
#include "../drivers/audio_i2s.h"

// Test compile/time cho audio; tín hiệu analog cần nghe trực tiếp trên loa.

void setUp(void) {
    audio_i2s_init();
}

void tearDown(void) {
}

void test_audio_play_attention(void) {
    audio_i2s_play_attention();
    TEST_PASS_MESSAGE("audio_i2s_play_attention() chạy; hãy nghe loa để xác nhận âm thanh.");
}

