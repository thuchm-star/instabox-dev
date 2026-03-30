# BÁO CÁO KỸ THUẬT: HỆ THỐNG GIÁM SÁT KIOSK

> **Phiên bản:** 2.7 | **Ngày cập nhật:** Tháng 3, 2025 | **Phân loại:** Nội bộ

---

## Mục lục

1. [Tổng quan hệ thống](#1-tổng-quan-hệ-thống)
2. [Phần cứng đã chốt](#2-phần-cứng-đã-chốt)
3. [Kiến trúc pipeline dữ liệu](#3-kiến-trúc-pipeline-dữ-liệu)
4. [Lớp 1 — Cảm biến kết nối với ESP32](#4-lớp-1--cảm-biến-kết-nối-với-esp32)
5. [Lớp 2 — ESP32 kết nối với Raspberry Pi 5 (6 phương pháp)](#5-lớp-2--esp32-kết-nối-với-raspberry-pi-5)
6. [Định dạng dữ liệu truyền](#6-định-dạng-dữ-liệu-truyền)
7. [Cài đặt môi trường](#7-cài-đặt-môi-trường)
8. [Sơ đồ tổng thể và lưu ý triển khai](#8-sơ-đồ-tổng-thể-và-lưu-ý-triển-khai)
9. [Phương án đề xuất](#9-phương-án-đề-xuất)

---

## 1. Tổng quan hệ thống

Hệ thống giám sát kiosk sử dụng **ESP32 làm trung gian** giữa cảm biến và Raspberry Pi 5. ESP32 đảm nhiệm toàn bộ việc đọc tín hiệu cảm biến real-time, xử lý thô và gửi dữ liệu sạch lên RPi5. RPi5 nhận dữ liệu, thêm ngữ cảnh rồi publish lên MQTT broker.

**Cảm biến đã chốt:**

| Mục đích | Cảm biến | Giao tiếp với ESP32 |
|---|---|---|
| Phát hiện người qua kiosk | LD2420 (mmWave Radar 24GHz) | UART |
| Nhiệt độ & độ ẩm | SHT30 | I2C |
| Trạng thái cửa kiosk | Reed switch (NO/NC) | Digital GPIO |

**Luồng dữ liệu tổng quát:**

```
LD2420 ──────UART──────┐
SHT30 ───────I2C───────┼──► ESP32 ──[UART GPIO]──► RPi5 ──► MQTT Broker
ReedSW ───Digital IO───┘   (JSON/\n @ 115200)      (Python)
```

---

## 2. Phần cứng đã chốt

### 2.1 Cảm biến LD2420 — Phát hiện người

LD2420 là module radar sóng milimet (mmWave) 24GHz của HiLink. Phát hiện sự hiện diện con người kể cả khi **đứng yên hoàn toàn** — điểm vượt trội so với HC-SR04 (chỉ đo khoảng cách vật cản) hay PIR (cần chuyển động nhiệt). Giao tiếp qua **UART**, không cần voltage divider vì hoạt động ở 3.3V.

**Thông số kỹ thuật:**

| Thông số | Giá trị |
|---|---|
| Tần số | 24 GHz |
| Phạm vi phát hiện | 0,75 m – 6 m |
| Phân giải khoảng cách | 0,75 m/vùng |
| Góc phát hiện | 60° |
| Điện áp hoạt động | 3,3V |
| Giao tiếp | UART (TX/RX) + IO |
| Output UART | Có người / vùng đang hoạt động |
| Output IO | HIGH = có người, LOW = không có |
| Nhiệt độ làm việc | -40°C – 85°C |

> ✅ **Không cần voltage divider** — LD2420 hoạt động ở 3.3V, kết nối thẳng vào GPIO ESP32.

> ✅ **Phát hiện người đứng yên** — radar mmWave detect vi chuyển động (nhịp thở, tim đập), không bị "mù" như PIR.

---

### 2.2 Cảm biến SHT30 — Nhiệt độ & Độ ẩm

SHT30 giao tiếp qua I2C, địa chỉ mặc định `0x44`. Điện áp hoạt động 3.3V — kết nối thẳng vào ESP32 không cần level shifter.

### 2.3 Reed switch — Nhận diện cửa đóng/mở

Reed switch là công tắc từ dùng nam châm để nhận biết trạng thái cửa. Kết nối vào chân GPIO input của ESP32, ưu tiên bật `INPUT_PULLUP` để hạn chế nhiễu dây dài.

Quy ước khuyến nghị:
- `door_open = 1`: cửa mở (mạch hở)
- `door_open = 0`: cửa đóng (mạch kín, có nam châm)

---

## 3. Kiến trúc pipeline dữ liệu

```
┌─────────────────────────────────────────────────────────────────┐
│  PHẦN CỨNG                                                       │
│                                                                  │
│  LD2420            SHT30             Reed switch                 │
│  TX ──► GPIO16    SDA ◄── GPIO21     SIG ◄── GPIO34             │
│  RX ◄── GPIO17    SCL ◄── GPIO22     (INPUT_PULLUP)             │
│  (UART1, 256000)  (I2C)                                          │
│           │                │                  │                  │
│           └───────┬────────┴──────────┬───────┘                  │
│                   │                                              │
│               ┌───▼───┐                                          │
│               │ ESP32 │  FreeRTOS, đọc cảm biến real-time       │
│               │       │  Gửi JSON qua UART2 115200 baud         │
│               └───┬───┘                                          │
│                   │ UART GPIO (TX/RX/GND)                       │
│               ┌───▼───────────┐                                  │
│               │ Raspberry Pi 5│  Python serial reader            │
│               │               │  JSON parser                    │
│               │               │  paho-mqtt publisher            │
│               └───┬───────────┘                                  │
│                   │ WiFi / LAN                                   │
│               ┌───▼───────┐                                      │
│               │   MQTT    │  Mosquitto broker                   │
│               │  Broker   │  Topic: kiosk/sensors               │
│               └───────────┘                                      │
└─────────────────────────────────────────────────────────────────┘
```

---

## 4. Lớp 1 — Cảm biến kết nối với ESP32

### 4.1 Sơ đồ nối dây đầy đủ

#### LD2420 → ESP32

| LD2420 | ESP32 | Ghi chú |
|---|---|---|
| VCC | 3.3V | Cùng mức điện áp, không cần level shifter |
| GND | GND | Chung GND |
| TX | GPIO 16 (RX1) | UART1 nhận dữ liệu từ radar |
| RX | GPIO 17 (TX1) | UART1 gửi lệnh cấu hình tới radar |
| OUT | GPIO 4 (tùy chọn) | IO output: HIGH = có người (đọc nhanh không cần parse UART) |

> **Giao thức UART:** LD2420 mặc định baud 256000. Dùng `HardwareSerial(1)` (UART1) trên ESP32 để không xung đột với UART2 (gửi lên RPi5).

#### SHT30 → ESP32

| SHT30 | ESP32 | Ghi chú |
|---|---|---|
| VCC | 3.3V | |
| GND | GND | |
| SDA | GPIO 21 | I2C Data |
| SCL | GPIO 22 | I2C Clock |
| ADDR | GND | Địa chỉ 0x44 (nối VCC để dùng 0x45) |

#### Reed switch → ESP32

| Reed switch | ESP32 | Ghi chú |
|---|---|---|
| COM | GND | Chung mass |
| NO/NC | GPIO 34 | Đọc trạng thái cửa; có thể đổi NO/NC theo logic mong muốn |

---

### 4.2 Code ESP32 với FreeRTOS — Đọc cảm biến và gửi UART

Cài thư viện trước trong Arduino IDE:
- `Adafruit SHT31 Library` (by Adafruit, dùng chung cho SHT30/SHT31)
- `ArduinoJson` (by Benoit Blanchon)
- FreeRTOS: **có sẵn trong ESP32 Arduino core, không cần cài thêm**

ESP32 đã tích hợp sẵn **FreeRTOS**. Tách thành 3 task chạy song song trên 2 core:

- **Task 1 (Core 1):** Đọc UART1 liên tục từ LD2420, parse frame nhị phân → cập nhật shared buffer
- **Task 2 (Core 1):** Đọc I2C từ SHT30 mỗi 2 giây + đọc reed switch định kỳ → cập nhật shared buffer
- **Task 3 (Core 0):** Lấy dữ liệu từ buffer → đóng gói JSON → gửi qua UART2 lên RPi5

Lợi ích: LD2420 gửi frame UART liên tục (baud 256000) — đọc trên Core 1 riêng đảm bảo không bỏ sót frame, trong khi Core 0 lo việc gửi dữ liệu ổn định lên RPi5.

```cpp
#include <Wire.h>
#include <Adafruit_SHT31.h>
#include <ArduinoJson.h>

// ── Pin định nghĩa ───────────────────────────────
#define I2C_SDA        21
#define I2C_SCL        22
#define RADAR_RX_PIN   16   // UART1: nhận từ LD2420 TX
#define RADAR_TX_PIN   17   // UART1: gửi lệnh tới LD2420 RX
#define UART2_RX_PIN   -1   // không dùng RX từ RPi5
#define UART2_TX_PIN   4    // UART2: gửi data lên RPi5
#define RADAR_IO_PIN   5    // IO output của LD2420 (tùy chọn)
#define DOOR_SW_PIN    34   // Reed switch

// ── Serial peripherals ───────────────────────────
HardwareSerial SerialRadar(1);  // UART1 ↔ LD2420, baud 256000
HardwareSerial SerialPi(2);     // UART2 ↔ RPi5,   baud 115200

// ── Shared data + Mutex ─────────────────────────
struct SensorData {
  bool   person;    // LD2420: có người hay không
  uint8_t zone;     // vùng gần nhất có người (1–8, 0 = trống)
  float  temp;      // SHT30
  float  humi;      // SHT30
  bool   door_open; // Reed switch: 1 mở, 0 đóng
};

SensorData        sharedData = {false, 0, 0.0f, 0.0f, false};
SemaphoreHandle_t dataMutex;
Adafruit_SHT31    sht31;

// ── Parse frame LD2420 ───────────────────────────
// LD2420 gửi frame: AA FF 03 00 [8 byte energy] [checksum] FD FC FB FA
// Đây là simplified parser dựa trên datasheet v1.07
bool parseLD2420Frame(uint8_t* buf, int len, bool* presence, uint8_t* nearestZone) {
  // Tìm header AA FF 03 00
  for (int i = 0; i <= len - 12; i++) {
    if (buf[i] == 0xAA && buf[i+1] == 0xFF && buf[i+2] == 0x03 && buf[i+3] == 0x00) {
      // Byte 4: presence flag (bit 0 = có người)
      *presence    = (buf[i+4] & 0x01);
      // Byte 5: nearest zone (0 = none, 1-8 = vùng 0.75m/zone)
      *nearestZone = buf[i+5];
      return true;
    }
  }
  return false;
}

// ── Task 1: Đọc UART từ LD2420 (Core 1) ─────────
void taskReadRadar(void* pvParams) {
  uint8_t frameBuf[32];
  int     frameIdx = 0;

  for (;;) {
    while (SerialRadar.available()) {
      uint8_t b = SerialRadar.read();
      if (frameIdx < (int)sizeof(frameBuf))
        frameBuf[frameIdx++] = b;

      // Frame kết thúc bằng FD FC FB FA
      if (frameIdx >= 4 &&
          frameBuf[frameIdx-4] == 0xFD && frameBuf[frameIdx-3] == 0xFC &&
          frameBuf[frameIdx-2] == 0xFB && frameBuf[frameIdx-1] == 0xFA) {

        bool    pres; uint8_t zone;
        if (parseLD2420Frame(frameBuf, frameIdx, &pres, &zone)) {
          if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            sharedData.person = pres;
            sharedData.zone   = zone;
            xSemaphoreGive(dataMutex);
          }
        }
        frameIdx = 0; // reset buffer
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10)); // yield 10ms
  }
}

// ── Task 2: Đọc SHT30 + reed switch (Core 1) ────
void taskReadSHT31(void* pvParams) {
  uint32_t lastShtReadMs = 0;
  for (;;) {
    bool doorOpen = digitalRead(DOOR_SW_PIN) == HIGH;

    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      sharedData.door_open = doorOpen;
      xSemaphoreGive(dataMutex);
    }

    if (millis() - lastShtReadMs >= 2000) {
      lastShtReadMs = millis();
      float t = sht31.readTemperature();
      float h = sht31.readHumidity();

      if (!isnan(t) && !isnan(h) &&
          xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        sharedData.temp = t;
        sharedData.humi = h;
        xSemaphoreGive(dataMutex);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(100)); // reed 10Hz, SHT30 mỗi 2 giây
  }
}

// ── Task 3: Đóng gói JSON & Gửi UART2 (Core 0) ──
void taskSendData(void* pvParams) {
  StaticJsonDocument<128> doc;
  char buf[128];

  for (;;) {
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      doc["person"] = sharedData.person;
      doc["zone"]   = sharedData.zone;
      doc["temp"]   = round(sharedData.temp * 10) / 10.0;
      doc["humi"]   = round(sharedData.humi * 10) / 10.0;
      doc["door_open"] = sharedData.door_open;
      doc["ts"]     = millis();
      xSemaphoreGive(dataMutex);
    }

    serializeJson(doc, buf, sizeof(buf));
    SerialPi.println(buf);  // gửi qua UART2 → RPi5

    vTaskDelay(pdMS_TO_TICKS(500)); // gửi mỗi 500ms
  }
}

// ── Setup ────────────────────────────────────────
void setup() {
  Wire.begin(I2C_SDA, I2C_SCL);
  SerialRadar.begin(256000, SERIAL_8N1, RADAR_RX_PIN, RADAR_TX_PIN);
  SerialPi.begin(115200, SERIAL_8N1, UART2_RX_PIN, UART2_TX_PIN);

  pinMode(RADAR_IO_PIN, INPUT);
  pinMode(DOOR_SW_PIN, INPUT_PULLUP);
  sht31.begin(0x44);

  dataMutex = xSemaphoreCreateMutex();

  // 3 task trên 2 core
  xTaskCreatePinnedToCore(taskReadRadar, "ReadRadar", 4096, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(taskReadSHT31, "ReadSHT31", 2048, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(taskSendData,  "SendData",  4096, NULL, 1, NULL, 0);
}

void loop() {
  // Trống — FreeRTOS quản lý toàn bộ
}
```

> **FreeRTOS đã có sẵn trong ESP32 Arduino core** — không cần cài thêm. Hàm `xTaskCreatePinnedToCore()`, `xSemaphoreCreateMutex()`, `vTaskDelay()` đều dùng được ngay.

> **UART phân lớp:** UART1 (baud 256000) đọc LD2420, UART2 (baud 115200) gửi lên RPi5. Hai luồng serial hoàn toàn độc lập.

**Ví dụ output qua UART2 lên RPi5:**
```json
{"person":true,"zone":2,"temp":28.5,"humi":65.2,"door_open":false,"ts":12450}
{"person":false,"zone":0,"temp":28.5,"humi":65.1,"door_open":true,"ts":12952}
```

> `zone`: vùng gần nhất có người (0 = không có, 1 = 0–0.75m, 2 = 0.75–1.5m, ..., 8 = 5.25–6m)

---

## 5. Lớp 2 — ESP32 kết nối với Raspberry Pi 5

Lớp 2 sử dụng kết nối **có dây** giữa ESP32 và RPi5. Có 4 phương pháp, mỗi phương pháp phù hợp với một kịch bản lắp đặt khác nhau. **Phương án đề xuất: UART GPIO** — không chiếm nguồn USB của RPi5, kết nối gọn trong vỏ kiosk.

### Bảng so sánh nhanh

| # | Phương pháp | Dây vật lý | Tốc độ | Độ khó | Phù hợp khi |
|---|---|---|---|---|---|
| 1 | **USB Serial** | Cáp USB | ~1 Mbps | ⭐ Thấp | Kiosk cố định, ưu tiên đơn giản |
| 2 | **UART GPIO** | 2–3 dây | ~1 Mbps | ⭐⭐ Thấp | Muốn tiết kiệm cổng USB |
| 3 | **SPI** | 4–5 dây | ~80 Mbps | ⭐⭐⭐ Cao | Truyền lượng lớn dữ liệu liên tục |
| 4 | **I2C** | 2 dây | ~400 kbps | ⭐⭐ Thấp | ESP32 là slave, ít dùng |

---

### 5.1 USB Serial

ESP32 DevKit cắm thẳng vào cổng USB của RPi5. RPi5 tự nhận driver `cp210x` hoặc `ch341`. ESP32 lấy luôn nguồn từ USB — không cần nguồn riêng.

**Kết nối:**
```
ESP32  ──[Micro-USB / USB-C cable]──  RPi5 USB port
```

**Ưu điểm:** Không cần level shifter, không cần cấu hình thêm, dễ debug bằng `screen` hoặc `minicom`, nguồn điện ổn định từ USB.

**Nhược điểm:** Cần cáp USB vật lý, chiếm 1 cổng USB của RPi5.

**Xác định cổng trên RPi5:**

```bash
ls /dev/ttyUSB* /dev/ttyACM*    # thường là /dev/ttyUSB0
dmesg | tail -5                  # kiểm tra driver nhận
screen /dev/ttyUSB0 115200       # xem raw output, Ctrl+A K để thoát
```

**Cố định tên cổng bằng udev rule:**

```bash
# Lấy vendor/product ID
udevadm info -a /dev/ttyUSB0 | grep -E "idVendor|idProduct" | head -2

# Tạo rule
sudo nano /etc/udev/rules.d/99-esp32.rules
```
```
SUBSYSTEM=="tty", ATTRS{idVendor}=="10c4", ATTRS{idProduct}=="ea60", SYMLINK+="esp32", MODE="0666"
```
```bash
sudo udevadm control --reload-rules && sudo udevadm trigger
# Từ giờ luôn dùng /dev/esp32
```

**Code ESP32** — dùng `Serial.println()` gửi JSON (xem Section 4.2).

**Code RPi5 Python:**
```python
import serial, json
ser = serial.Serial('/dev/esp32', 115200, timeout=2)
while True:
    line = ser.readline().decode('utf-8').strip()
    if line:
        data = json.loads(line)
        print(data)
```

---

### 5.2 UART GPIO (TX/RX trực tiếp) ✅ Đề xuất

Nối thẳng chân TX/RX giữa ESP32 và header GPIO của RPi5. Cả hai đều 3.3V — **không cần level shifter**.

**Sơ đồ nối dây:**

```
ESP32                    RPi5 GPIO Header
GPIO17 (TX2) ──────────► Pin 10 (GPIO15 / RX)
GPIO16 (RX2) ◄────────── Pin  8 (GPIO14 / TX)
GND          ──────────── Pin  6 (GND)
```

> ⚠️ TX nối RX, RX nối TX (chéo nhau). ESP32 cần nguồn **riêng** vì không lấy từ USB RPi5.

> ⚠️ **Bắt buộc tắt Serial Console RPi5** trước khi dùng:
> ```bash
> sudo raspi-config
> # Interface Options → Serial Port
> # Login shell over serial? → No
> # Serial hardware enabled?  → Yes
> ```

**Code ESP32** — xem Section 4.2 (dùng `SerialPi` = UART2 `HardwareSerial(2)`). Toàn bộ logic đọc cảm biến và FreeRTOS đã được tích hợp sẵn ở đó, gửi qua `SerialPi.println(buf)` thay vì `Serial.println()`.

Tóm tắt thay đổi duy nhất so với USB Serial: thay `Serial` → `SerialPi` (UART2):

```cpp
HardwareSerial SerialPi(2);
// Trong setup():
SerialPi.begin(115200, SERIAL_8N1, 16, 17); // RX=GPIO16, TX=GPIO17
// Trong taskSendData():
SerialPi.println(buf);  // thay vì Serial.println(buf)
```

**Code RPi5 Python** — đọc `/dev/ttyAMA0`:

```python
import serial, json

ser = serial.Serial('/dev/ttyAMA0', 115200, timeout=2)

while True:
    line = ser.readline().decode('utf-8', errors='ignore').strip()
    if line:
        try:
            data = json.loads(line)
            print(data)
        except json.JSONDecodeError:
            pass
```

**Ưu điểm:** Không chiếm nguồn USB RPi5, kết nối gọn trong vỏ kiosk bằng 3 dây, ESP32 dùng nguồn riêng ổn định.

**Nhược điểm:** Phải có nguồn cấp riêng cho ESP32 (5V adapter hoặc pin), cần tắt Serial Console RPi5 một lần.

---

### 5.3 SPI (Master RPi5 / Slave ESP32)

RPi5 làm SPI Master, ESP32 làm SPI Slave. RPi5 chủ động poll dữ liệu từ ESP32 theo lịch hoặc theo trigger. Phù hợp khi cần throughput cao hoặc muốn RPi5 kiểm soát thời điểm đọc.

**Sơ đồ nối dây:**

```
ESP32                    RPi5 GPIO Header
GPIO23 (MOSI) ◄────────── Pin 19 (GPIO10 / MOSI)
GPIO19 (MISO) ──────────► Pin 21 (GPIO9  / MISO)
GPIO18 (SCLK) ◄────────── Pin 23 (GPIO11 / SCLK)
GPIO5  (CS)   ◄────────── Pin 24 (GPIO8  / CE0)
GND           ──────────── Pin  6 (GND)
```

> ⚠️ Cả hai đều 3.3V — không cần level shifter. ESP32 cần nguồn riêng.

**Code ESP32** — SPI Slave nhận lệnh, trả về JSON:

```cpp
#include <SPI.h>
#include <ArduinoJson.h>

char spi_buf[128];

void setup() {
  // ESP32 SPI Slave mode
  pinMode(SS, INPUT);
  SPI.begin();
  // Dùng thư viện ESP32SPISlave nếu cần slave mode đầy đủ
}

// Chuẩn bị buffer gửi khi RPi5 poll
void prepareSPIBuffer() {
  StaticJsonDocument<128> doc;
  doc["person"] = sharedData.person;
  doc["zone"]   = sharedData.zone;
  serializeJson(doc, spi_buf, sizeof(spi_buf));
}
```

**Code RPi5 Python** — dùng thư viện `spidev`:

```bash
pip3 install spidev
```

```python
import spidev
import json
import time

spi = spidev.SpiDev()
spi.open(0, 0)          # bus 0, device 0 (CE0)
spi.max_speed_hz = 500000
spi.mode = 0

def read_esp32_spi():
    # Gửi 128 byte 0x00 để clock data ra từ ESP32
    raw = spi.xfer2([0x00] * 128)
    text = ''.join(chr(b) for b in raw if b != 0).strip()
    return json.loads(text) if text else None

while True:
    data = read_esp32_spi()
    if data:
        print(data)
    time.sleep(0.5)
```

**Ưu điểm:** Tốc độ rất cao, RPi5 kiểm soát hoàn toàn thời điểm đọc.

**Nhược điểm:** Lập trình phức tạp nhất, cần thư viện SPI Slave riêng cho ESP32, dễ lỗi timing. **Không cần thiết cho cảm biến LD2420 + SHT30 + reed switch.**

---

### 5.4 I2C (RPi5 Master / ESP32 Slave)

RPi5 làm I2C Master, ESP32 đóng vai I2C Slave với một địa chỉ cố định. RPi5 đọc dữ liệu khi cần. Phù hợp khi đã có bus I2C sẵn và muốn gắn thêm ESP32 vào đó.

**Sơ đồ nối dây:**

```
ESP32                    RPi5 GPIO Header
GPIO21 (SDA) ◄──[4.7kΩ pullup 3.3V]──► Pin 3 (GPIO2 / SDA)
GPIO22 (SCL) ◄──[4.7kΩ pullup 3.3V]──► Pin 5 (GPIO3 / SCL)
GND          ──────────────────────────── Pin 6 (GND)
```

> ⚠️ Cần **resistor pullup 4.7kΩ** trên cả SDA và SCL lên 3.3V nếu chưa có sẵn trên board.

**Code ESP32** — đăng ký làm I2C Slave địa chỉ `0x10`:

```cpp
#include <Wire.h>
#include <ArduinoJson.h>

#define I2C_SLAVE_ADDR 0x10
char response[64];

void onRequest() {
  Wire.write((uint8_t*)response, strlen(response));
}

void setup() {
  Wire.begin(I2C_SLAVE_ADDR, 21, 22); // addr, SDA, SCL
  Wire.onRequest(onRequest);
  updateResponse();
}

void updateResponse() {
  StaticJsonDocument<64> doc;
  doc["dist_cm"] = measureDistance();
  doc["person"]  = doc["dist_cm"].as<float>() < 120.0;
  serializeJson(doc, response, sizeof(response));
}

void loop() {
  updateResponse();
  delay(200);
}
```

**Code RPi5 Python** — dùng `smbus2`:

```bash
pip3 install smbus2
```

```python
from smbus2 import SMBus
import json

ESP32_ADDR = 0x10

with SMBus(1) as bus:
    while True:
        raw = bus.read_i2c_block_data(ESP32_ADDR, 0, 64)
        text = bytes(raw).decode('utf-8', errors='ignore').strip('\x00').strip()
        if text:
            try:
                data = json.loads(text)
                print(data)
            except json.JSONDecodeError:
                pass
```

**Ưu điểm:** Chỉ cần 2 dây tín hiệu, có thể gắn nhiều slave trên cùng bus.

**Nhược điểm:** Tốc độ thấp (400 kbps), ESP32 Slave mode ít được hỗ trợ chính thức, dễ bị lỗi khi buffer không đồng bộ. **Ít dùng trong thực tế.**

---

### 5.5 So sánh chi tiết và hướng dẫn chọn phương pháp

| Tiêu chí | USB Serial | UART GPIO | SPI | I2C |
|---|---|---|---|---|
| **Cần cáp / dây** | Cáp USB | 2–3 dây | 4–5 dây | 2 dây |
| **Nguồn cho ESP32** | Từ USB RPi5 | Cần riêng | Cần riêng | Cần riêng |
| **Cần level shifter** | Không | Không | Không | Không |
| **Độ trễ** | ~1ms | ~1ms | <0.1ms | ~5ms |
| **Hoạt động khi mất WiFi** | ✅ Có | ✅ Có | ✅ Có | ✅ Có |
| **Khoảng cách tối đa** | ~3m (cáp USB) | ~1m (dây GPIO) | ~20cm | ~1m |
| **Tiêu thụ điện ESP32** | ~80mA | ~80mA | ~80mA | ~80mA |
| **Phức tạp code ESP32** | ⭐ | ⭐⭐ | ⭐⭐⭐⭐ | ⭐⭐⭐ |
| **Phức tạp code RPi5** | ⭐ | ⭐ | ⭐⭐⭐⭐ | ⭐⭐⭐ |
| **Dễ debug** | ✅ Cao | ✅ Cao | ❌ Thấp | ❌ Thấp |
| **Độ ổn định** | ✅ Rất cao | ✅ Cao | ✅ Cao | ⚠️ Trung bình |

**Chọn theo kịch bản thực tế:**

| Kịch bản | Phương pháp phù hợp | Lý do |
|---|---|---|
| ESP32 đặt ngay cạnh RPi5 trong vỏ kiosk | **USB Serial** hoặc **UART GPIO** | Đơn giản, không phụ thuộc WiFi |
| Môi trường WiFi không ổn định / không có | **USB Serial** hoặc **UART GPIO** | Luôn hoạt động độc lập với network |
| Muốn debug dễ nhất, không quen embedded | **USB Serial** | Dùng `screen` / `minicom` xem trực tiếp |

## 6. Định dạng dữ liệu truyền — Phân tích & So sánh

Khi ESP32 truyền dữ liệu lên RPi5, có nhiều cách đóng gói dữ liệu. Mỗi cách có đánh đổi riêng về kích thước, tốc độ xử lý, độ dễ debug và khả năng mở rộng.

**Dữ liệu cần truyền (5 trường):**

| Trường | Ví dụ | Phạm vi | Độ phân giải cần |
|---|---|---|---|
| Có người | `1` / `0` | 0 hoặc 1 | 1 bit |
| Zone gần nhất | `0`–`8` | 0 = không có, 1–8 = vùng 0.75m/bước | 1 chữ số |
| Nhiệt độ | `28.5` °C | -40 ~ +125°C | 0.1°C |
| Độ ẩm | `65.2` %RH | 0 – 100% | 0.1% |
| Cửa mở | `1` / `0` | 0 = đóng, 1 = mở | 1 bit |

---

### 6.1 Compact String — Chuỗi số ghép liền

**Ý tưởng:** Encode tất cả trường thành một chuỗi chữ số liền, không dấu phân cách, không key. Bên nhận cắt theo vị trí cố định đã thỏa thuận trước.

**Ví dụ đề xuất ban đầu:** `137462901` = `1` (có người) + `374` (khoảng cách) + `629` (nhiệt độ) + `01` (độ ẩm).

Ý tưởng đúng hướng nhưng cần xem xét kỹ phần encoding để tránh mất dữ liệu:

**Vấn đề #1 — Độ phân giải khoảng cách:**
LD2420 không cho khoảng cách chính xác mà cho **zone** (vùng 0–8). Trường khoảng cách thay bằng `zone` 1 chữ số (0–8) — rất nhỏ gọn, không cần nhiều chữ số.

**Vấn đề #2 — Nhiệt độ âm:**
SHT30 đo được -40°C. Chuỗi số không thể chứa dấu trừ. Giải pháp: dùng **offset +400** trước khi nhân 10: `-40°C → (−40+40)×10 = 000`, `+28.5°C → (28.5+40)×10 = 685`. Cần 4 chữ số cho range đủ (-40 → 0000, +125 → 1650).

**Vấn đề #3 — Độ ẩm 100%:**
`100.0%` → `1000` cần 4 chữ số. Thực tế SHT30 hiếm khi đo chính xác 100%, có thể dùng 3 chữ số cho `0–999` (tức 0.0–99.9%), giá trị 100% encode thành `999`.

**Format khả thi sau khi giải quyết:**

```
[P][Z][TTTT][HHH]  =  9 ký tự
 │  │   │    └── Độ ẩm × 10, 3 chữ số → 652 = 65.2%
 │  │   └──────── Nhiệt độ: (temp + 40) × 10, 4 chữ số → 0685 = 28.5°C
 │  └────────────── Zone LD2420: 1 chữ số → 0–8
 └────────────────── Người: 1 chữ số → 1 = có người
```

**Ví dụ thực tế:** `1084068565.2` → viết liền: `10840685652`

**Code ESP32 encode:**

```cpp
void sendCompact(bool person, uint8_t zone, float temp, float humi) {
  int P = person ? 1 : 0;
  int Z = constrain((int)zone, 0, 8);                    // 0–8
  int T = constrain((int)((temp + 40.0) * 10), 0, 9999); // offset+40, 0.1°C
  int H = constrain((int)(humi * 10), 0, 999);            // 0.1%

  char buf[12];
  snprintf(buf, sizeof(buf), "%1d%1d%04d%03d", P, Z, T, H);
  SerialPi.println(buf);  // 9 ký tự + \n
}

// Gọi:
sendCompact(true, 2, 28.5, 65.2);
// Output: "120685652"
// Decode: P=1, Z=2, T=0685→(685/10)-40=28.5°C, H=652→65.2%
```

**Code RPi5 decode:**

```python
def decode_compact(s):
    s = s.strip()
    if len(s) != 9:
        raise ValueError(f"Độ dài không hợp lệ: {len(s)}, cần 9")
    return {
        "person": int(s[0]),
        "zone":   int(s[1]),
        "temp":   int(s[2:6]) / 10.0 - 40.0,
        "humi":   int(s[6:9]) / 10.0,
    }

# Test
print(decode_compact("120685652"))
# {'person': 1, 'zone': 2, 'temp': 28.5, 'humi': 65.2}
```

**Ưu điểm:**
- Nhỏ gọn: 11 byte thay vì ~62 byte JSON → tiết kiệm ~82% băng thông
- Không cần thư viện parse, chỉ dùng string slicing thuần
- Phù hợp khi truyền qua kênh tốc độ thấp: LoRa, 433MHz, BLE, SMS

**Nhược điểm:**
- Khó đọc khi debug: `10840685652` không tự nói lên gì nếu không có tài liệu
- Dễ lỗi khi thêm/bỏ trường — phải sửa cả encoder lẫn decoder đồng thời
- Không tự mô tả (non-self-describing): không thể biết cấu trúc chỉ từ chuỗi
- Giá trị out-of-range bị `constrain()` cắt ngầm, không có cảnh báo
- Nếu mất 1 ký tự giữa chừng → toàn bộ frame lệch, không phát hiện được

---

### 6.2 JSON (Text)

Format key-value tiêu chuẩn, tự mô tả, dễ đọc.

```json
{"person":1,"zone":2,"temp":28.5,"humi":65.2,"door_open":0,"ts":12450}
```

Kích thước: ~55 byte.

**Code ESP32:**
```cpp
#include <ArduinoJson.h>

StaticJsonDocument<128> doc;
doc["person"] = person ? 1 : 0;
doc["zone"]   = zone;
doc["temp"]   = temp;
doc["humi"]   = humi;
doc["door_open"] = door_open ? 1 : 0;
doc["ts"]     = millis();
serializeJson(doc, Serial);
Serial.println();
```

**Code RPi5:**
```python
import json
data = json.loads(line)  # một dòng là xong
```

**Ưu điểm:** Dễ đọc khi debug, tự mô tả, thêm trường không cần sửa decoder, tương thích với mọi hệ thống downstream.

**Nhược điểm:** Kích thước lớn hơn compact ~5×, cần thư viện ArduinoJson trên ESP32 (~30KB flash).

---

### 6.3 CSV (Comma-Separated Values)

Dùng dấu phẩy phân cách, không có key. Nhỏ hơn JSON, lớn hơn compact string.

```
1,2,28.5,65.2,12450
```

Kích thước: ~19 byte.

**Code ESP32:**
```cpp
Serial.printf("%d,%d,%.1f,%.1f,%lu\n",
              person ? 1 : 0, zone, temp, humi, millis());
```

**Code RPi5:**
```python
parts = line.split(',')
data = {
    "person": int(parts[0]),
    "zone":   int(parts[1]),
    "temp":   float(parts[2]),
    "humi":   float(parts[3]),
}
```

**Ưu điểm:** Nhỏ gọn, không cần thư viện, dễ import vào Excel/spreadsheet, đọc được bằng mắt.

**Nhược điểm:** Phụ thuộc thứ tự cột — thêm/đổi thứ tự trường phải sửa cả hai đầu, không tự mô tả.

---

### 6.4 Binary Pack (struct)

Đóng gói trực tiếp thành byte theo cấu trúc C struct. Không có overhead text, kích thước tối thiểu tuyệt đối.

**Thiết kế struct (8 byte):**

```
Byte 0:   uint8  person      (0 hoặc 1)
Byte 1:   uint8  zone        (0–8)
Byte 2–3: int16  temp × 10   (°C × 10, hỗ trợ âm)
Byte 4–5: uint16 humi × 10   (% × 10, 0–1000)
Byte 6:   uint8  checksum    (XOR byte 0–5)
```
Kích thước: **7 byte** (nhỏ hơn phiên bản HC-SR04 1 byte do bỏ trường dist).

**Code ESP32:**
```cpp
#pragma pack(1)
struct SensorPacket {
  uint8_t  person;
  uint8_t  zone;
  int16_t  temp_x10;
  uint16_t humi_x10;
  uint8_t  checksum;
};

void sendBinary(bool person, uint8_t zone, float temp, float humi) {
  SensorPacket pkt;
  pkt.person   = person ? 1 : 0;
  pkt.zone     = zone;
  pkt.temp_x10 = (int16_t)(temp * 10);
  pkt.humi_x10 = (uint16_t)(humi * 10);

  uint8_t* p = (uint8_t*)&pkt;
  uint8_t cs = 0;
  for (int i = 0; i < 6; i++) cs ^= p[i];
  pkt.checksum = cs;

  SerialPi.write((uint8_t*)&pkt, sizeof(pkt));  // gửi 7 byte thô
}
```

**Code RPi5:**
```python
import struct

FMT  = '<BBhHB'                  # little-endian: uint8, uint8, int16, uint16, uint8
SIZE = struct.calcsize(FMT)      # = 7 bytes

def read_binary_packet(ser):
    raw = ser.read(SIZE)
    if len(raw) < SIZE:
        return None
    person, zone, temp_x10, humi_x10, cs = struct.unpack(FMT, raw)

    # Kiểm tra checksum
    calc = 0
    for b in raw[:6]:
        calc ^= b
    if calc != cs:
        return None  # frame bị lỗi

    return {
        "person": person,
        "zone":   zone,
        "temp":   temp_x10 / 10.0,
        "humi":   humi_x10 / 10.0,
    }
```

**Ưu điểm:** Nhỏ nhất (8 byte), nhanh nhất, có checksum phát hiện lỗi truyền, hỗ trợ nhiệt độ âm tự nhiên qua `int16_t`.

**Nhược điểm:** Hoàn toàn không đọc được bằng mắt, đồng bộ frame khó (mất 1 byte → toàn bộ lệch), cần header magic byte nếu dùng Serial.

---

### 6.5 So sánh tổng hợp các định dạng

| Tiêu chí | Compact String | CSV | JSON | Binary Pack |
|---|---|---|---|---|
| **Kích thước** | 11 byte | ~22 byte | ~62 byte | 8 byte |
| **Đọc được bằng mắt** | ⚠️ Khó | ✅ Được | ✅ Rõ ràng | ❌ Không |
| **Tự mô tả** | ❌ Không | ❌ Không | ✅ Có | ❌ Không |
| **Hỗ trợ nhiệt độ âm** | ⚠️ Cần offset | ✅ Có | ✅ Có | ✅ Có (int16) |
| **Phát hiện lỗi** | ❌ Không | ❌ Không | ❌ Không | ✅ Checksum XOR |
| **Dễ thêm trường** | ❌ Phải sửa 2 đầu | ❌ Phải sửa 2 đầu | ✅ Tự do | ❌ Phải sửa 2 đầu |
| **Cần thư viện** | Không | Không | ArduinoJson | `struct` Python |
| **Phù hợp nhất** | LoRa / radio tốc độ thấp | Log file, Excel | Serial / UART | Cần tối ưu tuyệt đối |

---

### 6.6 Đánh giá và khuyến nghị

**Về ý tưởng Compact String cho hệ thống này:**

Ý tưởng hoàn toàn đúng về kỹ thuật và rất phù hợp cho các kênh băng thông thấp. Tuy nhiên cần cân nhắc kỹ hai điểm:

Với kết nối USB Serial hoặc UART tốc độ **115200 baud**, bandwidth tối đa là ~11.5KB/s. JSON 62 byte gửi mỗi 500ms chỉ chiếm **124 byte/s = ~1% bandwidth** — hoàn toàn không phải vấn đề. Lợi thế compact chỉ thực sự có giá trị khi dùng **LoRa** (~250 bps), **433MHz** (~1200 bps) hoặc **BLE** (gói 20 byte/lần).

| Kịch bản | Định dạng khuyến nghị |
|---|---|
| USB Serial / UART — giai đoạn phát triển | **JSON** — dễ debug, dễ mở rộng |
| USB Serial / UART — production ổn định, không cần thêm trường | **CSV** — nhỏ gọn vừa phải, vẫn đọc được |
| Cần thêm module LoRa / 433MHz sau này | **Compact String** — tối ưu băng thông |
| Cần độ tin cậy cao, phát hiện lỗi truyền | **Binary Pack** — checksum, kích thước tối thiểu |

---

### 6.7 Frame JSON đầy đủ (giai đoạn hiện tại)

**ESP32 → RPi5:**
```json
{"person":1,"zone":2,"temp":28.5,"humi":65.2,"ts":12450}
```

**RPi5 → MQTT (sau khi thêm metadata):**
```json
{
  "person": 1,
  "zone": 2,
  "temp": 28.5,
  "humi": 65.2,
  "ts": 12450,
  "kiosk_id": "kiosk_01",
  "rx_ts": 1711180802
}
```

| Trường | Nguồn | Mô tả |
|---|---|---|
| `person` | ESP32 | `1` có người, `0` không có |
| `zone` | ESP32 | Vùng gần nhất có người (0 = không ai, 1–8 = bước 0.75m) |
| `temp` | ESP32 | Nhiệt độ SHT30 (°C) |
| `humi` | ESP32 | Độ ẩm SHT30 (%RH) |
| `door_open` | ESP32 | `1` cửa mở, `0` cửa đóng (reed switch) |
| `ts` | ESP32 | millis() từ lúc boot (ms) |
| `kiosk_id` | RPi5 | ID thiết bị kiosk |
| `rx_ts` | RPi5 | Unix timestamp khi RPi5 nhận (giây) |

---

### 6.8 MQTT Topic structure

```
kiosk/sensors                ← topic hiện tại
kiosk/{kiosk_id}/sensors     ← mở rộng khi có nhiều kiosk
kiosk/{kiosk_id}/status      ← heartbeat / trạng thái kết nối
```

---

## 7. Cài đặt môi trường

### 7.1 RPi5 — Cài Mosquitto MQTT Broker

```bash
sudo apt update && sudo apt install -y mosquitto mosquitto-clients
sudo systemctl enable mosquitto
sudo systemctl start mosquitto

# Test
mosquitto_sub -h localhost -t "kiosk/#" -v &
mosquitto_pub -h localhost -t "kiosk/test" -m "hello"
```

### 7.2 RPi5 — Cài thư viện Python

```bash
pip3 install pyserial paho-mqtt
```

### 7.3 ESP32 — Arduino IDE setup

```
Board Manager   → Tìm "esp32" → Cài "ESP32 by Espressif Systems"
Library Manager → Cài "Adafruit SHT31 Library" (dùng cho SHT30)
Library Manager → Cài "ArduinoJson" (by Benoit Blanchon)
# LD2420: không cần thư viện riêng — parse frame UART thủ công theo datasheet

Upload settings:
  Board: ESP32 Dev Module
  Upload Speed: 921600
```

---

## 8. Sơ đồ tổng thể và lưu ý triển khai

### 8.1 Sơ đồ nối dây hoàn chỉnh

```
                    ┌─────────────────────────────┐
                    │         ESP32 DevKit         │
                    │                              │
  LD2420             │                              │
  ┌────────┐         │  GPIO16 ◄──── TX             │
  │  VCC   │──3.3V──►│  3V3   (UART1 @256000)      │
  │  GND   │──GND───►│  GND                         │
  │  TX    │────────►│  GPIO16 (RX1)                │
  │  RX    │◄────────│  GPIO17 (TX1)                │
  │  OUT   │────────►│  GPIO5  (IO: HIGH=người)     │
  └────────┘         │                              │
                                                    │
  SHT30              │                              │
  ┌────────┐         │                              │
  │  VCC   │──3.3V──►│  3V3                         │
  │  GND   │──GND───►│  GND                         │
  │  SDA   │◄────────│  GPIO21                      │
  │  SCL   │◄────────│  GPIO22                      │
  │  ADDR  │──GND    │                              │
  └────────┘         │                              │
                                                   │
  Reed SW            │                              │
  ┌────────┐         │                              │
  │  COM   │──GND───►│  GND                         │
  │  NO    │────────►│  GPIO34 (INPUT_PULLUP)       │
  └────────┘         │                              │
                     │   UART GPIO (TX/RX/GND)      │
                     └──────────┬───────────────────┘
                                │
                     ┌──────────▼─────────────────┐
                     │      Raspberry Pi 5         │
                     │   /dev/ttyAMA0 @ 115200     │
                     │   kiosk_reader.py (Python)  │
                     └──────────┬─────────────────┘
                                │ localhost:1883
                     ┌──────────▼──────┐
                     │ Mosquitto MQTT  │
                     │ kiosk/sensors   │
                     └─────────────────┘
```

---

### 8.2 Lưu ý triển khai thực tế

**Vị trí đặt LD2420:** Gắn trên kiosk, hướng thẳng ra phía người dùng. Khoảng cách lý tưởng 0.5–3m tính từ màn hình. Zone 1–3 (0–2.25m) thường là vùng tương tác thực tế — cấu hình sensitivity theo datasheet để tối ưu.

**Tinh chỉnh LD2420:** Module hỗ trợ cấu hình độ nhạy từng zone qua UART. Có thể dùng công cụ HiLink để set lần đầu, sau đó cố định cấu hình trong firmware ESP32.

**Nguồn riêng ESP32:** Cấp 5V từ adapter riêng, nối GND chung với RPi5 qua dây UART. Không lấy 5V từ USB RPi5 để tránh ảnh hưởng nguồn.

**UART ttyAMA0 RPi5:** Cổng `/dev/ttyAMA0` ổn định hơn `/dev/ttyUSB0` vì là hardware UART, không qua USB. Cần tắt Serial Console một lần qua `raspi-config`.

**MQTT QoS=1:** paho-mqtt buffer message khi mất kết nối broker và gửi lại khi phục hồi. Không mất dữ liệu trong khoảng gián đoạn ngắn.

---


---

## 9. Phương án đề xuất

Dựa trên phân tích toàn bộ các phương pháp kết nối và định dạng dữ liệu, phần này đưa ra đề xuất cụ thể theo hai kịch bản triển khai.

---

### 9.1 Ma trận quyết định — Kết nối ESP32 → RPi5

Chấm điểm 1–5 theo mức độ phù hợp với kiosk trong nhà, cố định. Lưu ý thêm tiêu chí **không ảnh hưởng nguồn RPi5** so với phân tích trước:

| Tiêu chí | Trọng số | USB Serial | UART GPIO |
|---|---|---|---|
| Không phụ thuộc WiFi | 5 | **5** = 25 | **5** = 25 |
| Dễ triển khai & debug | 4 | **5** = 20 | **3** = 12 |
| Không ảnh hưởng nguồn RPi5 | 4 | **1** = 4 | **5** = 20 |
| Độ trễ thấp | 2 | **5** = 10 | **5** = 10 |
| Mở rộng nhiều kiosk | 2 | **2** = 4 | **2** = 4 |
| Không chiếm cổng USB RPi5 | 1 | **1** = 1 | **5** = 5 |
| **Tổng điểm** | | **64** | **76** |

> ✅ **UART GPIO đạt điểm cao hơn (76)** khi tính thêm tiêu chí không ảnh hưởng nguồn RPi5 — yếu tố quan trọng trong môi trường kiosk có nhiều thiết bị USB.

---

### 9.2 Ma trận quyết định — Định dạng dữ liệu

| Tiêu chí | Trọng số | JSON | CSV | Compact String | Binary |
|---|---|---|---|---|---|
| Dễ debug khi phát triển | 5 | **5** = 25 | **3** = 15 | **1** = 5 | **1** = 5 |
| Dễ mở rộng thêm trường | 4 | **5** = 20 | **2** = 8 | **1** = 4 | **1** = 4 |
| Kích thước payload nhỏ | 2 | **1** = 2 | **3** = 6 | **4** = 8 | **5** = 10 |
| Phát hiện lỗi truyền | 3 | **1** = 3 | **1** = 3 | **1** = 3 | **5** = 15 |
| Không cần thư viện nặng | 2 | **2** = 4 | **5** = 10 | **5** = 10 | **4** = 8 |
| **Tổng điểm** | | **54** | **42** | **30** | **42** |

---

### 9.3 Đề xuất chính thức

#### Phương án đề xuất — UART GPIO (có dây, không ảnh hưởng nguồn RPi5)

| Hạng mục | Lựa chọn | Lý do |
|---|---|---|
| **Kết nối ESP32 → RPi5** | **UART GPIO** @ 115200 baud | Không chiếm nguồn USB RPi5, 3 dây gọn trong vỏ kiosk, không phụ thuộc WiFi |
| **Xử lý ESP32** | **FreeRTOS** 2 task | Task đọc sensor (Core 1) + task gửi UART (Core 0), tách biệt hoàn toàn |
| **Định dạng dữ liệu** | **JSON** newline-delimited | Dễ debug nhất khi phát triển, thêm trường không cần sửa decoder |
| **Nguồn ESP32** | Adapter 5V riêng | Độc lập với RPi5, không kéo dòng từ USB |
| **Cổng UART RPi5** | `/dev/ttyAMA0` | Tắt Serial Console một lần qua `raspi-config` |
| **Cài đặt tự động** | systemd service | Tự khởi động và restart khi crash |

```
LD2420 ────UART1────┐                   3 dây (TX/RX/GND)
(baud 256000)        ├──► ESP32 ──UART2 GPIO──────────────► RPi5 ──► MQTT
SHT30 ──────I2C──────┤   FreeRTOS  (baud 115200)  /dev/ttyAMA0   Python
ReedSW ────GPIO──────┘
                          3 task
[Nguồn 5V riêng]────────────────────────────────────────────────────────┘
                       (không lấy từ USB RPi5)
```

> ✅ **Lý do chọn UART GPIO thay USB Serial:** RPi5 có giới hạn dòng cấp qua USB (~500mA–900mA chia cho tất cả thiết bị). Kiosk thường có nhiều ngoại vi USB khác (màn hình touch, camera, USB hub). Cấp nguồn ESP32 riêng giúp hệ thống ổn định hơn và tránh brownout khi RPi5 tải cao.


---

### 9.4 Khi nào nên chuyển định dạng

| Tình huống | Nên chuyển sang |
|---|---|
| Hệ thống ổn định, không cần thêm trường | **CSV** — nhỏ hơn ~3×, vẫn đọc được bằng mắt |
| Tích hợp thêm module LoRa / 433MHz | **Compact String** — tối ưu băng thông thấp |
| Cần phát hiện lỗi truyền tin cậy | **Binary Pack** — 8 byte + checksum XOR |

---

