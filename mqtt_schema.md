# MQTT Message Schema

**Single source of truth** for all MQTT topics and payloads. Backend and firmware must use this schema only.

---

## Broker & Connection

- **Port:** 8883 (TLS) recommended; 1883 only for development.
- **Auth:** `username = device_id`, `password = device_secret`.
- **Client ID:** `kiosk_{device_id}` (unique per device).

### QoS & Retain

| Topic pattern            | Direction   | QoS | Retain |
|--------------------------|------------|-----|--------|
| `kiosk/{id}/status`      | Device →   | 1   | true (last status) |
| `kiosk/{id}/event`       | Device →   | 1   | false |
| `kiosk/{id}/metrics`     | Device →   | 1   | false |
| `kiosk/{id}/log`         | Device →   | 0   | false |
| `kiosk/{id}/command`     | Backend →  | 1   | false |

### Last Will Testament (LWT)

- **Topic:** `kiosk/{device_id}/status`
- **Payload:** `{"online": false, "reason": "connection_lost"}`
- **QoS:** 1, **Retain:** true

When the device disconnects unexpectedly, the broker publishes this so the backend can mark the device offline immediately.

---

## Topic List (Device → Backend)

| Topic                    | Description |
|--------------------------|-------------|
| `kiosk/{device_id}/status`  | Heartbeat + health (every 30s). |
| `kiosk/{device_id}/event`   | Real-time events (person, door, etc.). |
| `kiosk/{device_id}/metrics` | Aggregated counters + sensor snapshot. |
| `kiosk/{device_id}/log`     | Optional diagnostic logs. |

## Topic (Backend → Device)

| Topic                    | Description |
|--------------------------|-------------|
| `kiosk/{device_id}/command` | Remote commands (subscribe by device). |

`{device_id}` format: alphanumeric + underscore, e.g. `KIOSK_023`. Must match device registration.

---

## 1. Status (Heartbeat)

**Topic:** `kiosk/{device_id}/status`  
**Publish:** Device **every 30 seconds** (chu kỳ cố định, không đổi theo config).  
**Retain:** true (broker keeps last status).

**Lưu ý:** Nhiệt độ và độ ẩm **không** gửi trong status; chỉ gửi trong **metrics** (theo report_interval).

```json
{
  "device_id": "KIOSK_023",
  "firmware": "1.0.2",
  "uptime": 12000,
  "online": true,
  "wifi_rssi": -60,
  "heap_free": 120000,
  "time_synced": true,
  "timestamp": 1710000000
}
```

| Field         | Type    | Required | Description |
|---------------|---------|----------|-------------|
| device_id     | string  | yes      | Same as topic device_id. |
| firmware      | string  | yes      | Semver, e.g. 1.0.2. |
| uptime        | number  | yes      | Seconds since boot. |
| online        | boolean | yes      | Always true when publishing. |
| wifi_rssi     | number  | no       | RSSI in dBm (e.g. -70). |
| heap_free     | number  | no       | Free heap in bytes. |
| time_synced   | boolean | yes      | true if NTP sync succeeded. |
| timestamp     | number  | yes      | Unix seconds (from NTP if synced). |

---

## 2. Event

**Topic:** `kiosk/{device_id}/event`  
**Publish:** On each event (person detected, door open, etc.).

### person_detected

```json
{
  "event": "person_detected",
  "timestamp": 1710000000,
  "device_id": "KIOSK_023",
  "sensor": "radar"
}
```

### door_opened (Reed switch)

```json
{
  "event": "door_opened",
  "timestamp": 1710000000,
  "device_id": "KIOSK_023",
  "sensor": "reed_switch"
}
```

### door_closed

```json
{
  "event": "door_closed",
  "timestamp": 1710000000,
  "device_id": "KIOSK_023",
  "sensor": "reed_switch"
}
```

| Field     | Type   | Required | Description |
|-----------|--------|----------|-------------|
| event     | string | yes      | One of: `person_detected`, `door_opened`, `door_closed`. |
| timestamp | number | yes      | Unix seconds. |
| device_id | string | yes      | Kiosk ID. |
| sensor    | string | yes      | `radar` or `reed_switch`. |

---

## 3. Metrics

**Topic:** `kiosk/{device_id}/metrics`  
**Publish:** Device at **configured interval** `report_interval_sec` (ví dụ mặc định **300 s = 5 phút**). Gồm environmental data (nhiệt độ, độ ẩm) và counters (people_count, interactions).

```json
{
  "device_id": "KIOSK_023",
  "timestamp": 1710000000,
  "people_count": 43,
  "interactions": 15,
  "temperature": 42.5,
  "humidity": 60
}
```

| Field         | Type   | Required | Description |
|---------------|--------|----------|-------------|
| device_id     | string | yes      | Kiosk ID. |
| timestamp     | number | yes      | Unix seconds. |
| people_count  | number | yes      | Count of person_detected in period. |
| interactions  | number | yes      | Count of LED-on / interaction in period. |
| temperature   | number | no       | Celsius (SHT30/SHTC3). |
| humidity      | number | no       | Percent (SHT30/SHTC3). |

---

## 4. Log (Optional)

**Topic:** `kiosk/{device_id}/log`  
**Publish:** Device for diagnostics; avoid high frequency.

```json
{
  "level": "warn",
  "message": "WiFi reconnect after 120s",
  "timestamp": 1710000000,
  "device_id": "KIOSK_023"
}
```

`level`: `info` | `warn` | `error`.

---

## 5. Command (Backend → Device)

**Topic:** `kiosk/{device_id}/command`  
**Subscribe:** Device only subscribes to its own topic.  
**QoS:** 1. Device should publish ack to `kiosk/{device_id}/command_ack` on success/failure (see below).

### reboot

```json
{
  "cmd": "reboot",
  "id": "cmd-uuid-123"
}
```

### play_sound

```json
{
  "cmd": "play_sound",
  "id": "cmd-uuid-456",
  "sound_id": "attention"
}
```

| sound_id  | Description |
|-----------|-------------|
| attention | Default short attention sound (pre-flashed). |
| custom_1  | Optional custom sound 1 (if stored on device). |
| custom_2  | Optional custom sound 2. |

Firmware maps `sound_id` to internal file or URL; unknown id → ignore or play `attention`.

### led_on / led_off

```json
{ "cmd": "led_on",  "id": "cmd-uuid-789" }
{ "cmd": "led_off", "id": "cmd-uuid-012" }
```

### update_config

```json
{
  "cmd": "update_config",
  "id": "cmd-uuid-345",
  "config": {
    "led_timeout_sec": 60,
    "speaker_volume": 80,
    "radar_sensitivity": 5,
    "report_interval_sec": 300
  }
}
```

| config key           | Type   | Range / notes |
|----------------------|--------|----------------|
| led_timeout_sec      | number | 10–600. |
| speaker_volume       | number | 0–100. |
| radar_sensitivity    | number | 1–10 (sensor-specific). |
| report_interval_sec  | number | 60–3600. |

Device must validate; out-of-range → reject and send ack with error.

### Command response (Device → Backend)

**Topic:** `kiosk/{device_id}/command_ack`

```json
{
  "id": "cmd-uuid-123",
  "cmd": "reboot",
  "status": "ok"
}
```

or

```json
{
  "id": "cmd-uuid-345",
  "cmd": "update_config",
  "status": "error",
  "error": "invalid led_timeout_sec"
}
```

| status | Meaning |
|--------|--------|
| ok     | Command accepted and executed (or queued e.g. reboot). |
| error  | Rejected or failed; see `error` string. |

---

## Summary Table for Implementers

| Topic                         | Publisher | Payload key fields |
|-------------------------------|-----------|---------------------|
| `kiosk/{id}/status`           | Device    | device_id, firmware, uptime, online, time_synced, timestamp |
| `kiosk/{id}/event`            | Device    | event, timestamp, device_id, sensor |
| `kiosk/{id}/metrics`          | Device    | device_id, timestamp, people_count, interactions, temperature, humidity |
| `kiosk/{id}/log`              | Device    | level, message, timestamp, device_id |
| `kiosk/{id}/command`          | Backend   | cmd, id [, sound_id \| config] |
| `kiosk/{id}/command_ack`      | Device    | id, cmd, status [, error] |
