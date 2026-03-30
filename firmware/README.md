## Firmware ESP32 – Kiosk Instabox

Skeleton code này bám theo `firmware_architecture.md`. Mục tiêu: có khung FreeRTOS tasks, module tách rõ để team bắt đầu implement.

### Cấu trúc thư mục đề xuất

- `main/`
  - `main.c` – `app_main`, tạo task chính.
  - `tasks_sensor.c`, `tasks_led.c`, `tasks_network_mqtt.c`, `tasks_ota.c`, `tasks_watchdog.c`.
- `drivers/`
  - `radar_ld2420.c`, `pir.c`, `reed_switch.c`, `sht30.c`, `led.c`, `audio_i2s.c`.
- `net/`
  - `wifi.c`, `ntp.c`, `mqtt_client.c`.
- `storage/`
  - `nvs_config.c`, `offline_buffer.c`.

Hiện tại chỉ tạo `main.c` với các task stub; phần còn lại team sẽ bổ sung dần.

