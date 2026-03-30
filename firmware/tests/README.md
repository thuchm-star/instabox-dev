## Unit test cho firmware ESP32 (drivers)

Mục tiêu: có **code test** cho từng driver/hardware, chạy bằng Unity (framework test của ESP-IDF).

### Cấu trúc

- `tests/test_led.c` – test LED driver (GPIO 2).
- `tests/test_reed_switch.c` – test reed switch (cửa).
- `tests/test_radar.c` – test radar/PIR integration (mức logic).
- `tests/test_sht30.c` – test đọc nhiệt/ẩm (SHTC3).
- `tests/test_audio.c` – test gọi hàm `audio_i2s_play_attention()`.

### Cách chạy test (gợi ý – inline trong app hiện tại)

Vì bạn đã chọn chế độ **inline** (dùng app hiện tại làm test-runner), quy trình gợi ý:

1. **Tạo file test runner** (ví dụ `main/test_runner.c`) trong 1 branch riêng:

   ```c
   #include "unity.h"

   void test_led_init_and_set(void);
   void test_reed_switch_api_compiles(void);
   void test_radar_and_pir_api_compiles(void);
   void test_shtc3_read_returns_bool(void);
   void test_audio_play_attention(void);

   void app_main(void) {
       UNITY_BEGIN();
       RUN_TEST(test_led_init_and_set);
       RUN_TEST(test_reed_switch_api_compiles);
       RUN_TEST(test_radar_and_pir_api_compiles);
       RUN_TEST(test_shtc3_read_returns_bool);
       RUN_TEST(test_audio_play_attention);
       UNITY_END();
       while (1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
   }
   ```

   - Tạm thời **đổi tên** `app_main` hiện tại trong `main.c` (hoặc exclude file) để tránh trùng symbol.

2. **Build & flash bằng ESP-IDF**:

   - Trong thư mục `firmware/`:
     - Chạy `idf.py set-target esp32s3` (nếu dùng ESP32-S3).
     - Chạy `idf.py build flash monitor`.
   - Quan sát log serial: Unity sẽ in PASS/FAIL cho từng test.

3. **Liên kết với test manual**:

   - Những test này chủ yếu kiểm tra **API không crash / build OK**.
   - Để xác nhận hành vi thực tế:
     - LED: nhìn LED trên GPIO 2 (TC-LED-01/02).
     - Reed switch: mở/đóng cửa, xem event MQTT (TC-REED-01/02).
     - Radar/PIR: di chuyển trước kiosk, xem event `person_detected` (TC-RADAR-01/02, TC-PIR-01).
     - SHTC3: xem metrics trên backend (TC-SHTC3-01).
     - Audio: nghe loa (TC-AUDIO-01/02).

Các file `test_*.c` hiện tại minh họa cách dùng Unity; khi driver hoàn thiện, bạn có thể thêm assert chi tiết (ví dụ đọc lại GPIO level, mock hàm đọc UART/I2C) để biến chúng thành unit test thực sự.

