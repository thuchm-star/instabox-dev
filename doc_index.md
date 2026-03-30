# Instabox Kiosk – Document Index

Tài liệu chuẩn cho team triển khai. Đọc theo thứ tự nếu bắt đầu từ đầu; nếu code từng phần thì dùng bảng dưới.

---

## Bắt đầu nhanh (cho người code)

| Bạn đang làm | Đọc trước | Tham chiếu thêm |
|--------------|-----------|------------------|
| **Firmware (ESP32)** | firmware_architecture.md | mqtt_schema.md (topic + payload), provisioning.md (credentials) |
| **Backend (API + MQTT)** | backend_api_spec.md, mqtt_schema.md | database_schema.md, kiosk_system_architecture.md |
| **Database** | database_schema.md | backend_api_spec.md (ingestion), mqtt_schema.md (payload fields) |
| **Provisioning thiết bị** | provisioning.md | backend_api_spec.md (POST /devices), mqtt_schema.md (auth) |
| **Triển khai & test** | kiosk_deployment_checklist.md | firmware_architecture.md, mqtt_schema.md |
| **Dung lượng / SIM 4G** | data_usage_estimation.md | mqtt_schema.md (traffic), kiosk_system_architecture.md |
| **1 kiosk: ổn định & sparring** | kiosk_1_stability_sparring.md | data_usage_estimation.md, kiosk_deployment_checklist.md, firmware_architecture.md |

---

## Danh sách tài liệu

| File | Nội dung chính |
|------|----------------|
| **kiosk_system_architecture.md** | Tổng quan phần cứng, mạng, backend, OTA, bảo mật, vận hành, runbook. |
| **mqtt_schema.md** | **Nguồn chân lý** MQTT: topic, QoS, LWT, payload status/event/metrics/log/command + command_ack. |
| **firmware_architecture.md** | Task FreeRTOS, GPIO (radar, PIR, Reed, SHTC3, I2S, watchdog), NTP, offline buffer, boot sequence. |
| **backend_api_spec.md** | REST API: devices, commands, events, metrics, analytics, firmware. Ghi chú ingestion MQTT. |
| **database_schema.md** | DDL: devices, events, metrics, firmware_manifests, command_log. |
| **provisioning.md** | Quy trình đăng ký thiết bị và nạp device_id/device_secret lên ESP32. |
| **kiosk_deployment_checklist.md** | Checklist trước khi lắp, trên hiện trường, sau triển khai, bảo trì; test NTP/TLS/offline/OTA. |
| **data_usage_estimation.md** | Ước tính dung lượng khi dùng router SIM 4G phát WiFi: MQTT/NTP/OTA/in ảnh, gợi ý gói data. |
| **kiosk_1_stability_sparring.md** | 1 kiosk: những điều có thể chưa nghĩ tới (điện, 4G, in ảnh, NTP, flash…) và các bind để hoạt động ổn định. |

---

## Single source of truth

- **MQTT (topic + payload):** chỉ **mqtt_schema.md**.
- **GPIO và task firmware:** chỉ **firmware_architecture.md**.
- **API REST:** chỉ **backend_api_spec.md**.
- **Database:** chỉ **database_schema.md**.

Cập nhật đúng file trên khi thay đổi spec để tránh lệch giữa firmware và backend.
