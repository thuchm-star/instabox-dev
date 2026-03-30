# ESP32 Kiosk Firmware Architecture

## Overview

Firmware runs on ESP32-S3 and manages sensors, audio, connectivity, and reporting. All MQTT payloads follow `mqtt_schema.md`.

**Goals:**
- Detect people (radar primary; PIR optional for redundancy).
- Turn on LED and play attention sound on detection; turn LED off after timeout.
- Report events and metrics to backend via MQTT.
- Maintain WiFi/MQTT with auto-reconnect and offline buffer.
- Support OTA updates and NTP time sync.

---

## Task Architecture (FreeRTOS)

| Task                 | Priority | Stack | Responsibility |
|----------------------|----------|-------|----------------|
| sensor_task          | 5        | 4K    | Read radar, PIR, Reed; debounce; emit "person present" / door events. |
| led_control_task      | 4        | 2K    | LED ON on person_detected; OFF after timeout. |
| audio_task           | 4        | 2K    | Play sound via MAX98357A (I2S). |
| network_task         | 3        | 3K    | WiFi connect, reconnect, NTP sync. |
| mqtt_task            | 3        | 4K    | Publish status/event/metrics; subscribe command; send command_ack. |
| ota_task             | 2        | 4K    | Poll OTA endpoint or react to MQTT; download and apply. |
| watchdog_task        | 6        | 1K    | Feed HW watchdog; optional health check of other tasks. |

---

## Person Detection Logic (Radar vs PIR)

- **Primary:** LD2420 radar (UART). Used for "person present" and `person_detected` events. Marketing analytics and LED on/off are driven by radar.
- **PIR (optional):** GPIO 14. If used, define rule in config (e.g. "person present = radar OR PIR"). PIR can cause false positives (heat, sunlight); do not use PIR alone for analytics counts. Prefer **radar only** for `people_count` / `interactions`.

Implementer: set a compile-time or config option (e.g. `PIR_ENABLED`) and document the chosen rule.

---

## Event Flow

```
Person detected (radar) → sensor_task
  → queue_event(PERSON_DETECTED)
  → led_control_task: LED ON, send event to mqtt_task
  → audio_task: play sound once (debounced)
  → mqtt_task: publish person_detected to kiosk/{id}/event

Reed switch open → sensor_task
  → queue_event(DOOR_OPENED)
  → mqtt_task: publish door_opened

Reed switch closed → sensor_task
  → queue_event(DOOR_CLOSED)
  → mqtt_task: publish door_closed
```

---

## Hardware Mapping (GPIO)

**Resolved pinout (no shared pins):**

| Peripheral      | Interface | Pins |
|-----------------|-----------|------|
| LD2420 Radar    | UART2     | TX=17, RX=16 |
| PIR             | GPIO      | 14 |
| Reed switch     | GPIO      | 4 (internal pull-up; door open = LOW) |
| LED (output)    | GPIO      | 2 (HIGH = on, LOW = off; use resistor if 3.3V direct) |
| SHT30 (T/H)     | I2C       | SDA=21, SCL=19 (hoặc SDA=8, SCL=9 trên ESP32-S3-DevKitC-1) |
| MAX98357A (I2S) | I2S       | BCLK=26, LRC=25, DIN=22 |
| Watchdog feed   | GPIO      | 33 (to external WD input) |

**Note:** SHT30 dùng I2C; trên ESP32-S3-DevKitC-1 có thể dùng SDA=8, SCL=9 (board_config.h) để tránh trùng I2S.

---

## NTP & Time

- On WiFi connect: run NTP sync (e.g. `pool.ntp.org`). Set timezone via config if needed.
- Use synced time for all `timestamp` fields in status/event/metrics. If NTP not yet synced, set `time_synced: false` in status and prefer not publishing analytics events until synced (or document behaviour).
- Retry NTP periodically (e.g. every hour) while connected.

---

## Offline Buffer Policy

- **Storage:** SPIFFS or NVS-backed queue. Prefer a fixed-size queue (e.g. max 500 events).
- **When offline:** Append events (and optionally metrics) to buffer. Do not buffer status heartbeats to avoid filling flash.
- **Eviction:** When buffer is full, drop **oldest** event (FIFO). Optionally log once: "offline buffer full, dropping oldest".
- **On reconnect:** Send buffered messages in order, in batches (e.g. 10 per batch), with small delay between batches to avoid flooding. Delete from buffer only after publish success (QoS 1 ack).
- **Limit:** Cap total buffered size (e.g. 500 events). If device is offline longer, older data is lost; ensure backend treats "last_seen" and gaps appropriately.

---

## MQTT Behaviour (Firmware)

- Connect with LWT set (see `mqtt_schema.md`). Publish `status` every 30s. Publish `event` on each event, `metrics` at report_interval.
- Subscribe to `kiosk/{device_id}/command`. On each command: parse `cmd` and `id`, execute, then publish to `kiosk/{device_id}/command_ack` with same `id` and `status` (ok/error).
- Use TLS on port 8883 in production; certificate verification enabled for OTA and preferably for MQTT.

**Logic tần suất (đã rà soát):**
- **Status:** Chu kỳ **cố định 30 giây**. Chỉ chứa heartbeat/health (device_id, uptime, wifi_rssi, heap_free, time_synced, timestamp). **Không** gửi nhiệt độ/độ ẩm trong status.
- **Metrics:** Chu kỳ **report_interval_sec** (mặc định 300 s = 5 phút). Chứa people_count, interactions, **temperature**, **humidity**, device_id, timestamp. Nhiệt độ/độ ẩm chỉ gửi trong metrics, tránh trùng và tiết kiệm traffic.
- Backend coi thiết bị offline nếu không nhận status trong **3 phút** (hoặc LWT). Tần suất 30s cho phép phát hiện offline kịp thời mà không tốn quá nhiều data.

---

## OTA

- Check for new firmware (HTTP GET manifest or via backend); compare version with current. If newer: download over HTTPS, verify SHA256, write to inactive partition, reboot. On boot failure, rollback to previous partition.
- Use certificate validation for HTTPS. Store root CA or use fingerprint if no full TLS stack.

---

## Boot Sequence

1. Init NVS, load config (led_timeout_sec, volume, radar_sensitivity, report_interval).
2. Init sensors (radar, PIR, Reed, SHT30), LED GPIO, audio, watchdog.
3. Start WiFi → wait connected.
4. Sync NTP.
5. Connect MQTT (with LWT), subscribe command topic.
6. Start periodic: status (30s), metrics (report_interval), OTA check.
7. Process sensor events and command queue.

---

## Fail Safe

- **Software hang:** ESP32 task watchdog on critical tasks; extend to all long-running tasks.
- **System hang:** External hardware watchdog on GPIO 33; timeout 5–10 s. If no feed, device resets.
- **Boot failure after OTA:** Use rollback to previous partition (ESP32 OTA rollback support).

---

## Config Keys (NVS)

| Key                    | Type  | Default | Description |
|------------------------|-------|---------|-------------|
| led_timeout_sec        | uint  | 60      | LED off after no person (seconds). |
| speaker_volume         | uint  | 80      | 0–100. |
| radar_sensitivity      | uint  | 5       | 1–10. |
| report_interval_sec   | uint  | 300     | Metrics publish interval. |
| device_id              | string| —       | Set at provisioning. |
| device_secret          | string| —       | Set at provisioning; store securely if possible. |

Reference for remote `update_config` payload: `mqtt_schema.md`.
