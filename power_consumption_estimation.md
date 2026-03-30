# Tính toán cường độ điện (dòng & công suất) cho 1 kiosk

Tài liệu này ước tính **dòng điện và công suất** cần cho **một kiosk** theo kiến trúc phần cứng trong **kiosk_system_architecture.md**, gồm hai cấu hình:

- **Phần A:** Chỉ board ESP32 + cảm biến + loa (nguồn 12V → Buck 5V → LDO 3,3V).
- **Phần B:** Kiosk có **Raspberry Pi 5 + tản nhiệt + cảm biến** (ESP32 + Pi 5 cùng nguồn hoặc nguồn riêng).

---

## 0. Tại sao dùng nguồn 12V khi cảm biến chỉ cần 3,3V và 5V?

Cảm biến và MCU **không dùng trực tiếp 12V** — chúng chạy ở **3,3V** (ESP32, SHT30, LED, reed…) và **5V** (LD2420, MAX98357A, Pi 5). **12V là điện áp *đầu vào* (từ adapter/dây nguồn)**, rồi được hạ xuống 5V và 3,3V bằng Buck và LDO.

**Lý do chọn 12V làm nguồn chính:**


| Lý do                     | Giải thích ngắn                                                                                                            |
| ------------------------- | -------------------------------------------------------------------------------------------------------------------------- |
| **Chuẩn phổ biến**        | Adapter 12V DC rất phổ biến (cục nguồn, tủ điện, nguồn công nghiệp). Dễ mua, dễ thay.                                      |
| **Phân phối dây**         | Cùng công suất P: điện áp cao hơn → dòng thấp hơn (I = P/V). Dây từ nguồn đến kiosk ít sụt áp hơn nếu dùng 12V thay vì 5V. |
| **Một dây, một adapter**  | Một nguồn 12V cấp cho toàn bộ: Buck → 5V (radar, loa, Pi, LDO) và LDO → 3,3V. Không cần nhiều adapter (5V + 3,3V riêng).   |
| **Tải 12V khác (nếu có)** | Màn hình, quạt, máy in… nhiều loại chạy 12V. Có sẵn rail 12V thì cấp trực tiếp.                                            |


**Luồng thực tế:** 12V (đầu vào) → **Buck** → 5V → (radar, loa, Pi…) và 5V → **LDO** → 3,3V → (ESP32, cảm biến). Cảm biến vẫn chỉ dùng 3,3V hoặc 5V; 12V chỉ là “điểm vào” của hệ thống.

---

### 0.1. Phương án: nguồn tổ ong 5V 5A làm nguồn chính

Khi kiosk **chỉ cần** rail 5V và 3,3V (không có màn hình/quạt/máy in 12V), dùng **một nguồn 5V 5A** (nguồn tổ ong hoặc adapter) thay cho 12V + Buck:

```
5V 5A (nguồn tổ ong) → 5V rail
                            ├→ Raspberry Pi 5 (nếu có)
                            ├→ LD2420, MAX98357A
                            └→ LDO → 3,3V (ESP32, SHT30, LED, Reed…)
```

**Ưu điểm:** Bớt tầng Buck → mạch đơn giản, ít linh kiện. Nguồn 5V 5A (25 W) phổ biến, rẻ.

**Khi nào chọn 12V vs 5V:**


| Chọn **12V**                        | Chọn **5V 5A**            |
| ----------------------------------- | ------------------------- |
| Có tải 12V (màn hình, quạt, máy in) | Không có tải 12V          |
| Dây nguồn dài (12V ít sụt áp hơn)   | Dây ngắn, nguồn gần board |
| Tủ điện đã có sẵn rail 12V          | Muốn đơn giản, bỏ Buck    |


---

### 0.2. Kiểm tra chi tiết: nguồn 5V 5A có đủ cho tất cả thiết bị không?

Khi dùng **một nguồn tổ ong 5V 5A (25 W)** làm nguồn chính, **toàn bộ** thiết bị đều lấy điện từ rail 5V (trực tiếp hoặc qua LDO). Bảng dưới liệt kê **từng thiết bị**, dòng tiêu thụ **trên rail 5V**, rồi cộng tổng và so với 5 A.

**Lưu ý:** Thiết bị 3,3V (ESP32, SHT30, LED, reed, watchdog) được cấp qua **LDO**. Nguồn 5V phải cấp **dòng vào LDO** (đầu vào 5V), không phải dòng ra 3,3V: I_5V_vào_LDO ≈ P_3,3V / (η_LDO × 5V).

#### Bảng 1: Dòng tiêu thụ từng thiết bị trên rail 5V (kiosk có Pi 5)


| STT | Thiết bị                | Cấp từ       | Dòng TB     | Dòng peak   | Ghi chú                                                    |
| --- | ----------------------- | ------------ | ----------- | ----------- | ---------------------------------------------------------- |
| 1   | Raspberry Pi 5          | 5V trực tiếp | 0,5–0,7 A   | **1,8 A**   | Under load 7–9 W.                                          |
| 2   | Quạt tản nhiệt (nếu có) | 5V trực tiếp | 0,05 A      | 0,1 A       | Thụ động = 0.                                              |
| 3   | LD2420 (radar)          | 5V trực tiếp | 0,08 A      | 0,08 A      | Cảm biến radar 24GHz.                                      |
| 4   | MAX98357A (ampli loa)   | 5V trực tiếp | 0,1 A       | **0,4 A**   | Phát loa max ~1,8 W vào 8 Ω.                               |
| 5   | LDO (đầu vào 5V) → 3,3V | 5V vào LDO   | ~0,19 A     | **~0,31 A** | Cấp ESP32, SHT30, LED, reed, watchdog. P_3,3V peak 0,87 W. |
| —   | **Tổng dòng rail 5V**   |              | **~1,02 A** | **~2,7 A**  |                                                            |


**Chi tiết rail 3,3V (để tính dòng vào LDO):** Tổng 3,3V TB ~160 mA, peak ~265 mA → P_3,3V peak 0,87 W → dòng 5V vào LDO (η 85%): 0,87/(0,85×5) ≈ 0,31 A.

#### So sánh với nguồn 5V 5A


| Chế độ    | Tổng dòng 5V cần | Nguồn 5V 5A | Đủ không? | Dự trữ             |
| --------- | ---------------- | ----------- | --------- | ------------------ |
| Điển hình | ~1,0 A           | 5 A         | Có        | ~~4 A (~~80% dư)   |
| Peak      | ~2,7 A           | 5 A         | Có        | ~~2,3 A (~~45% dư) |


**Kết luận:** Nguồn **5V 5A** **đủ** cung cấp cho tất cả thiết bị. Tổng dòng peak ~**2,7 A** < 5 A, còn dự trữ ~**2,3 A** khi Pi 5 + loa + ESP32 cùng chạy cao — an toàn. Nếu không có Pi 5, tổng 5V peak ~0,79 A thì càng dư.

#### Thay nguồn tổ ong 5V 5A, dùng với Pi + các thiết bị khác thì còn bị sụt áp không?

Với **tải hiện tại** (Pi 5 + quạt + LD2420 + MAX98357A + LDO) tổng peak ~**2,7 A**, nguồn **5V 5A** có đủ dung lượng (2,7 A < 5 A) nên **thường không bị sụt áp do quá tải** — với điều kiện:


| Điều kiện                   | Ghi chú                                                                                                                                                                                                  |
| --------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Nguồn tổ ong chất lượng** | Nguồn rẻ có thể sụt áp ngay ở 2–3 A (ra còn 4,5–4,7 V). Nên chọn nguồn có ghi rõ 5V ±5%, đủ 5A; thương hiệu hoặc nguồn tổ ong công nghiệp.                                                               |
| **Dây từ nguồn đến board**  | Dây ngắn, tiết diện đủ (vd 2×0,75 mm² trở lên cho vài ampe). Dây dài/mảnh → sụt áp trên dây.                                                                                                             |
| **Pi 5 lấy nguồn từ đâu**   | Nếu Pi vẫn cấp qua **cáp USB-C** từ rail 5V: dùng cáp **ngắn, dày** để tránh sụt áp trên cáp (như khi dùng 5V 3A). Nếu hàn trực tiếp 5V/GND từ rail vào board Pi (bỏ qua USB-C) thì không phụ thuộc cáp. |


**Tóm lại:** Thay **5V 3A** bằng **nguồn tổ ong 5V 5A** (chất lượng) và nối **dây/cáp ngắn, đủ tiết diện** thì với **các thiết bị hiện tại** (Pi + radar + loa + ESP32/cảm biến) **không nên** còn sụt áp. Nếu sau này thêm máy in + màn hình 5V và tổng dòng gần 5A thì cần kiểm tra lại (đo 5V tại Pi khi tải max) hoặc chuyển sang 5V 10A.

#### Nếu dùng nguồn 5V 10A thì sao?

**5V 10A (50 W)** vẫn dùng được và **dư dòng nhiều hơn** so với 5V 5A. Tổng tải không đổi (~2,7 A peak), chỉ có dung lượng nguồn lớn hơn.


| Nguồn      | Dòng danh định | Tổng tải peak | Dự trữ              | Ghi chú                                                   |
| ---------- | -------------- | ------------- | ------------------- | --------------------------------------------------------- |
| 5V 5A      | 5 A            | ~2,7 A        | ~~2,3 A (~~45%)     | Đủ, phổ biến, rẻ.                                         |
| **5V 10A** | **10 A**       | ~2,7 A        | ~~**7,3 A (~~73%)** | Đủ dư, nguồn chạy nhẹ tải hơn, ít nóng, tuổi thọ tốt hơn. |


**Kết luận:** Dùng **5V 10A** được, thậm chí **tốt hơn** nếu muốn nguồn chạy thoải mái (≈27% tải thay vì ≈54% với 5A). Nhược điểm: nguồn 10A thường to hơn, đắt hơn một chút so với 5A; nếu không thêm tải (màn hình, máy in, v.v.) thì 5A đã đủ.

#### Cần thêm bao nhiêu thiết bị ngoại vi (5V) nữa mới "hết" 5V 5A?

Tải hiện tại (peak) ~**2,7 A** → còn dự trữ ~**2,3 A**. "Hết" 5A = thêm thiết bị 5V sao cho tổng ~5 A (nên giữ margin ~10%, tổng ~4,5 A an toàn).


| Thiết bị ngoại vi (5V)  | Dòng ước tính      | Ví dụ để dùng hết dự trữ ~2,3 A          |
| ----------------------- | ------------------ | ---------------------------------------- |
| Màn hình LCD 7–10" (5V) | 0,3–0,6 A          | ~4 màn 0,5 A hoặc 1 màn + thiết bị khác. |
| Máy in nhiệt (5V)       | 0,8–1,5 A (khi in) | 1–2 máy in khi in cùng lúc.              |
| Webcam / camera USB     | 0,2–0,5 A          | 5–10 thiết bị 0,2–0,5 A.                 |
| Strip LED 5V            | 0,5–2 A            | 1 strip 1–2 A hoặc 2–4 bóng ~0,5 A.      |
| Ổ SSD/HDD USB 2.5"      | 0,3–0,6 A          | 4–6 ổ ~0,5 A.                            |
| Quạt 5V                 | 0,1–0,2 A          | 10–20 quạt 0,1 A.                        |


**Ví dụ tổ hợp gần hết 5A:** 1 máy in nhiệt (~~1,2 A) + 1 màn 5V 7" (~~0,5 A) + 1 webcam (~~0,3 A) + strip LED (~~0,5 A) ≈ 2,5 A thêm → tổng 2,7 + 2,5 ≈ **5,2 A** (vượt 5A khi cùng lúc). Hoặc **2 máy in nhiệt** cùng in (~2,4 A) → tổng ~5,1 A.

**Với nguồn 5V 5A mà Pi chạy nặng + các ngoại vi trên thì có vấn đề gì không?**

**Có.** Khi **Pi đang peak** (1,8 A) và **các ngoại vi cùng bật** (máy in in, màn sáng, webcam, LED…) thì tổng dòng có thể **vượt 5 A** (ví dụ 2,7 + 2,5 ≈ 5,2 A). Hậu quả có thể gặp: **(1) Sụt áp** — nguồn hoặc dây không đủ → điện áp 5V xuống dưới ~4,75 V → Pi có thể **reboot**, đèn cảnh báo undervoltage (sét trên màn hình). **(2) Nguồn quá tải** — nguồn nóng, bảo vệ quá dòng cắt (tắt nguồn) hoặc chạy quá công suất lâu dài dễ hỏng. **(3) Chạy không ổn định** — khi in ảnh + màn hình sáng + Pi xử lý nặng cùng lúc dễ treo/restart. **Khuyến nghị:** Nếu kiosk có **máy in + màn hình 5V + Pi chạy nặng** — nên dùng **nguồn 5V 10A** (hoặc tính đúng tổng dòng và chọn nguồn có margin 15–20%). Nếu tạm thời chỉ dùng 5V 5A thì tránh bật **cùng lúc** in + màn sáng max + tải CPU cao; hoặc giảm thiết bị ngoại vi.

**Kết luận:** Với 1 kiosk (Pi 5 + cảm biến + loa), 5V 5A **chưa hết**; chỉ khi thêm **nhiều** thiết bị 5V (1–2 máy in + màn hình + LED/camera) và chạy **cùng lúc** mới gần hoặc vượt 5A. Nếu dự định thêm máy in + màn hình 5V → nên dùng **5V 10A** hoặc tính lại tổng dòng theo thiết bị cụ thể.

---

## 1. Sơ đồ nguồn (tóm tắt)

### 1.1. Chỉ board ESP32 + cảm biến + loa

```
12V DC (nguồn ngoài) → Buck (LM2596/MP1584) → 5V rail
                                                      ├→ LD2420 (radar)
                                                      ├→ MAX98357A (loa)
                                                      └→ LDO → 3,3V rail
                                                                   ├→ ESP32-S3
                                                                   ├→ SHT30 (T/H)
                                                                   ├→ LED, Reed, Watchdog
                                                                   └→ logic khác
```

### 1.2. Kiosk có Raspberry Pi 5 + tản nhiệt + cảm biến

```
12V DC (nguồn ngoài) → Buck 5V (công suất lớn) → 5V rail
                                                      ├→ Raspberry Pi 5 (5V, tối đa 5A theo spec)
                                                      ├→ Quạt tản nhiệt (5V, nếu có)
                                                      ├→ LD2420 (radar)
                                                      ├→ MAX98357A (loa)
                                                      └→ LDO → 3,3V rail
                                                                   ├→ ESP32-S3
                                                                   ├→ SHT30 (T/H)
                                                                   ├→ LED, Reed, Watchdog
                                                                   └→ cảm biến khác
```

---

## 2. Tiêu thụ theo từng nhánh

### 2.1. Rail 3,3V (LDO – cấp cho ESP32 và cảm biến)


| Thành phần     | Điện áp | Dòng điển hình | Dòng đỉnh (peak) | Ghi chú                   |
| -------------- | ------- | -------------- | ---------------- | ------------------------- |
| ESP32-S3       | 3,3 V   | 100–150 mA     | 240 mA           | WiFi bật, TX peak cao hơn |
| SHT30          | 3,3 V   | 0,2 mA         | 0,2 mA           | Đọc T/H mỗi 5 phút        |
| LED (GPIO 2)   | 3,3 V   | ~10 mA (TB)    | 20 mA            | Bật khi có người          |
| Reed, Watchdog | 3,3 V   | < 1 mA         | < 1 mA           | Bỏ qua                    |
| **Tổng 3,3V**  |         | **~160 mA**    | **~265 mA**      |                           |


- Công suất 3,3V: **P_3V3 ≈ 0,53 W (TB)** và **≈ 0,87 W (peak)**.

### 2.2. Rail 5V (Buck – radar + ampli loa), không có Pi 5


| Thành phần     | Điện áp | Dòng điển hình    | Dòng đỉnh (peak) | Ghi chú                                        |
| -------------- | ------- | ----------------- | ---------------- | ---------------------------------------------- |
| LD2420 (radar) | 5 V     | 60–80 mA          | 80 mA            | Module 24GHz, UART                             |
| MAX98357A      | 5 V     | 2,4 mA (idle)     | ~400 mA          | Idle 2,4 mA; phát loa ~1,8 W vào 8 Ω → ~400 mA |
| **Tổng 5V**    |         | **~80 mA** (idle) | **~480 mA**      | Khi không phát nhạc / phát nhạc max            |


### 2.3. Raspberry Pi 5 + tản nhiệt (khi kiosk có Pi 5)


| Thành phần           | Điện áp | Dòng điển hình | Dòng đỉnh (peak) | Ghi chú                                                            |
| -------------------- | ------- | -------------- | ---------------- | ------------------------------------------------------------------ |
| Raspberry Pi 5       | 5 V     | 0,5–0,7 A      | **1,4–1,8 A**    | Idle ~2,25–3,5 W; under load 7–9 W. Spec chính thức: 5V/5A (25 W). |
| Tản nhiệt (quạt)     | 5 V     | 0–0,1 A        | 0,1 A            | Quạt nhỏ 5V; tản nhiệt thụ động = 0 A.                             |
| **Tổng Pi 5 + quạt** | 5 V     | **~0,6 A**     | **~1,9 A**       | Lấy peak 1,9 A để chọn nguồn.                                      |


**Pi 5 có dùng hết 5V 5A không? Tại sao hãng spec 5V 5A?** Không — Pi 5 thực tế idle ~0,5–0,7 A, under load ~1,4–1,8 A. Hãng spec **5V 5A** cho **nguồn** vì: (1) Dự phòng peak khi boot/CPU nặng; (2) Cấp điện cho thiết bị USB (SSD, webcam…) qua Pi, tổng có thể gần 3–4 A; (3) 5A là **khả năng tối đa** nguồn, không phải dòng Pi luôn tiêu thụ; (4) USB-C PD 27W đáp ứng Pi + peripherals. **Kết luận:** Pi 5 một mình ~0,5–1,8 A; spec 5V 5A là cho nguồn (Pi + USB + margin), không phải Pi “ăn” hết 5A.

- Loa 8 Ω 5W: ampli MAX98357A @ 5V cấp tối đa ~~**1,8 W** vào 8 Ω (theo datasheet). Công suất đỉnh 5V: 1,8/0,92 ≈ 1,96 W → **~~0,4 A**.
- Trung bình khi có phát âm thanh (không max): lấy **~100 mA** cho MAX98357A.
- **Tổng 5V trung bình:** 80 (radar) + 100 (amp) = **180 mA**.  
- **Tổng 5V peak:** 80 + 400 = **480 mA**.

### 2.4. LDO 3,3V lấy từ 5V

- Công suất vào LDO ≈ công suất 3,3V (hiệu suất LDO ~80–90%).  
- Dòng từ 5V vào LDO: **0,16 A / 0,85 ≈ 0,19 A (TB)** và **0,265 / 0,85 ≈ 0,31 A (peak)**.

---

## 3. Tổng hợp dòng trên rail 5V (sau Buck)


| Chế độ     | Radar | MAX98357A | LDO (3,3V) | **Tổng 5V** |
| ---------- | ----- | --------- | ---------- | ----------- |
| Trung bình | 80 mA | 100 mA    | 190 mA     | **~370 mA** |
| Peak       | 80 mA | 400 mA    | 310 mA     | **~790 mA** |


- Công suất 5V: **P_5V ≈ 1,85 W (TB)** và **≈ 3,95 W (peak)**.

### 3.2. Tổng hợp rail 5V khi có Raspberry Pi 5 + tản nhiệt + cảm biến


| Chế độ     | Board (radar + loa + LDO) | Pi 5 + quạt | **Tổng 5V** |
| ---------- | ------------------------- | ----------- | ----------- |
| Trung bình | ~370 mA                   | ~600 mA     | **~0,97 A** |
| Peak       | ~790 mA                   | ~1 900 mA   | **~2,7 A**  |


- Công suất 5V khi có Pi 5: **P_5V ≈ 4,85 W (TB)** và **≈ 13,5 W (peak)**. Cần nguồn 5V **≥ 3,5 A** (18 W) để có margin.

---

## 4. Nguồn 12V (đầu vào)

- Buck 12V→5V hiệu suất khoảng **85–90%** (LM2596/MP1584).
- Công suất đầu vào 12V: **P_12V = P_5V / η**.  
  - TB: 1,85 / 0,88 ≈ **2,1 W**  
  - Peak: 3,95 / 0,88 ≈ **4,5 W**

**Dòng 12V (chỉ board, không Pi 5):**


| Chế độ     | Công suất 12V | Dòng 12V    |
| ---------- | ------------- | ----------- |
| Trung bình | ~2,1 W        | **~175 mA** |
| Peak       | ~4,5 W        | **~375 mA** |


### 4.2. Khi có Pi 5 (một nguồn 12V cho tất cả)

- Công suất 5V cần: 13,5 W (peak) + margin → lấy 20 W.
- Công suất 12V (Buck η ≈ 88%): 20 / 0,88 ≈ **22,7 W**.
- **Dòng 12V:** 22,7 / 12 ≈ **1,9 A** (peak). Khuyến nghị nguồn **12V / 2,5–3 A** (30–36 W).

---

## 5. Khuyến nghị nguồn cho 1 kiosk

### 5.1. Chỉ ESP32 + cảm biến + loa (không Pi 5)

- **Điện áp:** 12 V DC (ổn định, không sụt áp khi tải đỉnh).
- **Dòng danh định:** chọn nguồn có **dòng ≥ 0,5 A (500 mA)** để:
  - Có margin cho peak (~375 mA),
  - Tránh sụt áp khi dây dài hoặc khi WiFi TX + loa cùng lúc,
  - An toàn cho tuổi thọ nguồn (không chạy gần 100% định mức).
- **Công suất:** 12V × 0,5 A = **6 W** trở lên (ví dụ **6–10 W**).


| Đại lượng       | Giá trị khuyến nghị    |
| --------------- | ---------------------- |
| Điện áp nguồn   | 12 V DC                |
| Dòng danh định  | **≥ 0,5 A (500 mA)**   |
| Công suất nguồn | **≥ 6 W** (nên 6–10 W) |


### 5.2. Kiosk có Raspberry Pi 5 + tản nhiệt + cảm biến

Tổng dòng **5V** (từ Buck): phần ESP32/cảm biến/loa **~0,79 A peak** + Pi 5 + quạt **~1,9 A peak** → **~2,7 A @ 5V** (≈ 13,5 W). Thêm margin 20–25% → **5V / 3,5–4 A** (18–20 W).

Nếu dùng **một nguồn 12V** cho toàn bộ (Buck 12V→5V cấp cho Pi 5 + board):

- Công suất 5V cần: 20 W (để dư).
- Công suất 12V (với hiệu suất Buck ~88%): 20 / 0,88 ≈ **22,7 W**.
- Dòng 12V: 22,7 / 12 ≈ **1,9 A**.

**Khuyến nghị khi dùng 1 nguồn 12V:**


| Đại lượng       | Giá trị khuyến nghị                                |
| --------------- | -------------------------------------------------- |
| Điện áp nguồn   | 12 V DC                                            |
| Dòng danh định  | **≥ 2,5 A** (nên **3 A**)                          |
| Công suất nguồn | **≥ 30 W** (12V × 2,5 A); nên **36 W** (12V × 3 A) |


**Cách khác:** Dùng **hai nguồn**:

- **Nguồn 1:** 12V / 0,5 A (6 W) cho board ESP32 + cảm biến + loa (như §5.1).
- **Nguồn 2:** **5V / 5 A (25 W)** USB-C chính hãng cho Raspberry Pi 5 (đúng spec Pi 5, tản nhiệt/quạt thường lấy từ Pi nên không cần nguồn thêm).

### 5.3. Dùng nguồn tổ ong 5V làm nguồn chính (đủ cho toàn bộ)

Khi chọn **một nguồn 5V** (nguồn tổ ong hoặc adapter) thay vì 12V + Buck:


| Nguồn      | Điện áp | Dòng danh định | Công suất | Đủ cho                                                                     | Dự trữ (peak ~2,7 A)                           |
| ---------- | ------- | -------------- | --------- | -------------------------------------------------------------------------- | ---------------------------------------------- |
| **5V 5A**  | 5 V DC  | 5 A            | 25 W      | Pi 5 + quạt + LD2420 + MAX98357A + LDO (ESP32, SHT30, LED, reed, watchdog) | ~~2,3 A (~~45%)                                |
| **5V 10A** | 5 V DC  | 10 A           | 50 W      | Cùng các thiết bị trên                                                     | ~~7,3 A (~~73%) — nguồn chạy nhẹ tải, ít nóng. |


Chi tiết từng thiết bị và so sánh 5A vs 10A: xem **§0.2**.

---

## 6. Lưu ý

1. **Sụt áp:** Dây nguồn dài hoặc tiết diện nhỏ → sụt áp. Nên đo 12V/5V/3,3V tại kiosk khi tải max (WiFi + loa, và Pi 5 nếu có) để đảm bảo trong spec.
2. **Raspberry Pi 5:** Spec chính thức 5V/5A (25 W). Pi 5 thực tế idle ~2,25–3,5 W, under load 7–9 W. Nếu cấp từ Buck 12V→5V, Buck phải chịu được **≥ 4 A @ 5V** (20 W) để an toàn.
3. **Tản nhiệt:** Tản nhiệt thụ động không tốn điện. Quạt 5V thường 50–100 mA, đã gộp trong bảng Pi 5 + quạt.
4. **In ảnh:** Nếu kiosk có thêm module in ảnh, cần cộng thêm dòng máy in khi in và chọn nguồn lớn hơn.
5. **Màn hình:** Nếu màn hình do kiosk cấp nguồn, cộng công suất màn hình vào tổng và tính lại dòng 12V (hoặc 5V nếu màn hình 5V).
6. **ESP32-S3:** Số liệu dựa trên tài liệu Espressif (WiFi active ~100 mA+, peak khi TX có thể 200–250 mA).
7. **Pi 5 với nguồn 5V 3A — sụt áp:** Nhiều trường hợp **chỉ dùng Pi 5 với nguồn 5V 3A** đã bị sụt áp (cảnh báo undervoltage, reboot). Nguyên nhân: (1) Nguồn 3A rẻ không đủ 3A thực khi tải cao → áp sụt. (2) Cáp USB-C dài/mảnh → điện trở dây lớn → sụt áp (5V tại nguồn còn ~4,5 V tại Pi khi dòng ~1,5 A). (3) Pi 5 spec **5V/5A**; 3A dưới spec, Pi + USB peak dễ vượt 3A. **Khuyến nghị:** Dùng **nguồn 5V 5A** (chính hãng hoặc nguồn tổ ong chất lượng) và **cáp USB-C ngắn, dày**; tránh cáp dài > 1 m hoặc cáp mỏng.

---

## 7. Công thức nhanh

**Chỉ ESP32 + cảm biến + loa:**  

- Dòng 12V TB ~**0,2 A**, peak ~**0,4 A**. → Nguồn **12V / 0,5 A (6 W)** trở lên.

**Kiosk có Raspi 5 + tản nhiệt + cảm biến:**  

- Dòng 5V peak ~**2,7 A** (ESP32/loa/cảm biến + Pi 5 + quạt).  
- Một nguồn 12V: **12V / 2,5–3 A (30–36 W)**.  
- Hoặc tách: **12V / 0,5 A** cho board + **5V / 5 A (25 W)** USB-C riêng cho Pi 5.

