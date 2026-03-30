# Database Schema

Use this schema for backend implementation. PostgreSQL for metadata; TimescaleDB (or InfluxDB) for time-series metrics/events.

---

## 1. Devices (PostgreSQL)

Stores device metadata and current status. Updated on status/event ingestion and via Admin API.

```sql
CREATE TABLE devices (
  device_id       VARCHAR(64) PRIMARY KEY,
  device_secret   VARCHAR(128) NOT NULL,   -- hashed or encrypted at rest
  location        VARCHAR(256),
  firmware_version VARCHAR(32),
  status          VARCHAR(16) NOT NULL DEFAULT 'offline',  -- online | offline
  last_seen       TIMESTAMPTZ,
  installation_date DATE,
  created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  updated_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  revoked         BOOLEAN NOT NULL DEFAULT FALSE
);

CREATE INDEX idx_devices_status ON devices(status);
CREATE INDEX idx_devices_last_seen ON devices(last_seen);
```

- **revoked:** When true, backend must not allow MQTT auth and should not send commands. Used for device revocation.

---

## 2. Events (Time-series)

Store each device event for analytics and audit. Use TimescaleDB hypertable or InfluxDB.

```sql
-- If using TimescaleDB (PostgreSQL extension)
CREATE TABLE events (
  time        TIMESTAMPTZ NOT NULL,
  device_id   VARCHAR(64) NOT NULL,
  event       VARCHAR(32) NOT NULL,   -- person_detected | door_opened | door_closed
  sensor      VARCHAR(32),            -- radar | reed_switch
  received_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

SELECT create_hypertable('events', 'time');
CREATE INDEX idx_events_device_time ON events(device_id, time DESC);
```

- **time:** Device timestamp (from payload). **received_at:** Server time (for dedup/ordering if needed).

---

## 3. Metrics (Time-series)

Aggregated metrics per device per interval. Backend can aggregate from events or store device-published metrics.

```sql
CREATE TABLE metrics (
  time           TIMESTAMPTZ NOT NULL,
  device_id      VARCHAR(64) NOT NULL,
  people_count   INTEGER,
  interactions   INTEGER,
  temperature    NUMERIC(4,1),
  humidity       NUMERIC(4,1),
  received_at    TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

SELECT create_hypertable('metrics', 'time');
CREATE INDEX idx_metrics_device_time ON metrics(device_id, time DESC);
```

---

## 4. Firmware manifests (PostgreSQL)

OTA version and binary URL. Backend serves manifest to devices.

```sql
CREATE TABLE firmware_manifests (
  id         SERIAL PRIMARY KEY,
  version    VARCHAR(32) UNIQUE NOT NULL,
  url        VARCHAR(512) NOT NULL,
  sha256     VARCHAR(64) NOT NULL,
  created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
```

---

## 5. Command log (optional, for debugging)

Store sent commands and acks.

```sql
CREATE TABLE command_log (
  id          BIGSERIAL PRIMARY KEY,
  device_id   VARCHAR(64) NOT NULL,
  cmd_id      VARCHAR(64) NOT NULL,
  cmd         VARCHAR(32) NOT NULL,
  payload     JSONB,
  ack_status  VARCHAR(16),   -- ok | error
  ack_error   TEXT,
  sent_at     TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  ack_at      TIMESTAMPTZ
);
```

---

## Summary for implementers

| Table              | Purpose |
|--------------------|---------|
| devices            | Device registry, credentials, status, revocation. |
| events             | Time-series events (person_detected, door_*). |
| metrics            | Time-series metrics (people_count, temp, humidity). |
| firmware_manifests | OTA versions and download URLs. |
| command_log        | Optional audit of commands and acks. |

Reference: **kiosk_system_architecture.md** §7; ingestion from MQTT topics per **mqtt_schema.md**.
