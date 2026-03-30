# Dung lượng truyền dữ liệu Kiosk & Router SIM 4G

Tài liệu giúp **tính dung lượng tiêu thụ** khi **mỗi kiosk dùng 1 router SIM 4G riêng** (1 kiosk = 1 router). Toàn bộ traffic từ kiosk đi qua kết nối 4G của router đó, cần ước tính để chọn gói data (MB/tháng).

**Phạm vi:** Chỉ tính cho **1 kiosk**; không gộp nhiều kiosk chung 1 router.

---

## 1. Luồng dữ liệu qua router 4G (1 kiosk, 1 router)

```
[Kiosk] --WiFi--> [Router 4G riêng] --mobile data--> [Internet: broker/API, NTP, OTA, in ảnh]
```

Mọi dữ liệu kiosk gửi/nhận đều tính vào **dung lượng 4G** của SIM gắn trên router đó (upload + download).

---

## 2. Các phương pháp truyền dữ liệu & lớp TCP/UDP

Dữ liệu từ kiosk có thể truyền bằng nhiều phương pháp. Phần dưới nêu **lớp transport (TCP vs UDP)** và **các giao thức ứng dụng** phổ biến, kèm ước tính dung lượng và so sánh.

### 2.0. TCP vs UDP (lớp transport)


|                              | TCP                                                 | UDP                                  |
| ---------------------------- | --------------------------------------------------- | ------------------------------------ |
| **Kết nối**                  | Hướng kết nối (handshake 3 bước).                   | Không kết nối.                       |
| **Header**                   | 20 byte (cơ bản) + option; mỗi segment mang header. | 8 byte/packet.                       |
| **Đảm bảo**                  | Giao nhận đầy đủ, thứ tự, retransmit khi mất.       | Không đảm bảo; gói có thể mất/trùng. |
| **Overhead cho dữ liệu nhỏ** | Cao: handshake + ACK + keepalive.                   | Thấp: chỉ payload + 8 byte.          |
| **Dùng trong kiosk**         | MQTT (8883), HTTPS (443), WebSocket, TCP custom.    | NTP (123), (CoAP nếu dùng).          |


**TCP overhead ước tính (cho từng phiên):**

- **Handshake:** 3 segment (SYN, SYN-ACK, ACK) ≈ 60–120 byte (tùy option).
- **TLS handshake (nếu dùng):** thêm vài KB (certificate, key exchange).
- **Mỗi segment:** 20 byte TCP + IP header 20 byte = 40 byte overhead; payload nhỏ (ví dụ 100 byte) → hiệu suất kém so với gói UDP vài trăm byte.

Vì vậy với **traffic nhỏ, tần suất cao** (status 30s, metrics 5 phút), tỉ lệ overhead/byte khá lớn; giao thức trên TCP (MQTT, HTTPS) vẫn tiện vì chuẩn, có TLS, dễ qua firewall.

---

### 2.1. Bảng so sánh các phương pháp truyền (cho 1 kiosk, cùng workload)

Workload giả định: **status mỗi 30s** + **metrics mỗi 5 phút** + event thỉnh thoảng (không tính in ảnh, OTA).


| Phương pháp                     | Lớp transport | Port      | Push / Poll                    | Ước tính MB/tháng (1 kiosk) | Overhead                                          | Ghi chú                                                      |
| ------------------------------- | ------------- | --------- | ------------------------------ | --------------------------- | ------------------------------------------------- | ------------------------------------------------------------ |
| **MQTT over TLS**               | **TCP**       | 8883      | Push (device gửi khi có)       | **~15–25**                  | TCP + TLS + MQTT header (~ vài chục byte/packet)  | Đang dùng; phù hợp IoT, ít code.                             |
| **HTTPS REST (polling)**        | **TCP**       | 443       | Poll (device gọi API định kỳ)  | **~20–35**                  | TCP + TLS + HTTP header mỗi request/response      | Mỗi lần poll = 1 request + 1 response → nhiều round-trip.    |
| **WebSocket over TLS**          | **TCP**       | 443       | Push (kết nối giữ, gửi khi có) | **~15–28**                  | 1 lần handshake TCP+TLS+WS; frame 2–14 byte/frame | Giống MQTT về mô hình; cần server hỗ trợ WS.                 |
| **TCP thuần (custom protocol)** | **TCP**       | tùy chọn  | Push hoặc poll                 | **~12–22**                  | Chỉ TCP + IP; không HTTP/MQTT                     | Overhead thấp nhất trên TCP; phải tự implement auth, format. |
| **CoAP (UDP)**                  | **UDP**       | 5683/5684 | Push hoặc observe              | **~10–18**                  | Header CoAP 4–20 byte + UDP 8 byte                | Nhẹ, tiết kiệm; ít phổ biến hơn MQTT, firewall có thể chặn.  |
| **NTP**                         | **UDP**       | 123       | Poll (vài lần/ngày)            | **< 1**                     | 48 byte/packet, rất ít lần                        | Chỉ dùng đồng bộ thời gian.                                  |


*Ước tính đã gộp hệ số 1,3–1,5 cho TLS/header trên đường truyền.*

---

### 2.2. So sánh nhanh: khi nào dùng phương pháp nào


| Tiêu chí                       | MQTT (TCP)                    | HTTPS REST (TCP)   | WebSocket (TCP)    | TCP custom      | CoAP (UDP)               |
| ------------------------------ | ----------------------------- | ------------------ | ------------------ | --------------- | ------------------------ |
| **Dung lượng (cùng workload)** | Thấp–TB                       | TB–cao hơn         | Thấp–TB            | Thấp nhất (TCP) | Thấp nhất                |
| **Độ phức tạp backend**        | Cần broker                    | Chỉ API REST       | Cần server WS      | Tự implement    | Cần server CoAP          |
| **Firewall / 4G**              | Port 8883 đôi khi bị kiểm tra | Port 443 thường mở | Port 443 thường mở | Tùy port        | Port 5683 có thể bị chặn |
| **Push từ server**             | Có (publish command)          | Không (phải poll)  | Có                 | Tùy thiết kế    | Có (observe)             |
| **Chuẩn hóa**                  | Chuẩn IoT                     | Rất phổ biến       | Chuẩn web          | Không           | Chuẩn IoT nhẹ            |


**Kết luận ngắn:** Với **1 kiosk**, **1 router 4G**, phương pháp **MQTT over TLS (TCP 8883)** cân bằng tốt giữa dung lượng, push command và độ phổ biến. Nếu muốn tối ưu tối đa dung lượng và chấp nhận tự làm giao thức thì **TCP custom** hoặc **CoAP (UDP)** có thể giảm thêm vài MB/tháng; **HTTPS polling** thường tốn data hơn và không push được từ server.

---

## 3. Thành phần tiêu thụ dung lượng (1 kiosk, 1 router)


| Thành phần | Giao thức       | Hướng                            | Mô tả                                                                |
| ---------- | --------------- | -------------------------------- | -------------------------------------------------------------------- |
| **MQTT**   | TLS (port 8883) | Upload (chủ yếu) + Download (ít) | Status, event, metrics, log, command_ack; nhận lệnh từ backend.      |
| **NTP**    | UDP             | Upload + Download (rất ít)       | Đồng bộ thời gian, vài KB/ngày.                                      |
| **OTA**    | HTTPS           | Download                         | Chỉ khi cập nhật firmware (vài MB/lần, không định kỳ).               |
| **In ảnh** | HTTPS / TCP     | Download (chủ yếu)               | Kiosk tải ảnh để in: **~1,5 MB/ảnh**; một người có thể in nhiều ảnh. |


- **MQTT** chạy trên **TCP** (port 8883) + TLS; **HTTPS** (OTA, in ảnh) cũng trên **TCP** (443). NTP dùng **UDP** (123).
- Phần lớn dung lượng đến từ **MQTT** (khi không in ảnh). Nếu kiosk có **in ảnh**, dung lượng in ảnh thường **lớn hơn rất nhiều** so với MQTT. NTP và OTA thường không đáng kể so với MQTT hàng ngày (trừ tháng có OTA).

---

## 3.1. Đã tính dung lượng gửi trạng thái máy, nhiệt độ, độ ẩm về server

Có. Toàn bộ traffic gửi **trạng thái máy**, **nhiệt độ**, **độ ẩm** về server đều đi qua MQTT và đã nằm trong ước tính:


| Dữ liệu gửi về server | Topic MQTT           | Nội dung payload                                                                  | Tần suất        | Đã tính trong ước tính                                  |
| --------------------- | -------------------- | --------------------------------------------------------------------------------- | --------------- | ------------------------------------------------------- |
| **Trạng thái máy**    | `kiosk/{id}/status`  | device_id, firmware, uptime, online, wifi_rssi, heap_free, time_synced, timestamp | Mỗi **30 giây** | Có — ~120–180 bytes/lần, chiếm phần lớn traffic.        |
| **Nhiệt độ, độ ẩm**   | `kiosk/{id}/metrics` | device_id, timestamp, people_count, interactions, **temperature**, **humidity**   | Mỗi **5 phút**  | Có — ~100–150 bytes/lần (đã gộp temperature, humidity). |


- **Trạng thái máy (status):** Gửi mỗi 30s → ~2.880 lần/ngày → đã tính trong dòng "status" ở bảng §4 và trong tổng ~15–20 MB/tháng (§5).
- **Nhiệt độ, độ ẩm (metrics):** Gửi mỗi 5 phút cùng với people_count, interactions → ~288 lần/ngày → đã tính trong dòng "metrics" và trong cùng tổng dung lượng.

Không cần cộng thêm dung lượng riêng cho nhiệt độ/độ ẩm — chúng nằm trong payload **metrics** đã ước tính.

## 4. Ước tính bytes theo loại message MQTT

Kích thước dưới đây là **payload JSON**; trên đường truyền thực tế (MQTT frame + topic + TLS) thường nhân thêm **~1,3–1,5 lần**.


| Message                             | Payload (bytes) | Tần suất (mặc định) | Ghi chú                                                                      |
| ----------------------------------- | --------------- | ------------------- | ---------------------------------------------------------------------------- |
| **status**                          | ~120–180        | Mỗi 30 giây         | **Trạng thái máy** (heartbeat): uptime, wifi_rssi, heap_free, time_synced... |
| **event** (person_detected, door_*) | ~80–100         | Theo sự kiện        | Tùy số lần phát hiện người / mở cửa.                                         |
| **metrics**                         | ~100–150        | Mỗi 5 phút          | **Nhiệt độ, độ ẩm** + people_count, interactions (gửi về server).            |
| **command_ack**                     | ~50–80          | Khi có lệnh         | Reboot, play_sound, update_config...                                         |
| **log** (tùy chọn)                  | ~80–120         | Khi có log          | Không bật thì = 0.                                                           |


*Overhead trên đường truyền:* TCP + IP (40 byte/segment) + TLS (vài chục byte) + MQTT header → tổng thường nhân payload khoảng **1,3–1,5 lần**.

---

## 4.0. MQTT ~25 MB/tháng gồm những gì? (chi tiết 1 kiosk)

Dung lượng MQTT **~15–25 MB/tháng** (1 kiosk) đến từ **upload** là chủ yếu (device gửi status, event, metrics, command_ack). Download (nhận command) rất ít. Dưới đây liệt kê từng thứ và tính ra con số.

**Quy ước:** Payload = kích thước JSON; "trên dây" = payload × 1,4 (topic + MQTT frame + TCP + TLS). 1 tháng = 30 ngày.


| Thành phần MQTT                                           | Topic                    | Tần suất                        | Số lần / 30 ngày        | Payload (byte) | Trên dây (×1,4) | Tổng bytes / 30 ngày | Tổng MB   |
| --------------------------------------------------------- | ------------------------ | ------------------------------- | ----------------------- | -------------- | --------------- | -------------------- | --------- |
| **Status (heartbeat)**                                    | `kiosk/{id}/status`      | Mỗi **30 giây**                 | 30×24×60×2 = **86 400** | ~150           | ~210            | 18 144 000           | **~17,3** |
| **Metrics** (nhiệt độ, độ ẩm, people_count, interactions) | `kiosk/{id}/metrics`     | Mỗi **5 phút**                  | 30×24×12 = **8 640**    | ~130           | ~182            | 1 572 480            | **~1,5**  |
| **Event** (person_detected, door_*)                       | `kiosk/{id}/event`       | Theo sự kiện (vd **200/ngày**)  | 200×30 = **6 000**      | ~90            | ~126            | 756 000              | **~0,7**  |
| **Command_ack** (phản hồi lệnh)                           | `kiosk/{id}/command_ack` | Khi có lệnh (vd **2/ngày**)     | 2×30 = **60**           | ~65            | ~91             | 5 460                | **<0,01** |
| **Log** (tùy chọn)                                        | `kiosk/{id}/log`         | Khi có (vd **5/ngày**)          | 5×30 = **150**          | ~100           | ~140            | 21 000               | **~0,02** |
| **Nhận command** (download)                               | `kiosk/{id}/command`     | Khi backend gửi (vd **2/ngày**) | 60                      | ~80            | ~112            | 6 720                | **<0,01** |


**Tổng upload (device gửi):** ~~17,3 + 1,5 + 0,7 + 0,01 + 0,02 ≈ **19,5 MB/tháng**.~~  
~~**Tổng download (device nhận):** <0,01 MB/tháng (chỉ command).~~  
~~**Tổng MQTT:** ≈ **~~20 MB/tháng** (nếu event 200/ngày, ít log).

- Nếu event nhiều hơn (vd 500/ngày): +0,7 MB → tổng ~**21 MB**.
- Nếu overhead thực tế cao hơn (1,5× thay vì 1,4×): status ~~18,5 MB → tổng **~~25 MB**.
- **Kết luận:** Phần lớn **~25 MB** là do **status gửi mỗi 30 giây** (86 400 lần/tháng × ~210 byte ≈ **17–18 MB**). Metrics và event chỉ thêm vài MB.

---

## 4.1. In ảnh (khi kiosk có tính năng in)


| Tham số                         | Giá trị     | Ghi chú                                           |
| ------------------------------- | ----------- | ------------------------------------------------- |
| **Dung lượng trung bình 1 ảnh** | **~1,5 MB** | Ảnh tải về (hoặc nhận từ máy chủ/thiết bị) để in. |
| **Số ảnh mỗi lượt**             | Thay đổi    | Một người có thể in **nhiều ảnh** trong một lần.  |


**Công thức ước tính dung lượng in ảnh:**

```
Dung lượng in ảnh (MB/ngày)  = Số lượt in ảnh/ngày × Số ảnh trung bình/lượt × 1,5 (MB/ảnh)
Dung lượng in ảnh (MB/tháng) = MB/ngày × 30
```

**Ví dụ:**

- 10 lượt in/ngày, trung bình 2 ảnh/lượt → 10 × 2 × 1,5 = **30 MB/ngày** → **~900 MB/tháng** (chỉ riêng in ảnh).
- 5 lượt in/ngày, trung bình 3 ảnh/lượt → 5 × 3 × 1,5 = **22,5 MB/ngày** → **~675 MB/tháng**.

Lưu ý: Nếu ảnh được truyền qua cùng kết nối 4G (router SIM), toàn bộ dung lượng trên đều tính vào gói data. Ảnh thường tải qua **HTTPS (TCP 443)**.

---

## 5. Dung lượng ước tính cho 1 kiosk, 1 router (24/7)

### Chỉ MQTT (status + metrics, không event)


| Khoảng               | Upload (TX) | Download (RX) | Tổng ước tính   |
| -------------------- | ----------- | ------------- | --------------- |
| **/giờ**             | ~15–25 KB   | ~0,5–2 KB     | ~20–30 KB       |
| **/ngày**            | ~400–600 KB | ~10–50 KB     | **~0,5–0,7 MB** |
| **/tháng (30 ngày)** | ~12–18 MB   | ~0,3–1,5 MB   | **~15–20 MB**   |


### Cộng thêm event (ví dụ 200 person_detected/ngày)

- Mỗi event ~90 bytes × 1,4 (overhead) ≈ 130 bytes.
- 200 event/ngày ≈ 26 KB/ngày → **~0,8 MB/tháng**.
- **Tổng 1 kiosk/tháng (có event): ~20–25 MB.**

### NTP

- Rất nhỏ, vài KB/ngày → **< 1 MB/tháng**.

### OTA (cập nhật firmware)

- Mỗi lần tải firmware: ~~200–500 KB (file .bin nén). Vài lần/năm → trung bình **~~1–2 MB/tháng** nếu tính dàn trải.

### Cộng thêm in ảnh (kiosk có tính năng in)

- **1 ảnh ≈ 1,5 MB.** Một người có thể in nhiều ảnh mỗi lượt.
- Dung lượng in ảnh/ngày = (số lượt in/ngày) × (số ảnh trung bình/lượt) × 1,5 MB.
- **Tổng 1 kiosk/tháng (có in ảnh)** = MQTT + NTP + OTA (~25 MB) + **in ảnh** (tùy số lượt in và số ảnh — xem §4.1 và bảng dưới §8).

---

## 6. Công thức chọn gói data SIM 4G (1 kiosk = 1 router)

### 1 kiosk / 1 router 4G

- **Chỉ MQTT + NTP:** ~**20 MB/tháng** (làm tròn an toàn **30–50 MB/tháng**).
- **MQTT + event trung bình + NTP:** ~**25–30 MB/tháng** → gói **50 MB/tháng** là dư.
- **Có OTA vài lần/năm:** cộng thêm ~2–5 MB/tháng → vẫn **50 MB/tháng** là đủ.

**Gợi ý:** Gói **50–100 MB/tháng** cho 1 kiosk là an toàn, có buffer cho event cao hoặc log.

### Kiosk có tính năng in ảnh (1 kiosk, 1 router 4G)

Dung lượng in ảnh thường **lớn hơn rất nhiều** so với MQTT. Công thức:

```
Dung lượng 4G (MB/tháng) ≈ 25 (MQTT+NTP+OTA) + [Số lượt in/ngày × Số ảnh TB/lượt × 1,5 MB × 30]
```


| Ví dụ (lượt in/ngày × ảnh/lượt) | In ảnh (MB/tháng) | Tổng ước tính (MB/tháng) | Gói data gợi ý |
| ------------------------------- | ----------------- | ------------------------ | -------------- |
| 5 × 1 ảnh                       | 225               | ~250                     | 500 MB/tháng   |
| 10 × 2 ảnh                      | 900               | ~925                     | 1–2 GB/tháng   |
| 20 × 2 ảnh                      | 1 800             | ~1,8 GB                  | 2–3 GB/tháng   |
| 15 × 3 ảnh                      | 2 025             | ~2 GB                    | 2–3 GB/tháng   |


Nên ước tính **số lượt in/ngày** và **số ảnh trung bình mỗi lượt** theo từng điểm đặt để chọn gói SIM phù hợp.

**Lưu ý:** Tài liệu này chỉ tính **1 kiosk, 1 router riêng**. Mỗi kiosk khác dùng router/SIM khác thì tính riêng theo cùng công thức.

---

## 7. Lưu ý khi dùng router SIM 4G (1 kiosk, 1 router)

1. **Tính cả upload lẫn download:** Nhà mạng tính cả hai; kiosk upload (status/event/metrics) nhiều hơn download (command, OTA thỉnh thoảng).
2. **TCP & TLS:** MQTT dùng **TCP** (8883) + TLS; HTTPS (OTA, in ảnh) dùng **TCP** (443) + TLS. Overhead đã gộp trong hệ số 1,3–1,5 lần.
3. **1 kiosk = 1 router:** Mỗi kiosk có router 4G riêng; dung lượng tính cho từng SIM riêng biệt.
4. **Cập nhật firmware (OTA):** Nên làm ngoài giờ cao điểm; tháng có OTA có thể vượt trung bình vài MB.
5. **In ảnh:** Trung bình **~1,5 MB/ảnh**; một người có thể in nhiều ảnh. Nếu ảnh tải qua 4G, cần cộng dung lượng in ảnh vào gói data (thường chiếm phần lớn traffic).
6. **Thống kê thực tế:** Khi firmware có counter `bytes_tx_total` / `bytes_rx_total`, backend có thể lưu và báo cáo dung lượng thực tế để so sánh với ước tính và điều chỉnh gói data.

---

## 8. Tóm tắt bảng chọn gói data (1 kiosk, 1 router)

### 1 kiosk **không** in ảnh (chỉ MQTT, NTP, OTA, event)


| Ước tính MB/tháng | Gói data gợi ý |
| ----------------- | -------------- |
| 20–30             | 50 MB/tháng    |


### 1 kiosk **có** in ảnh (~1,5 MB/ảnh, 1 người có thể in nhiều ảnh)

Ước tính in ảnh: **Số lượt in/ngày × Số ảnh trung bình/lượt × 1,5 MB × 30 ngày** (MB/tháng). Cộng thêm ~25 MB cho MQTT+NTP+OTA.


| Số lượt in/ngày | Ảnh TB/lượt | In ảnh (MB/tháng) | Tổng ước tính (1 kiosk) | Gói data gợi ý |
| --------------- | ----------- | ----------------- | ----------------------- | -------------- |
| 5               | 1           | 225               | ~250 MB                 | 500 MB/tháng   |
| 10              | 2           | 900               | ~1 GB                   | 1–2 GB/tháng   |
| 20              | 2           | 1 800             | ~2 GB                   | 2–3 GB/tháng   |
| 30              | 2           | 2 700             | ~3 GB                   | 3–5 GB/tháng   |


Có thể dùng các bảng trên làm cơ sở **tính dung lượng tiêu thụ** khi triển khai **1 kiosk với 1 router SIM 4G riêng** và chọn gói SIM phù hợp. So sánh các phương pháp truyền (MQTT, HTTPS, WebSocket, TCP custom, CoAP) xem §2.