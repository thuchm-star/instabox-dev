## Test Plan – Kiosk Instabox

Tài liệu này mô tả **test case** cho từng tính năng, từng thiết bị phần cứng và từng “model” kiosk/firmware phiên bản. Dùng kèm với `kiosk_deployment_checklist.md`.

---

## 1. Ma trận tính năng ↔ test

| Nhóm tính năng | Mô tả | Tài liệu tham chiếu |
|----------------|-------|----------------------|
| Giám sát | Nhiệt độ/độ ẩm, trạng thái nguồn, cửa mở/tamper, trạng thái máy chính | firmware_architecture.md, mqtt_schema.md |
| Vận hành | Restart thiết bị chính, báo lỗi, heartbeat | firmware_architecture.md, backend_api_spec.md |
| Thu hút khách | Bật LED, phát âm thanh, phát hiện người (radar/PIR) | firmware_architecture.md, mqtt_schema.md |

---

## 2. Test per-device (hardware)

### 2.1. Radar LD2420 (human presence)

- **TC-RADAR-01 – Phát hiện người ở khoảng cách gần**
  - Setup: kiosk trong phòng, 1 người đi vào vùng quét (~1–2m).
  - Step:
    1. Đứng ngoài vùng quét 5s.
    2. Đi chậm vào vùng quét.
  - Expected:
    - LED bật trong vòng ≤ 500 ms sau khi bước vào vùng.
    - MQTT event `person_detected` được gửi (kiểm tra log backend).

- **TC-RADAR-02 – Không báo khi không có người**
  - Setup: không có người trong vùng, không có vật di chuyển.
  - Step: quan sát 2 phút.
  - Expected:
    - Không có event `person_detected` mới.

### 2.2. PIR (nếu bật)

- **TC-PIR-01 – Motion detected backup**
  - Disable radar tạm thời (hoặc che lại), di chuyển trước PIR.
  - Expected:
    - Hệ thống vẫn bật LED (nếu cấu hình cho phép dùng PIR) và gửi `person_detected` (sensor = `pir` nếu có phân biệt).

### 2.3. Reed switch (door sensor)

- **TC-REED-01 – Cửa mở**
  - Step:
    1. Đóng cửa 5s.
    2. Mở cửa.
  - Expected:
    - MQTT event `door_opened` được gửi với `sensor = reed_switch`.

- **TC-REED-02 – Cửa đóng**
  - Từ trạng thái mở, đóng cửa lại.
  - Expected:
    - MQTT event `door_closed` được gửi.

### 2.4. SHTC3 (nhiệt độ/độ ẩm)

- **TC-SHTC3-01 – Giá trị hợp lý**
  - Step: đọc metrics từ backend trong 5 phút.
  - Expected:
    - `temperature` trong khoảng 0–60°C.
    - `humidity` trong khoảng 10–90%.

### 2.5. LED

- **TC-LED-01 – Bật/tắt theo người**
  - Step: đi vào vùng radar để kích hoạt, sau đó rời đi.
  - Expected:
    - LED bật khi phát hiện người.
    - Tắt sau khoảng `led_timeout_sec` (ví dụ 60s) không có người mới.

- **TC-LED-02 – Lệnh từ backend**
  - Gửi lệnh `led_on` và `led_off` qua API/backend.
  - Expected:
    - LED thay đổi đúng theo lệnh.
    - Có `command_ack` với status `ok`.

### 2.6. Audio (MAX98357A)

- **TC-AUDIO-01 – Phát âm khi có người**
  - Step: đi vào vùng radar.
  - Expected:
    - Âm thanh attention phát ra 1 lần, không bị lặp liên tục khi đứng yên.

- **TC-AUDIO-02 – Lệnh play_sound**
  - Gửi lệnh `play_sound` với `sound_id = attention`.
  - Expected:
    - Kiosk phát đúng âm thanh; `command_ack` trả về `ok`.

---

## 3. Test per-feature (firmware features)

### 3.1. Heartbeat & status

- **TC-HB-01 – Heartbeat định kỳ**
  - Expected:
    - Mỗi ~30s có message `status` với `online=true`, `uptime` tăng, `time_synced` đúng.

- **TC-HB-02 – LWT khi mất nguồn đột ngột**
  - Ngắt nguồn ESP32 mà không disconnect clean.
  - Expected:
    - Broker gửi LWT `status` với `online=false, reason=connection_lost`.
    - Backend đánh dấu device OFFLINE (< 3 phút).

### 3.2. Offline buffer

- **TC-OFFLINE-01 – Lưu & gửi lại**
  - Ngắt WiFi trong 2–5 phút, đi qua trước kiosk vài lần.
  - Sau đó bật WiFi lại.
  - Expected:
    - Event `person_detected` được gửi lên đủ sau khi có mạng lại (có thể trễ nhưng không mất nhiều).

### 3.3. OTA

- **TC-OTA-01 – Update thành công**
  - Deploy firmware mới với version lớn hơn.
  - Expected:
    - Thiết bị tải, reboot, gửi status với version mới.

- **TC-OTA-02 – Rollback**
  - Đẩy firmware lỗi (có bug khiến task crash).
  - Expected:
    - Thiết bị rollback về firmware cũ sau vài lần boot thất bại (theo cơ chế ESP32 OTA).

---

## 4. Test per-model (kiosk / firmware version)

Ở thời điểm hiện tại có 1 model kiosk chuẩn, nhưng về sau có thể có:

- **Model A**: Kiosk chuẩn (LED + loa + 1 màn hình chính điều khiển bởi máy chính).
- **Model B**: Kiosk nhỏ hơn (chỉ LED, không loa).

Cho mỗi model / firmware version:

- **TC-MODEL-01 – Compatibility matrix**
  - Xác nhận: driver mapping (GPIO, sensor) đúng cho model.
  - Test qua các case ở mục 2 & 3 trên ít nhất 1 device đại diện.

---

## 5. Cách ghi lại kết quả test

- Mỗi test case ghi:
  - **PASS/FAIL**, thời gian, người test, thiết bị (device_id, model, firmware).
  - Nếu FAIL: mô tả rõ điều kiện, log MQTT/backend/serial.
- Nên lưu vào 1 file log (Excel/Notion/Jira) liên kết với mã test case (VD: TC-RADAR-01).

