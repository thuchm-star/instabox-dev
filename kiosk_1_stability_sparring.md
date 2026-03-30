# 1 Kiosk: Những điều có thể chưa nghĩ tới & Binds để hoạt động ổn định

Tài liệu **AI Sparring** cho **một kiosk**: gợi ý các rủi ro/gap dễ bỏ sót và **các bind** (điều kiện ràng buộc) để kiosk chạy ổn định. Scope: **chỉ 1 kiosk**.

---

## Phần A. Những điều có thể chưa nghĩ tới

### 1. Điện & phần cứng

| Điểm | Mô tả |
|------|--------|
| **Mất điện ngắn** | Mất điện vài giây → ESP32 reboot, WiFi/MQTT reconnect. Offline buffer giữ event nhưng **status mất 30s**; backend có thể báo offline rồi lại online. Cần chấp nhận hoặc có UPS nhỏ. |
| **Sụt áp** | Nguồn yếu hoặc dây dài → 12V/5V/3.3V sụt khi in ảnh hoặc WiFi TX. Có thể gây reboot hoặc hang. Nên đo điện áp tại kiosk khi tải max. |
| **Watchdog ngoài** | Nếu không có HW watchdog, ESP32 hang (ví dụ do lỗi phần mềm) sẽ không tự khôi phục. Bind: **luôn dùng watchdog ngoài** (GPIO 33) với timeout 5–10s. |

### 2. Router SIM 4G & mạng

| Điểm | Mô tả |
|------|--------|
| **Hết data SIM** | Gói data hết → nhà mạng chặn hoặc throttle → MQTT/HTTPS (in ảnh, OTA) fail. Backend thấy device offline. Cần: cảnh báo sớm (dựa trên ước tính data_usage_estimation.md) hoặc gói data dư. |
| **Throttle sau khi vượt data** | Một số SIM sau khi hết data vẫn cho kết nối nhưng rất chậm → timeout MQTT/OTA. Cần retry và timeout hợp lý ở firmware. |
| **Router 4G restart** | Router reboot (update, mất điện) → WiFi mất vài phút → kiosk offline. Offline buffer giữ event; cần đảm bảo **router ổn định** hoặc chấp nhận gap. |
| **APN / cấu hình SIM** | SIM trả sau/doanh nghiệp có thể cần APN đúng. Sai APN → không lên 4G → kiosk không kết nối. Bind: **kiểm tra APN** trước khi đặt điểm. |
| **Firewall / CGNAT** | Một số mạng chặn outbound 8883 (MQTT) hoặc chỉ cho web (80/443). Cần đảm bảo **port 8883 (và NTP nếu dùng)** được phép ra internet. |

### 3. Nhiệt độ & môi trường

| Điểm | Mô tả |
|------|--------|
| **Quá nóng trong hộp** | SHT30 đã gửi temperature về server nhưng **chưa có ngưỡng cảnh báo** (ví dụ >45°C). Nên: backend alert khi temperature vượt ngưỡng; firmware có thể giảm tải (ví dụ tắt LED/âm thanh) nếu nhiệt cao. |
| **Nắng trực tiếp / bụi** | Radar, ống kính PIR (nếu có) bị bẩn hoặc nắng chiếu → false positive/negative. Nên vị trí lắp tránh nắng trực tiếp; định kỳ vệ sinh. |
| **Độ ẩm cao** | Độ ẩm cao lâu ngày ảnh hưởng board, đầu nối. Có thể thêm ngưỡng humidity trong metrics để theo dõi. |

### 4. In ảnh (nếu có)

| Điểm | Mô tả |
|------|--------|
| **In ảnh khi mất mạng** | User chọn in nhưng 4G/WiFi đang mất → ảnh không tải được. Cần: thông báo "Mất kết nối, thử lại sau" và **không trừ tiền / không tạo job** cho đến khi tải thành công (hoặc policy rõ ràng). |
| **Queue in ảnh** | Nhiều người in cùng lúc → cần hàng đợi và giới hạn (ví dụ tối đa N job, hoặc từ chối khi queue đầy). Tránh tràn bộ nhớ/đĩa. |
| **Dung lượng lưu ảnh tạm** | Ảnh ~1,5 MB/ảnh; nếu lưu tạm trên thiết bị (ESP32 hoặc máy in kết nối), cần giới hạn dung lượng và xóa sau khi in xong. |
| **Lạm dụng in ảnh** | Một người in rất nhiều ảnh → tốn data SIM và giấy. Có thể cần **giới hạn số ảnh/lượt hoặc/ngày** (theo chính sách) và hiển thị rõ cho user. |
| **Timeout tải ảnh** | 4G chậm → tải ảnh lâu. Cần timeout (ví dụ 60–120s) và thông báo "Quá thời gian, thử lại" thay vì treo. |

### 5. Thời gian (NTP)

| Điểm | Mô tả |
|------|--------|
| **NTP chưa sync lâu** | Boot trong môi trường mạng lỗi → NTP fail nhiều lần. `time_synced: false` trong status; **timestamp trong event/metrics có thể sai** (ví dụ dùng RTC nội bộ). Analytics theo thời gian sẽ lệch. Bind: quyết định có gửi event khi chưa sync không; backend có thể bỏ qua hoặc đánh dấu "unsynced". |
| **Lệch múi giờ** | NTP trả UTC; cần timezone (ví dụ Asia/Ho_Chi_Minh) để hiển thị và báo cáo đúng giờ địa phương. |

### 6. Flash & bộ nhớ

| Điểm | Mô tả |
|------|--------|
| **Offline buffer + OTA** | Buffer event ghi flash nhiều lần; OTA cũng ghi partition. Flash có số chu kỳ ghi giới hạn. Cần giới hạn **kích thước và tần suất ghi buffer** (đã có max 500 events); tránh ghi liên tục từng event nhỏ. |
| **Heap** | `heap_free` đã gửi trong status. Backend có thể cảnh báo khi heap_free xuống thấp (ví dụ <50 KB) → nguy cơ crash. |

### 7. Backend & broker

| Điểm | Mô tả |
|------|--------|
| **Broker down** | MQTT broker sập → kiosk mất kết nối, LWT báo offline. Kiosk vẫn chạy, buffer event; khi broker lên lại sẽ reconnect và gửi buffer. Cần **broker HA hoặc SLA** phù hợp. |
| **Backend ingest chậm** | Broker nhận message nhưng backend xử lý chậm hoặc down → event/metrics vẫn nằm broker. Khi backend lên lại cần xử lý đúng (idempotent nếu cần). |
| **Revoke device** | Thiết bị bị thu hồi (revoked) → MQTT không đăng nhập được. Firmware nên có hành vi rõ (ví dụ log và không spam reconnect). |

### 8. Người dùng & vận hành

| Điểm | Mô tả |
|------|--------|
| **Che radar / vô hiệu hóa** | Cố tình che radar → không phát hiện người; hoặc ngược lại vật chuyển động gây false positive. Khó ngăn hoàn toàn; analytics nên chấp nhận nhiễu. |
| **Mở cửa liên tục** | Reed switch báo door_opened/door_closed liên tục (bảo trì hoặc cố ý). Có thể rate-limit event door trên backend để tránh flood log. |
| **Ai bảo trì tại chỗ** | Một kiosk đặt xa → khi offline hoặc lỗi in ảnh, ai được gọi? Cần **runbook 1 kiosk** (kiểm tra nguồn, WiFi, SIM, giấy in) và contact rõ. |
| **Cập nhật cấu hình** | Thay đổi report_interval, led_timeout... qua `update_config`. Cần **validate** (min/max) ở firmware và backend; sai có thể gây tải cao hoặc mất báo cáo. |

---

## Phần B. Các bind để kiosk hoạt động ổn định

*Bind = điều kiện / ràng buộc / giới hạn cần thỏa để 1 kiosk chạy ổn định.*

### B1. Điều kiện phần cứng & lắp đặt

| Bind | Mô tả |
|------|--------|
| **Nguồn ổn định** | 12V (hoặc 5V/3.3V sau converter) trong spec; không sụt áp khi tải max (WiFi TX + in ảnh nếu có). |
| **Watchdog ngoài** | Luôn nối và cấu hình watchdog ngoài (GPIO 33), timeout 5–10s. |
| **RSSI tối thiểu** | WiFi RSSI tại vị trí lắp **≥ -70 dBm** (hoặc ngưỡng đã chọn) để heartbeat 30s ổn định. |
| **Vị trí radar / PIR** | Tránh nắng trực tiếp, khuất góc chết; dễ vệ sinh. |

### B2. Điều kiện mạng (1 kiosk, router 4G)

| Bind | Mô tả |
|------|--------|
| **SIM còn data** | Gói data 4G đủ dùng: MQTT + (nếu có) in ảnh; tham chiếu **data_usage_estimation.md**. Có cảnh báo hoặc gói dư để tránh hết data giữa kỳ. |
| **APN đúng** | Router 4G cấu hình APN đúng cho SIM. |
| **Port mở** | Outbound **8883** (MQTT TLS) và **123** (NTP) (hoặc port NTP đang dùng) không bị firewall chặn. |
| **Router ổn định** | Router ít restart ngoài ý; nếu mất điện thường xuyên thì cân nhắc UPS cho router. |

### B3. Điều kiện firmware & cấu hình

| Bind | Mô tả |
|------|--------|
| **NTP sync** | Sau vài phút online, NTP sync thành công (`time_synced: true`). Nếu không, có chính sách rõ: có gửi event/metrics hay không. |
| **Cấu hình trong khoảng** | `report_interval_sec`, `led_timeout_sec`, `radar_sensitivity`, `speaker_volume` nằm trong min/max (theo mqtt_schema.md); validate ở firmware khi nhận `update_config`. |
| **Offline buffer giới hạn** | Buffer tối đa (ví dụ 500 event), xóa sau khi publish thành công; không buffer status. |
| **OTA an toàn** | OTA có kiểm tra SHA256; rollback khi boot fail. Với 1 kiosk: vẫn nên test OTA trên bản tương tự trước khi áp dụng. |

### B4. Điều kiện backend & vận hành

| Bind | Mô tả |
|------|--------|
| **Heartbeat 30s, offline 3 phút** | Backend đánh dấu offline khi không nhận status **≥ 3 phút** (hoặc LWT). Cảnh báo khi 1 kiosk offline. |
| **Cảnh báo nhiệt / heap** | (Khuyến nghị) Cảnh báo khi `temperature` > ngưỡng (ví dụ 45°C) hoặc `heap_free` < ngưỡng (ví dụ 50 KB). |
| **Runbook 1 kiosk** | Có checklist khi 1 kiosk offline hoặc lỗi in: kiểm tra nguồn, WiFi/RSSI, SIM/data, broker, rồi mới đến firmware/reboot. |
| **Giới hạn in ảnh (nếu có)** | Giới hạn số ảnh/lượt hoặc/ngày theo chính sách; timeout tải ảnh; thông báo rõ khi mất mạng. |

### B5. Tóm tắt checklist “1 kiosk ổn định”

- [ ] Nguồn ổn định; watchdog ngoài hoạt động.
- [ ] WiFi RSSI ≥ -70 dBm tại vị trí lắp.
- [ ] SIM 4G còn data; APN đúng; port 8883 (và NTP) mở.
- [ ] NTP sync; `time_synced: true` trong status.
- [ ] Cấu hình (report_interval, led_timeout, …) trong khoảng cho phép.
- [ ] Backend nhận status/event/metrics; cảnh báo offline sau 3 phút.
- [ ] (Tùy chọn) Cảnh báo nhiệt độ / heap khi vượt ngưỡng.
- [ ] (Nếu có in ảnh) Timeout và giới hạn in; xử lý khi mất mạng; runbook bảo trì.

---

## Tài liệu tham chiếu

| Doc | Nội dung liên quan |
|-----|--------------------|
| **data_usage_estimation.md** | Dung lượng 1 kiosk (MQTT, in ảnh); chọn gói SIM 4G. |
| **kiosk_deployment_checklist.md** | Checklist trước/sau khi lắp, bảo trì. |
| **kiosk_system_architecture.md** | Runbook, rủi ro WiFi/OTA, heartbeat 3 phút. |
| **firmware_architecture.md** | Offline buffer, NTP, watchdog, boot, config. |
| **mqtt_schema.md** | Status, metrics, update_config, validation. |
