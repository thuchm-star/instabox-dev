# Backend API Specification

REST API for Admin Dashboard and integrations. Base URL: `/api/v1`. All timestamps in ISO 8601; auth (e.g. JWT or API key) required unless noted.

---

## 1. Devices

### List devices

```
GET /devices
Query: status=online|offline, limit=50, offset=0
Response: { "items": [ { "device_id", "location", "firmware_version", "status", "last_seen", "installation_date" } ], "total": N }
```

### Get device

```
GET /devices/:device_id
Response: { "device_id", "location", "firmware_version", "status", "last_seen", "installation_date", "created_at" }
```

### Register device (provisioning)

```
POST /devices
Body: { "device_id": "KIOSK_023", "location": "Floor 1", "installation_date": "2025-03-01" }
Response: 201 { "device_id", "device_secret", "location", "installation_date" }
```

- Backend generates **device_secret** (random, e.g. 32 bytes hex). Caller must store and inject into device (see provisioning doc). Do not return secret again in GET.

### Update device

```
PATCH /devices/:device_id
Body: { "location", "installation_date" }
Response: 200 { "device_id", "location", ... }
```

### Revoke device

```
POST /devices/:device_id/revoke
Response: 200 { "device_id", "revoked": true }
```

- Backend sets devices.revoked = true and updates broker ACL or credential so device cannot connect.

---

## 2. Commands (send to device via MQTT)

Backend publishes to `kiosk/{device_id}/command`. These endpoints generate the command payload and publish.

### Reboot

```
POST /devices/:device_id/commands/reboot
Response: 200 { "cmd_id", "sent": true }
```

### Play sound

```
POST /devices/:device_id/commands/play_sound
Body: { "sound_id": "attention" }
Response: 200 { "cmd_id", "sent": true }
```

### LED on/off

```
POST /devices/:device_id/commands/led_on
POST /devices/:device_id/commands/led_off
Response: 200 { "cmd_id", "sent": true }
```

### Update config

```
POST /devices/:device_id/commands/update_config
Body: { "led_timeout_sec", "speaker_volume", "radar_sensitivity", "report_interval_sec" }
Response: 200 { "cmd_id", "sent": true }
```

- Validate ranges per mqtt_schema.md before publishing.

---

## 3. Events & Analytics

### List events (time range)

```
GET /devices/:device_id/events?from=ISO8601&to=ISO8601&limit=100
Response: { "items": [ { "time", "event", "sensor" } ] }
```

### List metrics (time range)

```
GET /devices/:device_id/metrics?from=ISO8601&to=ISO8601&interval=5m&limit=100
Response: { "items": [ { "time", "people_count", "interactions", "temperature", "humidity" } ] }
```

### Aggregates (dashboard)

```
GET /analytics/summary?from=ISO8601&to=ISO8601
Response: { "total_events", "total_interactions", "devices_online", "devices_offline" }
```

---

## 4. Firmware (OTA)

### List versions

```
GET /firmware
Response: { "items": [ { "version", "url", "sha256", "created_at" } ] }
```

### Create version (upload manifest)

```
POST /firmware
Body: { "version": "1.0.2", "url": "https://cdn.example.com/firmware/v1.0.2.bin", "sha256": "hex..." }
Response: 201 { "version", "url", "sha256" }
```

### Manifest for devices (OTA check)

```
GET /firmware/manifest
Response: { "version": "1.0.2", "url": "https://...", "sha256": "..." }
```

- Devices GET this endpoint to compare with current version and download if newer. Optional: require query param or header to return stable/latest only.

---

## 5. MQTT Ingestion (backend implementation note)

Backend must subscribe to:

- `kiosk/+/status`   → update devices.status, devices.last_seen; mark offline on LWT or timeout.
- `kiosk/+/event`    → insert into events table.
- `kiosk/+/metrics`  → insert into metrics table.
- `kiosk/+/command_ack` → optional: update command_log, alert on error.

Use single source of truth: **mqtt_schema.md** for topic names and payload fields.

---

## Summary for implementers

| Area      | Endpoints |
|-----------|-----------|
| Devices   | GET/POST/PATCH /devices, POST /devices/:id/revoke |
| Commands  | POST /devices/:id/commands/reboot, play_sound, led_on, led_off, update_config |
| Events    | GET /devices/:id/events |
| Metrics   | GET /devices/:id/metrics |
| Analytics | GET /analytics/summary |
| Firmware  | GET/POST /firmware, GET /firmware/manifest |

Database schema: **database_schema.md**. MQTT payloads: **mqtt_schema.md**.
