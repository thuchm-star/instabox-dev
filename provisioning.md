# Device Provisioning

How to register a new kiosk and load credentials onto the device so it can connect to MQTT and backend.

---

## 1. Backend: Create device

1. Call **POST /api/v1/devices** with:
   - `device_id`: e.g. `KIOSK_023` (unique, alphanumeric + underscore).
   - `location`: optional string.
   - `installation_date`: optional date.

2. Response includes **device_secret** (only returned once). Store it securely (e.g. in password manager or secure storage). You will need it for step 2.

3. Backend creates:
   - DB row in `devices` (device_id, device_secret hashed/stored, status=offline).
   - MQTT broker user/ACL so `username=device_id`, `password=device_secret` can subscribe to `kiosk/{device_id}/command` and publish to `kiosk/{device_id}/status`, `event`, `metrics`, `log`, `command_ack`.

---

## 2. Load credentials onto ESP32

Choose one method. Device must end up with `device_id` and `device_secret` in NVS (or equivalent).

### Option A: Serial / USB (factory or lab)

- Use a small script or CLI that connects to ESP32 over serial and writes NVS keys: `device_id`, `device_secret`.
- Or use ESP32 provisioning app (e.g. WiFi + AP mode with captive portal) that asks for device_id and device_secret and saves to NVS.

### Option B: QR code

- Generate QR containing: `device_id` and `device_secret` (or a one-time token that backend exchanges for secret). Device scans QR at first boot and writes NVS.

### Option C: Pre-flash file

- Generate a file (e.g. `credentials.json`) with device_id and device_secret. Flash to device or place on SPIFFS during factory flash. Firmware reads once and copies to NVS, then deletes or overwrites file.

---

## 3. Broker ACL (example EMQX)

For each device, allow:

- **Publish:** `kiosk/{device_id}/status`, `kiosk/{device_id}/event`, `kiosk/{device_id}/metrics`, `kiosk/{device_id}/log`, `kiosk/{device_id}/command_ack`.
- **Subscribe:** `kiosk/{device_id}/command`.

Deny all other topics. Use device_id as username and device_secret as password.

---

## 4. Checklist

- [ ] Device created via POST /devices; device_secret saved.
- [ ] Broker user and ACL configured for this device_id.
- [ ] device_id and device_secret written to device NVS (or equivalent).
- [ ] Device powered and connected to WiFi; MQTT connects; status appears in backend.
- [ ] Test command (e.g. play_sound) and verify command_ack.

Reference: **backend_api_spec.md** (Register device), **mqtt_schema.md** (auth), **kiosk_system_architecture.md** (§13 Security).
