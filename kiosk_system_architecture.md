# Kiosk System Architecture (ESP32 + Sensor + Backend)

## 1. Project Overview

This document defines the **technical architecture and operational
rules** for a kiosk system using:

-   ESP32-S3 microcontroller
-   Radar sensor for human detection
-   Environmental sensors
-   Audio playback
-   Cloud backend for monitoring and control

Target scale:

-   \~100 kiosks
-   Continuous operation (24/7)
-   Remote management and OTA updates

The system must support:

-   Device monitoring
-   Sensor data collection
-   Marketing analytics
-   Remote configuration
-   OTA firmware update
-   Fault recovery

------------------------------------------------------------------------

# 2. Hardware Architecture

## Main Controller

-   ESP32-S3

Responsibilities:

-   WiFi connection
-   Sensor reading
-   MQTT communication
-   OTA firmware update
-   Local buffering when offline
-   Command execution

------------------------------------------------------------------------

## Sensors

### Radar Sensor (Human Detection)

Purpose:

-   Detect nearby people
-   Turn on LED
-   Trigger audio

Interface:

-   UART

Recommended sensor:

-   HLK-LD2420

------------------------------------------------------------------------

### Temperature / Humidity Sensor

Purpose:

-   Monitor kiosk internal temperature
-   Detect overheating

Sensor:

-   SHT30 (I2C, address 0x44)

Data reporting interval:

-   every 5 minutes

------------------------------------------------------------------------

### Reed Switch

Purpose:

-   Detect kiosk door opening

Trigger event:

-   maintenance access
-   tampering detection

------------------------------------------------------------------------

## Audio System

Speaker:

-   8 Ohm 5W

Amplifier:

-   MAX98357A (I2S audio DAC)

Connection:

ESP32 I2S → MAX98357A → Speaker

------------------------------------------------------------------------

# 3. Power System

## Power Flow

12V Input → Buck Converter → 5V Rail

5V Rail supplies:

-   Radar sensor
-   MAX98357A amplifier

5V → LDO → 3.3V

3.3V supplies:

-   ESP32
-   SHT30
-   logic sensors

------------------------------------------------------------------------

## Recommended Components

Buck converter:

-   LM2596 or
-   MP1584

Add capacitors near ESP32:

-   470uF electrolytic
-   100nF ceramic

Purpose:

-   prevent voltage drop
-   reduce noise
-   stabilize ESP32

------------------------------------------------------------------------

# 4. Watchdog Strategy

## Software Watchdog

ESP32 built-in watchdog enabled.

Used for:

-   task deadlock
-   infinite loops

------------------------------------------------------------------------

## Hardware Watchdog

External watchdog timer recommended.

Function:

If ESP32 does not send heartbeat signal periodically:

-   watchdog triggers
-   system reset

Typical timeout:

5--10 seconds

------------------------------------------------------------------------

# 5. Network Architecture

Communication protocol: **MQTT** over **TLS (port 8883)** in production. Use 1883 only for development.

All kiosks connect to a central MQTT broker. Recommended brokers: **EMQX**, **Mosquitto**.

**Single source of truth for topics and payloads:** see `mqtt_schema.md`. Summary:

| Topic | Direction | Purpose |
|-------|-----------|---------|
| kiosk/{device_id}/status | Device → | Heartbeat every 30s (retain). |
| kiosk/{device_id}/event | Device → | person_detected, door_opened, door_closed. |
| kiosk/{device_id}/metrics | Device → | Counters + temperature/humidity. |
| kiosk/{device_id}/log | Device → | Optional diagnostics. |
| kiosk/{device_id}/command | Backend → | reboot, play_sound, led_on/off, update_config. |
| kiosk/{device_id}/command_ack | Device → | Command result (id, status, error). |

LWT: on unexpected disconnect, broker publishes offline status so backend can mark device OFFLINE immediately.

------------------------------------------------------------------------

# 6. Backend Architecture

Main components:

Admin Dashboard Backend API MQTT Broker Database Firmware Storage

System diagram:

Admin Dashboard → Backend API → MQTT Broker → Kiosk devices

------------------------------------------------------------------------

## Backend API Responsibilities

-   Device registration (see **backend_api_spec.md** and **provisioning.md**).
-   Firmware management (OTA manifest and versioning).
-   Remote commands (publish to MQTT from API).
-   Analytics aggregation (ingest events/metrics from MQTT; store per **database_schema.md**).
-   Configuration management (update_config command).

Suggested frameworks: NodeJS (NestJS), Python (FastAPI), Go.

**Full API and payloads:** **backend_api_spec.md**. **Database DDL and tables:** **database_schema.md**.

------------------------------------------------------------------------

# 7. Database Design

See **database_schema.md** for full DDL. Summary:

- **devices:** device_id, device_secret, location, firmware_version, status, last_seen, installation_date, revoked.
- **events:** time-series (time, device_id, event, sensor).
- **metrics:** time-series (time, device_id, people_count, interactions, temperature, humidity).
- **firmware_manifests:** version, url, sha256.
- **command_log:** optional audit (device_id, cmd_id, cmd, ack_status, ack_error).

Use PostgreSQL for metadata; TimescaleDB (or InfluxDB) for events and metrics.

------------------------------------------------------------------------

# 8. OTA Firmware System

ESP32 uses dual partition OTA.

Partition layout:

factory ota_0 ota_1

Update process:

1.  Device checks server for new version
2.  Firmware downloaded
3.  Checksum verification
4.  Write to inactive partition
5.  Reboot
6.  Boot new firmware

Rollback enabled if boot fails.

------------------------------------------------------------------------

## Firmware Manifest Example

{ "version": "1.0.2", "url": "https://server/firmware/v1.0.2.bin",
"sha256": "checksum" }

------------------------------------------------------------------------

# 9. Firmware Runtime Tasks

See **firmware_architecture.md** for task list and priorities. Main tasks: sensor_task, led_control_task, audio_task, network_task, mqtt_task, ota_task, watchdog_task.

**Status (heartbeat):** Sent every 30 seconds to `kiosk/{id}/status`. Payload must include device_id, firmware, uptime, online, time_synced, timestamp (see mqtt_schema.md).

**Metrics / sensor data:** Published to `kiosk/{id}/metrics` at report_interval (e.g. 5 min). Payload: device_id, timestamp, people_count, interactions, temperature, humidity.

------------------------------------------------------------------------

# 10. Remote Commands

Backend sends commands to `kiosk/{device_id}/command`. Device replies on `kiosk/{device_id}/command_ack` with same `id` and `status` (ok/error).

Supported commands and payloads are defined in **mqtt_schema.md**: reboot, play_sound (with sound_id), led_on, led_off, update_config (with config object and validation rules).

------------------------------------------------------------------------

# 11. Remote Configuration

Configurable parameters:

led_timeout speaker_volume radar_sensitivity report_interval

Configuration stored locally in:

ESP32 NVS

------------------------------------------------------------------------

# 12. Offline Data Buffer

If WiFi/MQTT is down, device buffers **events** (and optionally metrics) in flash (SPIFFS or NVS-backed queue). Do not buffer status heartbeats.

- **Max size:** e.g. 500 events. When full, drop oldest (FIFO).
- **On reconnect:** Upload in batches (e.g. 10 at a time); remove from buffer only after successful publish (QoS 1 ack).
- **Backend:** Accept out-of-order or delayed events; use device timestamp and last_seen for analytics and gap handling.

Details: **firmware_architecture.md** (Offline Buffer Policy).

------------------------------------------------------------------------

# 13. Security Rules

- **Credentials:** Each device has device_id and device_secret. Stored in NVS; prefer NVS encryption if supported.
- **MQTT:** Authentication: username = device_id, password = device_secret. Use **TLS (port 8883)** in production; certificate verification enabled.
- **OTA:** All downloads over **HTTPS**. Verify server certificate (root CA or fingerprint). Firmware integrity: SHA256 checksum in manifest; verify after download before writing partition.
- **Revocation:** Backend must support revoking a device (invalidate credential in DB, remove from broker ACL or change password). Revoked devices must not be able to connect or receive commands.

------------------------------------------------------------------------

# 14. Monitoring

Recommended monitoring tools:

Prometheus Grafana

Metrics:

device online status MQTT connection count API latency sensor data
ingestion rate

------------------------------------------------------------------------

# 15. Operational Rules

- **Heartbeat:** Device publishes to `kiosk/{id}/status` every 30 seconds. Backend marks device **OFFLINE** if no status received for **3 minutes** (or on LWT).
- **Offline action:** Backend should alert (e.g. Prometheus alert, dashboard, email) when device goes offline; optionally create ticket or show on map. See runbook below.
- **OTA rollout:** Staged. Stage 1: 5 devices → observe. Stage 2: 20 devices → observe. Stage 3: full deployment.
- **OTA rollback criteria:** If >10% of updated devices fail to report status within 10 minutes after reboot, or >5% report boot/error in command_ack, **pause rollout** and rollback (push previous firmware version to affected devices). Do not deploy to all devices at once.

------------------------------------------------------------------------

# 16. Known Risks

## WiFi instability

Mall or building WiFi may disconnect frequently.

Firmware must support:

auto reconnect MQTT reconnect data buffering

------------------------------------------------------------------------

## False people detection

Radar detects motion, not unique people.

Marketing analytics must consider:

movement ≠ unique visitor

------------------------------------------------------------------------

## OTA failure risk

Never deploy firmware to all devices simultaneously.

Use staged rollout.

------------------------------------------------------------------------

# 17. Runbook (Operations)

| Situation | Action |
|-----------|--------|
| Device offline >3 min | Check dashboard; if many devices, check broker/network. For single device: check WiFi at site, power, reboot via command. |
| WiFi unstable at site | Verify signal >-70 dBm; consider repeater or Ethernet. Ensure firmware has reconnect + buffer (see firmware_architecture.md). |
| OTA failure / device not coming back | Use rollback criteria; push previous version. If device unreachable, on-site check or hardware replacement. |
| Broker down | Failover broker if HA; else restore broker and let devices reconnect. Backend should tolerate duplicate events after reconnect. |
| High event rate from one device | Check for bug or tampering; rate-limit on broker/backend if needed; consider revoking and re-provisioning. |

------------------------------------------------------------------------

# 18. Future Extensions

Possible upgrades: BLE beacon analytics, Edge AI detection, Camera integration, Remote diagnostics.
