## Kế hoạch triển khai hệ thống kiosk Instabox

Tài liệu này chia công việc thành các giai đoạn rõ ràng, có thể giao task cho từng người. Tham chiếu chi tiết: `kiosk_system_architecture.md`, `firmware_architecture.md`, `backend_api_spec.md`, `database_schema.md`, `mqtt_schema.md`, `provisioning.md`, `kiosk_deployment_checklist.md`.

---

## Giai đoạn 0 – Chốt yêu cầu & ranh giới

- **0.1. Chốt vai trò ESP32**
  - [ ] Ghi rõ trong tài liệu (1–2 đoạn): ESP32 là lớp giám sát + vận hành + thu hút (LED/âm thanh), không chứa core business logic.
- **0.2. Chốt phạm vi MVP**
  - [ ] Thống nhất với business: chức năng bắt buộc trong đợt 1 (heartbeat, event, metrics, command cơ bản, OTA cơ bản).
- **0.3. Chốt môi trường triển khai**
  - [ ] Quyết định hạ tầng: server/bare metal/cloud, domain, chứng chỉ TLS.

---

## Giai đoạn 1 – Chuẩn bị repo & môi trường dev

- **1.1. Repo & cấu trúc thư mục**
  - [ ] Tạo repo (hoặc monorepo) chứa:
    - `firmware/` – mã ESP32.
    - `backend/` – API + MQTT ingestion.
    - Các file `.md` kiến trúc hiện có ở thư mục gốc.
- **1.2. Môi trường dev**
  - [ ] Docker/compose cho:
    - MQTT broker (EMQX/Mosquitto).
    - PostgreSQL (+ TimescaleDB nếu dùng).
    - Backend (FastAPI/NestJS).
- **1.3. CI cơ bản**
  - [ ] Thiết lập pipeline:
    - Build + lint firmware.
    - Lint + test backend.

---

## Giai đoạn 2 – Firmware ESP32

Tham chiếu: `firmware_architecture.md`.

- **2.1. Skeleton project**
  - [x] Khởi tạo project ESP-IDF (hoặc Arduino-ESP32). *(Đã tạo thư mục `firmware/` với `CMakeLists.txt`, `main/` và `main.c` skeleton dùng FreeRTOS tasks.)*
  - [x] Tạo module: sensors, network, mqtt, ota, led, audio, storage/offline, tasks. *(Đã tạo thư mục `drivers/`, `net/`, `storage/` với các file stub: radar_ld2420.c, pir.c, reed_switch.c, sht30.c, led.c, audio_i2s.c, wifi.c, ntp.c, mqtt_client.c, nvs_config.c, offline_buffer.c.)*
- **2.2. Driver phần cứng**
  - [x] LD2420 (UART2) – đọc trạng thái hiện diện người. *(Đã tạo driver `radar_ld2420.c/.h` và gọi trong `hardware_init()` + `sensor_task()` để phát hiện EVENT_PERSON_DETECTED dựa trên radar/PIR.)*
  - [x] PIR (nếu dùng) – GPIO. *(Stub driver `pir.c/.h` và sử dụng như nguồn phụ trong `sensor_task()`.)*
  - [x] Reed switch – GPIO với debounce. *(Stub driver `reed_switch.c/.h`, được dùng để phát EVENT_DOOR_OPENED/CLOSED trong `sensor_task()`.)*
  - [x] SHTC3 – I2C (SDA=21, SCL=19). *(Stub driver `shtc3.c/.h`, đã khởi tạo trong `hardware_init()` – sẽ được dùng ở phần metrics sau.)*
  - [x] LED – GPIO 2. *(Driver `led.c/.h`, được gọi trong `hardware_init()` và `led_control_task()` để bật/tắt LED theo led_timeout_sec.)*
  - [x] MAX98357A – I2S. *(Driver `audio_i2s.c/.h`, đã khởi tạo trong `hardware_init()` – chờ implement `audio_task()` chi tiết.)*
- **2.3. Task & event flow**
  - [ ] Implement `sensor_task` (phát hiện `person_present`, door_open/close).
  - [ ] Implement `led_control_task` (bật/tắt LED theo person_detected, dùng `led_timeout_sec`).
  - [ ] Implement `audio_task` (play `sound_id=attention`).
- **2.4. Network, NTP, MQTT**
  - [ ] `network_task`: WiFi connect + NTP sync.
  - [ ] `mqtt_task`: publish status/event/metrics, subscribe command, gửi command_ack theo `mqtt_schema.md`.
- **2.5. Offline buffer & storage**
  - [ ] Cài queue flash (SPIFFS/NVS) cho event khi offline.
  - [ ] Logic gửi lại khi reconnect (batch + QoS 1).
- **2.6. OTA & watchdog**
  - [ ] `ota_task`: đọc manifest `/firmware/manifest`, download, verify SHA256, switch partition, rollback.
  - [ ] `watchdog_task`: feed HW/SW watchdog.
- **2.7. Test trên board**
  - [ ] Test WiFi + MQTT + NTP. *(Xem test case TC-HB-01, TC-OFFLINE-01 trong `TEST_PLAN.md`.)*
  - [ ] Test radar/PIR/Reed/LED/audio. *(Nhóm TC-RADAR, TC-PIR, TC-REED, TC-LED, TC-AUDIO trong `TEST_PLAN.md`.)*
  - [ ] Test offline buffer (ngắt WiFi, cắm lại). *(TC-OFFLINE-01.)*
  - [ ] Test OTA (update, rollback). *(TC-OTA-01, TC-OTA-02.)*

---

## Giai đoạn 3 – Backend & dữ liệu

Tham chiếu: `backend_api_spec.md`, `database_schema.md`, `mqtt_schema.md`.

- **3.1. Thiết kế DB & migration**
  - [ ] Tạo bảng `devices`, `events`, `metrics`, `firmware_manifests`, `command_log`.
  - [ ] Thiết lập TimescaleDB (nếu dùng) và hypertable cho `events`, `metrics`.
- **3.2. Service backend**
  - [ ] Khởi tạo dự án backend (FastAPI/NestJS).
  - [ ] Implement module:
    - Devices (CRUD, revoke).
    - Commands (gửi MQTT).
    - Events/Metrics (read/analytics).
    - Firmware (quản lý version, manifest).
- **3.3. MQTT ingestion**
  - [ ] Service subscribe các topic `kiosk/+/status`, `event`, `metrics`, `command_ack`.
  - [ ] Lưu vào DB đúng schema.
  - [ ] Cập nhật status + last_seen, xử lý LWT (offline).
- **3.4. API theo spec**
  - [ ] /devices, /devices/:id.
  - [ ] /devices/:id/commands/* (reboot, led_on/off, play_sound, update_config).
  - [ ] /devices/:id/events, /metrics, /analytics/summary.
  - [ ] /firmware, /firmware/manifest.
- **3.5. Bảo mật backend**
  - [ ] Auth (JWT/API key) cho dashboard/API.
  - [ ] CORS & rate limit cơ bản.

---

## Giai đoạn 4 – Provisioning & MQTT broker

Tham chiếu: `provisioning.md`.

- **4.1. Tool provision thiết bị**
  - [ ] CLI/script:
    - Gọi `POST /devices` để tạo device_id + device_secret.
    - Ghi credentials vào NVS qua serial hoặc workflow khác.
- **4.2. Cấu hình broker**
  - [ ] Tạo user theo device_id/device_secret.
  - [ ] Thiết lập ACL cho publish/subscribe đúng topic trong `mqtt_schema.md`.
  - [ ] Bật TLS trong môi trường staging/prod.

---

## Giai đoạn 5 – Dashboard (tùy mức độ)

- **5.1. Màn hình thiết bị**
  - [ ] Danh sách thiết bị (status, last_seen, firmware, location).
  - [ ] Detail (chart events/metrics, logs cơ bản).
- **5.2. Điều khiển từ xa**
  - [ ] Giao diện gửi các lệnh: reboot, led_on/off, play_sound, update_config.
- **5.3. OTA**
  - [ ] Màn hình chọn firmware version + rollout theo nhóm.

---

## Giai đoạn 6 – Pilot & rollout

- **6.1. Pilot (5–10 kiosk)**
  - [ ] Dùng `kiosk_deployment_checklist.md` khi lắp đặt.
  - [ ] Theo dõi hoạt động 1–2 tuần (online/offline, event, metrics, OTA).
- **6.2. Điều chỉnh sau pilot**
  - [ ] Tinh chỉnh radar_sensitivity, led_timeout_sec, report_interval_sec.
  - [ ] Cập nhật alert rule (offline, event spike, OTA fail).
- **6.3. Rollout rộng**
  - [ ] Triển khai theo OTA staged: 5 → 20 → toàn bộ.

---

## Giai đoạn 7 – Monitoring & vận hành lâu dài

- **7.1. Monitoring**
  - [ ] Thiết lập Prometheus/Grafana (hoặc tương đương) cho:
    - Số thiết bị online/offline.
    - Event/metrics rate.
    - MQTT & API health.
- **7.2. Alert & runbook**
  - [ ] Định nghĩa cảnh báo: nhiều thiết bị offline, OTA lỗi nhiều, event bất thường.
  - [ ] Ghi runbook chi tiết dựa trên phần runbook trong `kiosk_system_architecture.md`.

---

## Giao việc & theo dõi

- Có thể dùng file này làm checklist trên GitHub issue hoặc project board:
  - Gán từng bullet cho một người/bucket (Firmware / Backend / DevOps / UI).
  - Đánh dấu [x] khi hoàn thành, cập nhật link PR/issue bên cạnh nếu cần.

