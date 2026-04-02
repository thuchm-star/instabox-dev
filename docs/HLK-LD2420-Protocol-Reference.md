# HLK-LD2420 Protocol Reference

## 1. Tổng quan giao thức

HLK-LD2420 giao tiếp qua **UART** với baud rate mặc định **115200**, 8N1. Module hoạt động ở hai chế độ:

- **Normal mode**: module tự gửi dữ liệu text (`ON`, `OFF`, `Range XXX`) hoặc energy frame.
- **Command mode**: gửi lệnh nhị phân (binary) để đọc/ghi cấu hình.

> **Lưu ý**: Phải **mở command mode** trước khi gửi bất kỳ lệnh cấu hình nào, và **đóng command mode** khi hoàn thành.

---

## 2. Cấu trúc Frame

### 2.1 Frame gửi (Send)

```
| Packet Header  | Data Length | Intra-frame Data            | Packet Footer  |
|----------------|-------------|-----------------------------|----------------|
| FD FC FB FA    | 2 bytes LE  | Command (2B) + Params (N*B) | 04 03 02 01    |
```

- **Packet Header**: `FD FC FB FA` (cố định)
- **Data Length**: 2 bytes Little-Endian, tính chiều dài phần *Intra-frame Data*
- **Intra-frame Data**: Command value (2 bytes LE) + parameter data (N bytes)
- **Packet Footer**: `04 03 02 01` (cố định)

### 2.2 Frame nhận (Response)

```
| Packet Header  | Data Length | Return Cmd | Status   | Return Data | Packet Footer  |
|----------------|-------------|------------|----------|-------------|----------------|
| FD FC FB FA    | 2 bytes LE  | 2 bytes    | 2 bytes  | N bytes     | 04 03 02 01    |
```

- **Return Cmd**: command value + `0x01` (ví dụ: gửi `0xFF 0x00` → nhận `0xFF 0x01`)
- **Status**: `00 00` = thành công, giá trị khác = thất bại
- **Return Data**: dữ liệu trả về (nếu có)

> **Byte order**: Tất cả giá trị multi-byte đều theo **Little-Endian** (byte thấp trước).

---

## 3. Bảng lệnh (Command Table)

| Lệnh | Command Value | Data gửi kèm | Mô tả |
|-------|:------------:|---------------|--------|
| Open command mode | `0xFF 0x00` | `01 00` (version, mặc định 01) | Mở chế độ cấu hình |
| Close command mode | `0xFE 0x00` | Không | Đóng chế độ cấu hình |
| Read version | `0x00 0x00` | Không | Đọc firmware version |
| Reboot module | `0x68 0x00` | Không | Khởi động lại module |
| Read parameter(s) | `0x08 0x00` | (param_name 2B) × N | Đọc 1 hoặc nhiều tham số |
| Set parameter(s) | `0x07 0x00` | (param_name 2B + param_value 4B) × N | Ghi 1 hoặc nhiều tham số |

---

## 4. Bảng tham số (Parameter Table)

| Tham số | Param Name | Giá trị (4 bytes) | Phạm vi |
|---------|:----------:|-------------------|---------|
| Min detection distance (gate) | `0x0000` | Số gate tối thiểu | 0 ~ 15 |
| Max detection distance (gate) | `0x0001` | Số gate tối đa | 0 ~ 15 |
| Delay time (giây) | `0x0004` | Thời gian trễ (s) | 0 ~ 65535 |
| Trigger threshold gate 0 | `0x0010` | Ngưỡng kích hoạt | 0 ~ 65535 |
| Trigger threshold gate 1 | `0x0011` | Ngưỡng kích hoạt | 0 ~ 65535 |
| ... | ... | ... | ... |
| Trigger threshold gate 15 | `0x001F` | Ngưỡng kích hoạt | 0 ~ 65535 |
| Maintain threshold gate 0 | `0x0020` | Ngưỡng duy trì | 0 ~ 65535 |
| Maintain threshold gate 1 | `0x0021` | Ngưỡng duy trì | 0 ~ 65535 |
| ... | ... | ... | ... |
| Maintain threshold gate 15 | `0x002F` | Ngưỡng duy trì | 0 ~ 65535 |

### UART output mode (lệnh `0x0012` — ghi system parameter)

Một số tài liệu / triển khai mở (ví dụ ESPHome `ld2420`) mô tả thêm lệnh **ghi tham số hệ thống** để chọn định dạng stream ở normal mode:

| Command | Value | Payload (sau command 2B) | Ý nghĩa |
|---------|:-----:|---------------------------|---------|
| Write system param | `0x0012` | `00 00` + `mode` 2B LE + `00 00` | `mode = 0x0064`: text (`ON`/`OFF`/`Range`); `mode = 0x0004`: khung energy `F4 F3 F2 F1`… |

Phản hồi thành công: return cmd `0x0112`, status `00 00`. Cần **mở command mode** trước và **đóng** sau (giống các lệnh cấu hình khác). Firmware cũ có thể không hỗ trợ — khi đó chỉ còn cách đổi mode trong tool PC của hãng.

**Công thức khoảng cách**: Mỗi gate tương ứng khoảng **0.7 mét**. Ví dụ: max gate = 12 → khoảng cách tối đa ≈ 8.4m.

**Trigger vs Maintain**:
- **Trigger threshold**: Ngưỡng energy để phát hiện có người mới xuất hiện tại gate đó.
- **Maintain threshold**: Ngưỡng energy để duy trì trạng thái "có người" tại gate đó (thường thấp hơn trigger).

---

## 5. Chi tiết từng lệnh

### 5.1 Open Command Mode (`0xFF`)

Bắt buộc gọi trước khi dùng bất kỳ lệnh cấu hình nào.

**Gửi:**
```
FD FC FB FA 04 00 FF 00 01 00 04 03 02 01
```

| Thành phần | Bytes | Giải thích |
|------------|-------|------------|
| Header | `FD FC FB FA` | |
| Length | `04 00` | 4 bytes data |
| Command | `FF 00` | Open command mode |
| Param | `01 00` | Protocol version = 1 |
| Footer | `04 03 02 01` | |

**Phản hồi (thành công):**
```
FD FC FB FA 08 00 FF 01 00 00 02 00 20 00 04 03 02 01
```

| Thành phần | Bytes | Giải thích |
|------------|-------|------------|
| Header | `FD FC FB FA` | |
| Length | `08 00` | 8 bytes data |
| Return cmd | `FF 01` | Phản hồi của lệnh 0xFF |
| Status | `00 00` | Thành công |
| Data | `02 00 20 00` | Thông tin module |
| Footer | `04 03 02 01` | |

**Cách kiểm tra**: byte[8] == `0x00` && byte[9] == `0x00` → thành công.

---

### 5.2 Close Command Mode (`0xFE`)

Trở về normal mode sau khi cấu hình xong. **Luôn gọi lệnh này khi hoàn tất.**

**Gửi:**
```
FD FC FB FA 02 00 FE 00 04 03 02 01
```

**Phản hồi:**
```
FD FC FB FA 04 00 FE 01 00 00 04 03 02 01
```

---

### 5.3 Read Version (`0x00`)

Đọc firmware version string của module.

**Gửi:**
```
FD FC FB FA 02 00 00 00 04 03 02 01
```

**Phản hồi:**
```
FD FC FB FA 0C 00 00 01 00 00 06 00 76 31 2E 35 2E 34 04 03 02 01
```

| Thành phần | Bytes | Giải thích |
|------------|-------|------------|
| Return cmd | `00 01` | Phản hồi của lệnh 0x00 |
| Status | `00 00` | Thành công |
| String len | `06 00` | 6 ký tự |
| Version | `76 31 2E 35 2E 34` | ASCII: `v1.5.4` |

**Parse**: đọc 2 byte length tại offset [10:11], sau đó đọc string tại offset [12 .. 12+length-1].

---

### 5.4 Reboot Module (`0x68`)

Khởi động lại module. Sau khi reboot, module trở lại normal mode.

**Gửi:**
```
FD FC FB FA 02 00 68 00 04 03 02 01
```

> Không cần kiểm tra phản hồi — module sẽ reset ngay.

---

### 5.5 Read Parameter(s) (`0x08`)

Đọc một hoặc nhiều tham số cấu hình.

#### Đọc 1 tham số

**Ví dụ**: Đọc min distance gate (param name = `0x0000`)

**Gửi:**
```
FD FC FB FA 04 00 08 00 00 00 04 03 02 01
```

| Thành phần | Bytes | Giải thích |
|------------|-------|------------|
| Length | `04 00` | 4 bytes (cmd 2B + param_name 2B) |
| Command | `08 00` | Read parameter |
| Param name | `00 00` | Min distance gate |

**Phản hồi:**
```
FD FC FB FA 08 00 08 01 00 00 00 00 00 00 04 03 02 01
```

| Thành phần | Bytes | Giải thích |
|------------|-------|------------|
| Return cmd | `08 01` | |
| Status | `00 00` | Thành công |
| Value | `00 00 00 00` | Gate 0 (LE) |

**Parse giá trị**: 4 bytes LE tại offset [10:13].
```
value = resp[10] | (resp[11] << 8) | (resp[12] << 16) | (resp[13] << 24)
```

#### Đọc nhiều tham số cùng lúc

**Ví dụ**: Đọc trigger threshold gate 0-5 (param `0x10` → `0x15`)

**Gửi:**
```
FD FC FB FA 0E 00 08 00 10 00 11 00 12 00 13 00 14 00 15 00 04 03 02 01
```

| Thành phần | Bytes | Giải thích |
|------------|-------|------------|
| Length | `0E 00` | 14 bytes (cmd 2B + 6 × param_name 2B) |
| Command | `08 00` | Read parameter |
| Params | `10 00 11 00 12 00 13 00 14 00 15 00` | Gate 0~5 trigger |

**Phản hồi:**
```
FD FC FB FA 1C 00 08 01 00 00
60 EA 00 00    ← gate 0: 60000
30 75 00 00    ← gate 1: 30000
B8 0B 00 00    ← gate 2: 3000
D0 07 00 00    ← gate 3: 2000
F4 01 00 00    ← gate 4: 500
90 01 00 00    ← gate 5: 400
04 03 02 01
```

**Parse**: sau status (2 bytes), mỗi 4 bytes LE là giá trị của 1 tham số theo thứ tự yêu cầu.

---

### 5.6 Set Parameter(s) (`0x07`)

Ghi một hoặc nhiều tham số cấu hình.

#### Ghi 1 tham số

**Ví dụ**: Set min distance gate = 0 (param `0x0000`, value `0x00000000`)

**Gửi:**
```
FD FC FB FA 08 00 07 00 00 00 00 00 00 00 04 03 02 01
```

| Thành phần | Bytes | Giải thích |
|------------|-------|------------|
| Length | `08 00` | 8 bytes (cmd 2B + name 2B + value 4B) |
| Command | `07 00` | Set parameter |
| Param name | `00 00` | Min distance gate |
| Param value | `00 00 00 00` | Gate 0 |

**Phản hồi (thành công):**
```
FD FC FB FA 04 00 07 01 00 00 04 03 02 01
```

#### Ghi nhiều tham số cùng lúc

**Ví dụ**: Set trigger threshold gate 0-5

**Gửi:**
```
FD FC FB FA 24 00 07 00
10 00 60 EA 00 00    ← gate 0 = 60000
11 00 30 75 00 00    ← gate 1 = 30000
12 00 B8 0B 00 00    ← gate 2 = 3000
13 00 D0 07 00 00    ← gate 3 = 2000
14 00 F4 01 00 00    ← gate 4 = 500
15 00 90 01 00 00    ← gate 5 = 400
04 03 02 01
```

| Thành phần | Bytes | Giải thích |
|------------|-------|------------|
| Length | `24 00` | 36 bytes (cmd 2B + 6 × (name 2B + value 4B)) |
| Command | `07 00` | Set parameter |
| Mỗi param | `name_lo name_hi val[0] val[1] val[2] val[3]` | 6 bytes/param |

**Phản hồi (thành công):**
```
FD FC FB FA 04 00 07 01 00 00 04 03 02 01
```

---

## 6. Các ví dụ đầy đủ (Full Examples)

### 6.1 Đọc max distance gate

```
Send: FD FC FB FA 04 00 08 00 01 00 04 03 02 01
Recv: FD FC FB FA 08 00 08 01 00 00 0C 00 00 00 04 03 02 01
                                     ^^^^^^^^^^^
                                     0x0000000C = 12 → max gate = 12 (~8.4m)
```

### 6.2 Set max distance gate = 12

```
Send: FD FC FB FA 08 00 07 00 01 00 0C 00 00 00 04 03 02 01
Recv: FD FC FB FA 04 00 07 01 00 00 04 03 02 01  (OK)
```

### 6.3 Đọc delay time

```
Send: FD FC FB FA 04 00 08 00 04 00 04 03 02 01
Recv: FD FC FB FA 08 00 08 01 00 00 1E 00 00 00 04 03 02 01
                                     ^^^^^^^^^^^
                                     0x0000001E = 30 → delay = 30 giây
```

### 6.4 Set delay time = 26 giây

```
Send: FD FC FB FA 08 00 07 00 04 00 1A 00 00 00 04 03 02 01
                                     ^^^^^^^^^^^
                                     0x0000001A = 26
Recv: FD FC FB FA 04 00 07 01 00 00 04 03 02 01  (OK)
```

### 6.5 Đọc maintain threshold gate 0

```
Send: FD FC FB FA 04 00 08 00 20 00 04 03 02 01
Recv: FD FC FB FA 08 00 08 01 00 00 40 9C 00 00 04 03 02 01
                                     ^^^^^^^^^^^
                                     0x00009C40 = 40000
```

### 6.6 Set maintain threshold gate 0 = 50000

```
Send: FD FC FB FA 08 00 07 00 20 00 50 C3 00 00 04 03 02 01
                                     ^^^^^^^^^^^
                                     0x0000C350 = 50000
Recv: FD FC FB FA 04 00 07 01 00 00 04 03 02 01  (OK)
```

### 6.7 Đọc trigger threshold gate 0

```
Send: FD FC FB FA 04 00 08 00 10 00 04 03 02 01
Recv: FD FC FB FA 08 00 08 01 00 00 40 9C 00 00 04 03 02 01
                                     ^^^^^^^^^^^
                                     0x00009C40 = 40000
```

### 6.8 Set trigger threshold gate 0 = 40016

```
Send: FD FC FB FA 08 00 07 00 10 00 50 9C 00 00 04 03 02 01
                                     ^^^^^^^^^^^
                                     0x00009C50 = 40016
Recv: FD FC FB FA 04 00 07 01 00 00 04 03 02 01  (OK)
```

---

## 7. Luồng sử dụng điển hình (Typical Workflow)

```
1. Open command mode    →  0xFF
2. Read version         →  0x00  (tùy chọn)
3. Read/Set parameters  →  0x08 / 0x07
4. Close command mode   →  0xFE
5. (Tùy chọn) Reboot   →  0x68
```

**Pseudocode:**
```c
// Bước 1: Mở command mode
open_command_mode();

// Bước 2: Đọc thông tin
read_version();
read_param(0x0000);  // min gate
read_param(0x0001);  // max gate
read_param(0x0004);  // delay

// Bước 3: Đọc thresholds cho 16 gates
for (gate = 0; gate < 16; gate++) {
    read_param(0x0010 + gate);  // trigger threshold
    read_param(0x0020 + gate);  // maintain threshold
}

// Bước 4: Thay đổi cấu hình (nếu cần)
set_param(0x0001, 10);    // max gate = 10 (~7m)
set_param(0x0004, 15);    // delay = 15s
set_param(0x0010, 50000); // trigger gate 0

// Bước 5: Đóng command mode
close_command_mode();
```

---

## 8. Energy Frame (Normal Mode)

Khi module ở normal mode và được cấu hình ở chế độ energy output, nó sẽ gửi energy frame liên tục:

```
| Header         | Length  | Status (3B) | Gate Data (32B)       | Footer         |
|----------------|---------|-------------|-----------------------|----------------|
| F4 F3 F2 F1    | 2B LE   | 3 bytes     | 16 gates × 2B LE     | F8 F7 F6 F5    |
```

- **Header**: `F4 F3 F2 F1` (khác với command frame)
- **Footer**: `F8 F7 F6 F5`
- **Gate Data**: mỗi gate có 2 bytes Little-Endian biểu diễn energy level
- So sánh energy level với trigger/maintain threshold để xác định có người tại gate đó

---

## 9. Text Output (Normal Mode)

Khi ở normal mode và chế độ text output, module gửi qua UART:

| Output | Ý nghĩa |
|--------|---------|
| `ON` | Phát hiện có người |
| `OFF` | Không phát hiện người |
| `Range XXX` | Có người, khoảng cách XXX cm |

---

## 10. Cách tính byte cho frame

### Tính Data Length

```
Data Length = 2 (command bytes) + N (parameter bytes)
```

**Ví dụ**:
- Lệnh không có param: length = 2 → `02 00`
- Read 1 param: length = 2 + 2 = 4 → `04 00`
- Set 1 param: length = 2 + 2 + 4 = 8 → `08 00`
- Read 6 params: length = 2 + 6×2 = 14 → `0E 00`
- Set 6 params: length = 2 + 6×(2+4) = 38 → `26 00`

### Chuyển đổi Little-Endian

```
Số 60000 (decimal) = 0xEA60 (hex)
Little-Endian: 60 EA 00 00  (4 bytes)

Số 40000 = 0x9C40
Little-Endian: 40 9C 00 00

Số 12 = 0x0C
Little-Endian: 0C 00 00 00
```

---

## 11. Timeout & Error Handling

- **Response timeout**: khuyến nghị **500ms** cho mỗi lệnh
- **Cách nhận biết response hoàn chỉnh**: tìm footer `04 03 02 01` ở cuối data nhận được
- **Kiểm tra thành công**: status bytes (offset [8:9] trong response) == `00 00`
- **Nên flush UART buffer** trước khi gửi lệnh và sau khi đóng command mode
