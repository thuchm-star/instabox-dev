# Kiosk Deployment Checklist

Use this checklist so firmware and backend can be validated end-to-end. References: **firmware_architecture.md**, **mqtt_schema.md**, **provisioning.md**.

---

## Before Installation

### Hardware

- [ ] ESP32 tested (boot, GPIO accessible).
- [ ] Radar LD2420 working (UART2).
- [ ] PIR (if used) and Reed switch working (GPIO 14, 4).
- [ ] SHTC3 working (I2C: SDA 21, SCL 19).
- [ ] Speaker and MAX98357A working (I2S: BCLK 26, LRC 25, DIN 22).
- [ ] LED connected (GPIO 2) and toggles on/off correctly.
- [ ] External watchdog connected (feed pin GPIO 33).

### Power

- [ ] 12V power supply stable.
- [ ] Buck converter tested (5V rail).
- [ ] 3.3V LDO stable for ESP32 and sensors.
- [ ] Capacitors near ESP32 (e.g. 470µF + 100nF) fitted.

### Connectivity

- [ ] WiFi SSID/password configured; network reachable.
- [ ] WiFi signal at install location > -70 dBm (or acceptable for 30s heartbeat).
- [ ] MQTT broker reachable (port 8883 TLS in production).
- [ ] NTP reachable (e.g. pool.ntp.org) for time sync.

### Firmware & provisioning

- [ ] Firmware flashed (current version).
- [ ] Device provisioned: device_id and device_secret in NVS (see **provisioning.md**).
- [ ] MQTT connects with TLS; LWT configured.
- [ ] NTP sync successful (time_synced = true in status).
- [ ] Status published every 30s; backend shows device online.

---

## On-Site Installation

1. [ ] Mount kiosk securely.
2. [ ] Connect power; verify stable 12V/5V/3.3V if measurable.
3. [ ] Verify WiFi signal (RSSI in status or local tool).
4. [ ] Trigger sensor test: walk in front of radar → confirm LED on and (if enabled) sound.
5. [ ] Confirm sound playback (volume acceptable).
6. [ ] Open/close door (Reed switch) → confirm door_opened / door_closed events in backend.

---

## Post-Deployment

### Backend

- [ ] Device appears online in dashboard.
- [ ] MQTT status received (last_seen updating).
- [ ] Events received (person_detected, door_* when triggered).
- [ ] Metrics received at report_interval (people_count, interactions, temperature, humidity).

### Optional tests

- [ ] **Offline buffer:** Disconnect WiFi for 2–5 min; reconnect. Confirm buffered events appear in backend (order/timestamp acceptable).
- [ ] **Command:** Send play_sound or led_off from backend; confirm command_ack and behaviour.
- [ ] **OTA (staged):** Deploy new version to 1 device; confirm update and rollback path if needed.

---

## Maintenance

### Daily (automated or manual)

- [ ] Device online count normal.
- [ ] Event count per device in expected range (no spike from one device unless expected).

### Monthly

- [ ] OTA firmware update (staged rollout per **kiosk_system_architecture.md** §15).
- [ ] Sensor check: radar sensitivity and PIR (if used) still appropriate; Reed switch still responsive.

---

## Doc reference

| Doc | Use |
|-----|-----|
| mqtt_schema.md | Topic names, payloads, QoS, LWT, commands. |
| firmware_architecture.md | GPIO, tasks, offline buffer, NTP, boot order. |
| backend_api_spec.md | REST API for devices, commands, firmware. |
| database_schema.md | Tables and DDL for backend. |
| provisioning.md | Register device and load credentials. |
| kiosk_system_architecture.md | Overall architecture and ops rules. |
