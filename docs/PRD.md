# OMNINODE — Product Requirements Document

**Firmware Specification & Implementation Contract**
*Version 1.0 — March 2026*

---

## 1. Purpose & Scope

This document is the implementation contract for the OmniNode puck firmware. It specifies exact behavior, API shapes, payload formats, error handling, hardware pin assignments, timing constraints, and phase boundaries.

**This PRD covers:**
- OmniNode puck firmware, Phases 1 through 5
- Arduino / PlatformIO (C++) on the Seeed Studio XIAO ESP32C6
- All endpoints, request/response schemas, and error codes

**This PRD does not cover:**
- Agent behavior (OpenClaw logic, NLP, device identification)
- Host-side scripts, code libraries, or automation logic
- Mobile apps, dashboards, or companion software
- Cloud services or remote access
- Home Assistant integration
- Enclosure or industrial design
- Zigbee / Thread endpoints (excluded from this PRD; see note in Section 15)

For strategic context, architecture rationale, and business model, see `docs/ProjectSummary.md`.

---

## 2. Phase Matrix

This table is the single source of truth for what exists in each firmware phase. If a feature is not marked for a phase, it does not exist in that firmware build.

| Feature | Phase 1 | Phase 2 | Phase 3 | Phase 4 | Phase 5 |
|---------|---------|---------|---------|---------|---------|
| WiFi connection (hardcoded creds) | Yes | — | — | — | — |
| WiFi connection (captive portal) | — | Yes | Yes | Yes | Yes |
| mDNS broadcast | Yes | Yes | Yes | Yes | Yes |
| `GET /` manifest | Yes | Yes | Yes | Yes | Yes |
| `GET /openapi.json` spec | Yes | Yes | Yes | Yes | Yes |
| `GET /status` | Yes | Yes | Yes | Yes | Yes |
| `POST /ir/capture` | Yes | Yes | Yes | Yes | Yes |
| `POST /ir/blast` | Yes | Yes | Yes | Yes | Yes |
| Bearer token authentication | — | Yes | Yes | Yes | Yes |
| Captive portal provisioning | — | Yes | Yes | Yes | Yes |
| Power-cycle factory reset | — | Yes | Yes | Yes | Yes |
| `POST /system/ota` | — | Yes | Yes | Yes | Yes |
| `POST /system/reboot` | — | Yes | Yes | Yes | Yes |
| `POST /rf/capture` | — | — | Yes | Yes | Yes |
| `POST /rf/blast` | — | — | Yes | Yes | Yes |
| `POST /nfc/read` | — | — | — | Yes | Yes |
| `POST /nfc/write` | — | — | — | Yes | Yes |
| `POST /ble/scan` | — | — | — | — | Yes |
| `POST /ble/connect` | — | — | — | — | Yes |
| `POST /ble/write` | — | — | — | — | Yes |
| `POST /ble/read` | — | — | — | — | Yes |

The OpenAPI spec served at `GET /openapi.json` only describes endpoints that are enabled in the current firmware build. It is a live capability manifest, not a future roadmap. An agent must be able to trust that every endpoint in the spec is functional.

---

## 3. Boot Sequences

### 3.1 Phase 1 Development Boot

Phase 1 uses hardcoded WiFi credentials compiled into the firmware. No captive portal. No authentication.

1. Initialize hardware: configure GPIO pins, set pin modes
2. Connect to WiFi using hardcoded SSID and password
3. If WiFi connection fails after 3 attempts (10 seconds each): halt and blink onboard LED as error indicator
4. Start mDNS: hostname `omninode-{mac6}.local`, service `_omninode._tcp`, port 80
5. Start HTTP server on port 80
6. Initialize IR receiver (begin passive listening on GPIO1)
7. Puck is operational and discoverable

`{mac6}` is the last 6 hex characters of the WiFi MAC address, lowercase, no separators (e.g., `omninode-a1b2c3`).

### 3.2 Phase 2+ Provisioned Boot

**First boot (no saved WiFi credentials in NVS):**

1. Initialize hardware: configure GPIO pins, set pin modes
2. Check NVS for saved WiFi credentials: none found
3. Start WiFi softAP: SSID `OmniNode-{last4MAC}`, open network (no password)
4. Start captive portal HTTP server on `192.168.4.1`, port 80
5. Serve provisioning page: WiFi network scan/selector, password field
6. On form submission:
   a. Validate WiFi credentials by attempting connection (timeout: 15 seconds)
   b. If connection fails: return error on portal page, allow retry
   c. If connection succeeds: generate bearer token (32-character hex string via hardware RNG)
   d. Display token on confirmation page with clear instructions to save it
   e. Save SSID, password, and token to NVS
   f. Reboot after 5-second delay (to allow user to copy token)

**Subsequent boot (credentials saved in NVS):**

1. Initialize hardware: configure GPIO pins, set pin modes
2. Check NVS for saved WiFi credentials: found
3. Connect to WiFi (timeout: 15 seconds per attempt, 3 attempts max)
4. If all attempts fail: fall back to captive portal mode (softAP)
5. If connected: start mDNS, start HTTP server, initialize radio peripherals
6. Puck is operational and discoverable

### 3.3 Recovery Boot (Power-Cycle Factory Reset)

**Trigger:** 5 power cycles within 15 seconds.

**Mechanism:**
1. On every boot, read `boot_count` from NVS and increment it
2. Start a 3-second timer
3. If the puck stays powered for 3 continuous seconds: reset `boot_count` to 0 (normal boot)
4. If `boot_count` reaches 5 before the timer ever completes: factory reset triggered
5. Factory reset wipes: WiFi SSID, WiFi password, bearer token
6. Reboot into first-boot captive portal mode

**Deliberate tradeoff acknowledged:** This mechanism adds one NVS write per boot and has a small risk of accidental triggering from unstable power. This is accepted because it requires zero GPIO, zero physical buttons, and works in any form factor from breadboard to sealed enclosure. For bench development, firmware may also support a jumper-to-ground reset on D2 (GPIO2) as a convenience; this is not part of the production spec.

---

## 4. Network & Discovery

### 4.1 WiFi

- 802.11ax (WiFi 6) via the ESP32C6's native radio
- DHCP client: puck receives its IP from the network router
- Automatic reconnection on WiFi dropout (silent, no reboot)
- If reconnection fails for 60 continuous seconds: reboot

### 4.2 mDNS

| Field | Value |
|-------|-------|
| Hostname | `omninode-{mac6}.local` |
| Service type | `_omninode._tcp` |
| Port | `80` |
| TXT: `fw` | Firmware version string (e.g., `0.1.0`) |
| TXT: `api` | API version string (e.g., `1.0`) |
| TXT: `protocols` | Comma-separated enabled protocols (e.g., `ir` or `ir,rf,nfc,ble`) |

The TXT records allow an agent to determine puck capabilities from mDNS alone, before making any HTTP request.

---

## 5. Authentication

### 5.1 Scheme

Bearer token in the `Authorization` header.

```
Authorization: Bearer {token}
```

### 5.2 Token Specification

- Length: 32 characters
- Character set: lowercase hexadecimal (`0-9`, `a-f`)
- Generated via the ESP32C6 hardware random number generator at first provisioning
- Stored in NVS
- Displayed once on the captive portal confirmation page
- Never displayed again after provisioning

### 5.3 Authentication Rules

| Endpoint | Auth Required |
|----------|---------------|
| `GET /` | No |
| `GET /openapi.json` | No |
| All other endpoints | Yes (Phase 2+) |

- Phase 1 firmware does not implement authentication. All endpoints are open.
- Phase 2+ firmware requires the bearer token on all endpoints except `GET /` and `GET /openapi.json`.
- Missing token: `401 Unauthorized`
- Invalid token: `401 Unauthorized`
- Malformed header (not `Bearer {token}`): `401 Unauthorized`

### 5.4 Transport Security

- Phase 1 and Phase 2: HTTP over local LAN. Bearer token provides application-level auth.
- Consumer release (post-MVP): minimum bar is transport encryption (HTTPS with self-signed certificate or equivalent secure local transport). The specific implementation is deferred to a post-MVP firmware revision.
- The open-source community is expected to contribute: certificate-based mutual TLS, token rotation, audit logging, and per-device permission scoping.

This is the honest security posture. Local HTTP with a bearer token is appropriate for development and trusted home networks. It is not appropriate for a publicly marketed consumer product.

---

## 6. API Response Format

### 6.1 Standard Envelope

All operational API endpoints (everything except `GET /` and `GET /openapi.json`) use a consistent JSON envelope.

**Success:**
```json
{
  "success": true,
  "data": { }
}
```

**Error:**
```json
{
  "success": false,
  "error": "error_code_string",
  "message": "Human-readable description of the error"
}
```

### 6.2 Exempt Endpoints

| Endpoint | Response format |
|----------|----------------|
| `GET /` | Returns the manifest JSON object directly (see Section 7) |
| `GET /openapi.json` | Returns the raw OpenAPI 3.0 JSON document |

These endpoints are exempt from the standard envelope because:
- The manifest at `GET /` is a self-contained identity document with its own schema.
- The OpenAPI spec at `GET /openapi.json` must be parseable by standard OpenAPI tooling, which expects the raw spec.

### 6.3 Standard HTTP Status Codes

| Code | Error String | Meaning |
|------|-------------|---------|
| `200` | — | Success |
| `400` | `bad_request` | Malformed JSON, missing required fields, payload exceeds limits |
| `401` | `unauthorized` | Missing or invalid bearer token |
| `404` | `not_found` | Endpoint does not exist in current firmware |
| `408` | `timeout` | Operation timed out (e.g., IR capture with no signal) |
| `413` | `payload_too_large` | Request body exceeds maximum size |
| `500` | `internal_error` | Firmware-level failure |
| `503` | `busy` | A radio operation is already in progress |

### 6.4 Content Type

All requests and responses use `Content-Type: application/json` unless otherwise specified (e.g., `POST /system/ota` accepts `application/octet-stream`).

---

## 7. Root Manifest — `GET /`

**Unauthenticated.** Returns a compact JSON object describing the puck's identity, firmware, capabilities, and API location.

```json
{
  "device": "omninode",
  "id": "omninode-a1b2c3",
  "mac": "AA:BB:CC:DD:EE:FF",
  "firmware_version": "0.1.0",
  "manifest_version": 1,
  "api_version": "1.0",
  "uptime_seconds": 3842,
  "protocols": ["ir"],
  "auth": {
    "scheme": "bearer",
    "header": "Authorization",
    "format": "Bearer {token}"
  },
  "endpoints": {
    "spec": "/openapi.json",
    "status": "/status"
  }
}
```

**Field definitions:**

| Field | Type | Description |
|-------|------|-------------|
| `device` | string | Always `"omninode"` |
| `id` | string | Device hostname identifier |
| `mac` | string | WiFi MAC address |
| `firmware_version` | string | Semantic version of the firmware |
| `manifest_version` | integer | Schema version of this manifest (increment on structural changes) |
| `api_version` | string | API contract version (increment on breaking endpoint changes) |
| `uptime_seconds` | integer | Seconds since last boot |
| `protocols` | string[] | Currently enabled protocol identifiers |
| `auth` | object | Authentication scheme description (omitted in Phase 1) |
| `endpoints` | object | Key endpoint paths |

The `protocols` array grows with each firmware phase:
- Phase 1: `["ir"]`
- Phase 3: `["ir", "rf"]`
- Phase 4: `["ir", "rf", "nfc"]`
- Phase 5: `["ir", "rf", "nfc", "ble"]`

An agent can check `manifest_version` and `api_version` to reason about compatibility without diffing the full spec.

---

## 8. Full API Specification — `GET /openapi.json`

**Unauthenticated.** Returns the complete OpenAPI 3.0 document as raw JSON.

This endpoint returns the OpenAPI document directly — not wrapped in the standard response envelope. Standard OpenAPI parsers, SDK generators, and agent tooling expect the raw spec.

The OpenAPI spec is embedded in the firmware binary (or stored in a firmware-accessible flash partition) and served natively by the puck. It is not fetched from an external source.

The spec only describes endpoints that are enabled in the current firmware build.

The canonical source-of-truth version of the spec lives in `api-spec/openapi.yaml` in the repository. The firmware embeds a compiled JSON version of this file.

---

## 9. Status Endpoint — `GET /status`

**Authenticated (Phase 2+).** Returns detailed operational information.

```json
{
  "success": true,
  "data": {
    "device": "omninode",
    "id": "omninode-a1b2c3",
    "mac": "AA:BB:CC:DD:EE:FF",
    "firmware_version": "0.1.0",
    "api_version": "1.0",
    "uptime_seconds": 3842,
    "free_heap_bytes": 180224,
    "wifi": {
      "ssid": "MyNetwork",
      "rssi_dbm": -42,
      "ip": "192.168.1.105"
    },
    "protocols": {
      "ir": "ready",
      "rf": "not_available",
      "nfc": "not_available",
      "ble": "not_available"
    },
    "radio_busy": false
  }
}
```

**Protocol status values:**

| Status | Meaning |
|--------|---------|
| `ready` | Protocol is initialized and available for operations |
| `busy` | Protocol is currently executing an operation |
| `not_initialized` | Hardware is present but protocol firmware is not enabled |
| `not_available` | Hardware is not wired or not detected at boot |
| `error` | Protocol initialization failed |

---

## 10. Concurrency Rule

**One active radio operation at a time, globally.**

The puck does not support concurrent radio operations across any protocol. If an IR capture is in progress and an RF blast is requested, the RF blast returns `503 busy`. If a BLE scan is running and an IR blast is requested, the IR blast returns `503 busy`.

This is a deliberate simplification for V1. The ESP32C6 has limited RAM, shared bus resources (SPI, I2C), and concurrent radio state management on a microcontroller is a significant source of firmware bugs. Reliability is more important than parallelism.

The `radio_busy` field in `GET /status` tells the agent whether an operation is currently in progress.

Requests to non-radio endpoints (`GET /`, `GET /openapi.json`, `GET /status`, `POST /system/reboot`) are always available regardless of radio state.

---

## 11. IR Endpoints (Phase 1)

### 11.1 Signal Format

**Raw timing arrays.** An array of unsigned integers representing alternating mark (IR LED on) and space (IR LED off) durations in microseconds. The first value is always a mark.

This format is protocol-agnostic. It works with NEC, Sony, RC5, RC6, Samsung, Panasonic, and every other consumer IR protocol without the firmware needing to decode any of them. The agent is responsible for protocol-level interpretation if desired.

### 11.2 `POST /ir/capture`

Activates the IR receiver and waits for a signal. Returns the raw timing data.

**Request:**
```json
{
  "timeout_ms": 10000
}
```

| Field | Type | Required | Default | Constraints |
|-------|------|----------|---------|-------------|
| `timeout_ms` | integer | No | `10000` | Min: `1000`, Max: `30000` |

**Success response (200):**
```json
{
  "success": true,
  "data": {
    "raw": [9000, 4500, 560, 560, 560, 1690, 560, 560, 560, 1690],
    "length": 10
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `raw` | integer[] | Microsecond timing values: mark, space, mark, space, ... |
| `length` | integer | Number of elements in the `raw` array |

**Note on carrier frequency:** The VS1838B is a demodulating receiver that strips the carrier frequency and outputs only the timing envelope. It cannot measure the actual carrier. The capture response therefore does not include a `frequency_khz` field. The standard consumer IR carrier frequency of 38 kHz is assumed for transmission (see `POST /ir/blast`).

**Error responses:**

| Code | Error | Condition |
|------|-------|-----------|
| `408` | `timeout` | No IR signal received within `timeout_ms` |
| `503` | `busy` | A radio operation is already in progress |

### 11.3 `POST /ir/blast`

Transmits a raw IR signal via the IR LED.

**Request:**
```json
{
  "raw": [9000, 4500, 560, 560, 560, 1690, 560, 560, 560, 1690],
  "frequency_khz": 38,
  "repeat": 1
}
```

| Field | Type | Required | Default | Constraints |
|-------|------|----------|---------|-------------|
| `raw` | integer[] | Yes | — | Min length: `2`, Max length: `1024` |
| `frequency_khz` | integer | No | `38` | Min: `30`, Max: `60` |
| `repeat` | integer | No | `1` | Min: `1`, Max: `10` |

**Success response (200):**
```json
{
  "success": true,
  "data": {
    "length": 10,
    "frequency_khz": 38,
    "repeat": 1
  }
}
```

**Note:** IR is a one-way protocol. The puck cannot confirm that the target device received the signal. The `success: true` response means the IR LED fired the signal. The agent is responsible for verification (e.g., asking the user or checking device state through other means).

**Error responses:**

| Code | Error | Condition |
|------|-------|-----------|
| `400` | `bad_request` | Missing `raw` field, empty array, or array exceeds 1024 elements |
| `503` | `busy` | A radio operation is already in progress |

---

## 12. RF Endpoints (Phase 3)

### 12.1 Scope Boundary

Phase 3 supports **fixed-code, OOK/ASK modulated sub-GHz devices** operating at common frequencies (433.92 MHz, 315 MHz). This covers the majority of:
- Fixed-code garage door remotes (pre-rolling-code era)
- RF power outlets and switches
- Ceiling fan remotes
- Simple wireless doorbells
- Fixed-code gate/barrier remotes

**Explicitly out of scope:**
- Rolling-code systems (KeeLoq, Security+, etc.) — these are cryptographically protected and cannot be replayed
- FSK, GFSK, or other non-OOK modulation schemes
- Frequency-hopping devices
- Devices requiring bidirectional RF communication

Raw capture-and-replay of unknown devices is best-effort, not guaranteed. The CC1101 is a configurable transceiver, not a software-defined radio.

### 12.2 `POST /rf/capture`

Activates the CC1101 receiver and waits for a signal.

**Request:**
```json
{
  "frequency_mhz": 433.92,
  "timeout_ms": 10000
}
```

| Field | Type | Required | Default | Constraints |
|-------|------|----------|---------|-------------|
| `frequency_mhz` | number | No | `433.92` | Allowed: `433.92`, `315.0` |
| `timeout_ms` | integer | No | `10000` | Min: `1000`, Max: `30000` |

**Success response (200):**
```json
{
  "success": true,
  "data": {
    "raw": [350, 1050, 350, 1050, 1050, 350, 350, 1050],
    "length": 8,
    "frequency_mhz": 433.92,
    "modulation": "OOK"
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `raw` | integer[] | Microsecond timing values (pulse, gap, pulse, gap, ...) |
| `length` | integer | Number of elements in the `raw` array |
| `frequency_mhz` | number | Capture frequency |
| `modulation` | string | Detected modulation type (typically `"OOK"`) |

**Error responses:**

| Code | Error | Condition |
|------|-------|-----------|
| `408` | `timeout` | No RF signal captured within `timeout_ms` |
| `503` | `busy` | A radio operation is already in progress |

### 12.3 `POST /rf/blast`

Transmits a raw RF signal via the CC1101.

**Request:**
```json
{
  "raw": [350, 1050, 350, 1050, 1050, 350, 350, 1050],
  "frequency_mhz": 433.92,
  "repeat": 3
}
```

| Field | Type | Required | Default | Constraints |
|-------|------|----------|---------|-------------|
| `raw` | integer[] | Yes | — | Min length: `2`, Max length: `1024` |
| `frequency_mhz` | number | No | `433.92` | Allowed: `433.92`, `315.0` |
| `repeat` | integer | No | `3` | Min: `1`, Max: `10` |

Note: RF default repeat is `3` (not `1` like IR) because sub-GHz devices commonly require multiple transmissions for reliable reception.

**Success response (200):**
```json
{
  "success": true,
  "data": {
    "length": 8,
    "frequency_mhz": 433.92,
    "repeat": 3
  }
}
```

**Error responses:**

| Code | Error | Condition |
|------|-------|-----------|
| `400` | `bad_request` | Missing `raw` field, empty array, array exceeds 1024 elements, or invalid frequency |
| `503` | `busy` | A radio operation is already in progress |

---

## 13. NFC Endpoints (Phase 4)

### 13.1 `POST /nfc/read`

Polls the PN532 for an NFC tag. Waits until a tag is presented or timeout.

**Request:**
```json
{
  "timeout_ms": 10000
}
```

| Field | Type | Required | Default | Constraints |
|-------|------|----------|---------|-------------|
| `timeout_ms` | integer | No | `10000` | Min: `1000`, Max: `30000` |

**Success response (200):**
```json
{
  "success": true,
  "data": {
    "uid": "04A23BCCDDEE",
    "uid_length": 7,
    "tag_type": "NTAG215",
    "ndef": [
      {
        "type": "text",
        "payload": "living_room_scene_1"
      }
    ]
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `uid` | string | Tag UID as hex string (no separators) |
| `uid_length` | integer | UID length in bytes (4 or 7 typically) |
| `tag_type` | string | Detected tag type (e.g., `"NTAG215"`, `"MIFARE_Classic"`, `"unknown"`) |
| `ndef` | object[] | Array of NDEF records (empty array if no NDEF data) |

**Error responses:**

| Code | Error | Condition |
|------|-------|-----------|
| `408` | `timeout` | No tag presented within `timeout_ms` |
| `503` | `busy` | A radio operation is already in progress |

### 13.2 `POST /nfc/write`

Writes an NDEF text record to a writable NFC tag. Tag must be presented before or during the operation.

**Request:**
```json
{
  "timeout_ms": 10000,
  "ndef": [
    {
      "type": "text",
      "payload": "living_room_scene_1"
    }
  ]
}
```

| Field | Type | Required | Default | Constraints |
|-------|------|----------|---------|-------------|
| `timeout_ms` | integer | No | `10000` | Min: `1000`, Max: `30000` |
| `ndef` | object[] | Yes | — | Max: `4` records, max `256` bytes total payload |

**Success response (200):**
```json
{
  "success": true,
  "data": {
    "uid": "04A23BCCDDEE",
    "records_written": 1
  }
}
```

**Error responses:**

| Code | Error | Condition |
|------|-------|-----------|
| `400` | `bad_request` | Missing or malformed NDEF data, payload exceeds limits |
| `408` | `timeout` | No writable tag presented within `timeout_ms` |
| `422` | `write_failed` | Tag is read-only or write operation failed |
| `503` | `busy` | A radio operation is already in progress |

---

## 14. BLE Endpoints (Phase 5)

### 14.1 Coexistence Rule

BLE and WiFi share radio time on the ESP32C6. If BLE operations cause WiFi API response latency to exceed 2000ms, BLE operations must be deprioritized or terminated. **WiFi stability is never sacrificed for BLE.**

### 14.2 `POST /ble/scan`

Performs an active BLE scan and returns discovered devices.

**Request:**
```json
{
  "duration_ms": 5000
}
```

| Field | Type | Required | Default | Constraints |
|-------|------|----------|---------|-------------|
| `duration_ms` | integer | No | `5000` | Min: `1000`, Max: `15000` |

**Success response (200):**
```json
{
  "success": true,
  "data": {
    "devices": [
      {
        "mac": "AA:BB:CC:DD:EE:FF",
        "name": "Living Room Speaker",
        "rssi_dbm": -55,
        "service_uuids": ["0000ffe0-0000-1000-8000-00805f9b34fb"]
      }
    ],
    "count": 1,
    "duration_ms": 5000
  }
}
```

**Error responses:**

| Code | Error | Condition |
|------|-------|-----------|
| `503` | `busy` | A radio operation is already in progress |

### 14.3 `POST /ble/connect`

Establishes a GATT connection to a BLE device and returns its service/characteristic map.

**Request:**
```json
{
  "mac": "AA:BB:CC:DD:EE:FF",
  "timeout_ms": 10000
}
```

| Field | Type | Required | Default | Constraints |
|-------|------|----------|---------|-------------|
| `mac` | string | Yes | — | Format: `XX:XX:XX:XX:XX:XX` |
| `timeout_ms` | integer | No | `10000` | Min: `1000`, Max: `30000` |

**Success response (200):**
```json
{
  "success": true,
  "data": {
    "mac": "AA:BB:CC:DD:EE:FF",
    "connected": true,
    "services": [
      {
        "uuid": "0000ffe0-0000-1000-8000-00805f9b34fb",
        "characteristics": [
          {
            "uuid": "0000ffe1-0000-1000-8000-00805f9b34fb",
            "properties": ["read", "write", "notify"]
          }
        ]
      }
    ]
  }
}
```

**Error responses:**

| Code | Error | Condition |
|------|-------|-----------|
| `400` | `bad_request` | Invalid MAC address format |
| `408` | `timeout` | Connection attempt timed out |
| `503` | `busy` | A radio operation is already in progress |

### 14.4 `POST /ble/write`

Writes a value to a BLE GATT characteristic on a connected device.

**Request:**
```json
{
  "mac": "AA:BB:CC:DD:EE:FF",
  "service_uuid": "0000ffe0-0000-1000-8000-00805f9b34fb",
  "characteristic_uuid": "0000ffe1-0000-1000-8000-00805f9b34fb",
  "value_base64": "AQ==",
  "timeout_ms": 5000
}
```

| Field | Type | Required | Default | Constraints |
|-------|------|----------|---------|-------------|
| `mac` | string | Yes | — | Must be previously connected |
| `service_uuid` | string | Yes | — | Full 128-bit UUID string |
| `characteristic_uuid` | string | Yes | — | Full 128-bit UUID string |
| `value_base64` | string | Yes | — | Base64-encoded, max `512` bytes decoded |
| `timeout_ms` | integer | No | `5000` | Min: `1000`, Max: `15000` |

**Success response (200):**
```json
{
  "success": true,
  "data": {
    "written": true,
    "bytes": 1
  }
}
```

**Error responses:**

| Code | Error | Condition |
|------|-------|-----------|
| `400` | `bad_request` | Missing fields, invalid UUIDs, or payload too large |
| `404` | `not_found` | Device not connected, service not found, or characteristic not found |
| `408` | `timeout` | Write operation timed out |
| `503` | `busy` | A radio operation is already in progress |

### 14.5 `POST /ble/read`

Reads a value from a BLE GATT characteristic on a connected device.

**Request:**
```json
{
  "mac": "AA:BB:CC:DD:EE:FF",
  "service_uuid": "0000ffe0-0000-1000-8000-00805f9b34fb",
  "characteristic_uuid": "0000ffe1-0000-1000-8000-00805f9b34fb",
  "timeout_ms": 5000
}
```

| Field | Type | Required | Default | Constraints |
|-------|------|----------|---------|-------------|
| `mac` | string | Yes | — | Must be previously connected |
| `service_uuid` | string | Yes | — | Full 128-bit UUID string |
| `characteristic_uuid` | string | Yes | — | Full 128-bit UUID string |
| `timeout_ms` | integer | No | `5000` | Min: `1000`, Max: `15000` |

**Success response (200):**
```json
{
  "success": true,
  "data": {
    "value_base64": "AQ==",
    "bytes": 1
  }
}
```

**Error responses:**

| Code | Error | Condition |
|------|-------|-----------|
| `400` | `bad_request` | Missing fields or invalid UUIDs |
| `404` | `not_found` | Device not connected, service not found, or characteristic not found |
| `408` | `timeout` | Read operation timed out |
| `503` | `busy` | A radio operation is already in progress |

---

## 15. Zigbee / Thread — Exclusion Statement

Zigbee and Thread endpoints are **not specified in this PRD**. The ESP32C6 has an 802.15.4 radio capable of these protocols, but:

- Zigbee and WiFi share the physical radio and require time-division coexistence that has not been validated.
- Thread/Matter requires significant firmware stack work beyond radio support.
- Shipping unstable Zigbee support would undermine the reliability of the core WiFi control path.

If Phase 5 BLE + WiFi coexistence testing is successful, a Zigbee/Thread investigation may begin. Any resulting endpoints would be specified in a future PRD revision. This exclusion is intentional, not an oversight.

---

## 16. System Endpoints (Phase 2+)

### 16.1 `POST /system/reboot`

Reboots the puck. The response is sent before the reboot begins.

**Request:** Empty body or `{}`.

**Success response (200):**
```json
{
  "success": true,
  "data": {
    "rebooting": true,
    "delay_ms": 1000
  }
}
```

The puck waits 1 second after sending the response, then reboots. This ensures the HTTP response is fully transmitted.

### 16.2 `POST /system/ota`

Accepts a firmware binary and flashes it to the secondary OTA partition.

**Request:**
- Content-Type: `application/octet-stream`
- Body: raw firmware binary
- Max size: `1048576` bytes (1 MB)

**Success response (200):**
```json
{
  "success": true,
  "data": {
    "flashed": true,
    "size_bytes": 524288,
    "rebooting": true,
    "delay_ms": 2000
  }
}
```

**OTA Safety:**
- The ESP32C6 uses a dual-partition OTA scheme. New firmware is written to the inactive partition.
- After reboot, if the new firmware fails to connect to WiFi within 60 seconds, the bootloader automatically rolls back to the previous partition.
- A corrupted binary or power loss during flashing does not brick the device; the previous firmware remains on the other partition.

**Error responses:**

| Code | Error | Condition |
|------|-------|-----------|
| `400` | `bad_request` | No binary data in request body |
| `413` | `payload_too_large` | Binary exceeds 1 MB |
| `500` | `ota_failed` | Flash write failed |

---

## 17. No Code Storage on the Puck

The puck does **not** provide `/ir/codes`, `/rf/codes`, or any CRUD endpoints for storing, labeling, or retrieving learned signal libraries.

When the agent wants to blast a signal, it sends the full raw payload in the request body. The puck executes the transmission and forgets the payload. It does not store it, label it, or associate it with a device name.

**Rationale:** The agent (OpenClaw or equivalent) is the system of record for all device knowledge, code libraries, and semantic labels. The puck is a stateless radio executor.

**Acknowledged tradeoff:** If the agent goes down, the puck cannot replay any previously learned commands on its own. It becomes inert until the agent reconnects. This is accepted for V1. A future firmware version may add a small local command cache for resilience (e.g., storing the last N commands with labels in NVS), but this is not a V1 requirement and is not specified in this PRD.

---

## 18. Hardware Pin Assignments

Resolved against the [Seeed Studio XIAO ESP32C6 pinout documentation](https://wiki.seeedstudio.com/xiao_esp32c6_getting_started/).

All assignments use default bus pins to avoid remapping complexity.

### 18.1 Pin Table

| Function | XIAO Pin | ESP32C6 GPIO | Interface | Phase | Notes |
|----------|----------|-------------|-----------|-------|-------|
| IR LED (transmit) | D0 | GPIO0 | PWM output | 1 | PWM-capable; drives LED via 100ohm resistor |
| IR Receiver (VS1838B) | D1 | GPIO1 | Digital input | 1 | Interrupt-capable for signal edge detection |
| PN532 NFC SDA | D4 | GPIO22 | I2C data | 4 | Default I2C SDA |
| PN532 NFC SCL | D5 | GPIO23 | I2C clock | 4 | Default I2C SCL |
| CC1101 SPI MOSI | D10 | GPIO18 | SPI data out | 3 | Default SPI MOSI |
| CC1101 SPI MISO | D9 | GPIO20 | SPI data in | 3 | Default SPI MISO |
| CC1101 SPI SCK | D8 | GPIO19 | SPI clock | 3 | Default SPI SCK |
| CC1101 SPI CS | D3 | GPIO21 | SPI chip select | 3 | Standard CS pin |
| *Reserved: UART TX* | D6 | GPIO16 | UART | — | Debug serial output |
| *Reserved: UART RX* | D7 | GPIO17 | UART | — | Debug serial input |
| *Free* | D2 | GPIO2 | — | — | Available for future expansion or dev-only reset jumper |

### 18.2 Summary

- **8 pins** allocated to radio peripherals
- **2 pins** reserved for debug UART (serial monitor during development)
- **1 pin** free for future use (D2 / GPIO2)
- **0 pins** used for buttons (none exist on the OmniNode by design)

### 18.3 Power & Ground

- **3V3**: Powers PN532 and CC1101 modules
- **GND**: Common ground for all external modules
- **USB-C**: Powers the ESP32C6 board and all connected peripherals

### 18.4 IR LED Circuit

- IR LED anode → 100ohm resistor → GPIO0 (D0)
- IR LED cathode → GND
- The 100ohm resistor limits current to protect both the GPIO pin and the LED
- For increased range, multiple IR LEDs can be wired in parallel with appropriate current limiting

### 18.5 VS1838B Circuit

- VS1838B signal pin → GPIO1 (D1)
- VS1838B VCC → 3V3
- VS1838B GND → GND

---

## 19. NVS Storage Schema

All persistent data is stored in the ESP32C6's Non-Volatile Storage (NVS) flash partition.

| Namespace | Key | Type | Max Size | Purpose | Written When |
|-----------|-----|------|----------|---------|--------------|
| `wifi` | `ssid` | string | 32 bytes | Saved WiFi network name | Provisioning |
| `wifi` | `pass` | string | 64 bytes | Saved WiFi password | Provisioning |
| `auth` | `token` | string | 32 bytes | Bearer token (hex) | Provisioning |
| `device` | `id` | string | 20 bytes | Device hostname | First boot |
| `system` | `boot_cnt` | uint8 | 1 byte | Power-cycle reset counter | Every boot |
| `system` | `fw_ver` | string | 12 bytes | Firmware version | Firmware flash |

**Total NVS usage:** Under 200 bytes. Minimal wear on flash.

**Factory reset clears:** `wifi/ssid`, `wifi/pass`, `auth/token`, `system/boot_cnt`. Device identity and firmware version are preserved.

---

## 20. Payload Limits

Hard limits to prevent memory exhaustion on the microcontroller.

| Limit | Value | Rationale |
|-------|-------|-----------|
| Max request body size | 8 KB | ESP32C6 has ~320KB SRAM; leave headroom for stack and buffers |
| Max IR timing array length | 1024 elements | Covers the longest known consumer IR signals (some AC remotes exceed 200 elements) |
| Max RF timing array length | 1024 elements | Consistent with IR |
| Max IR/RF repeat count | 10 | Prevents runaway transmission loops |
| Max capture timeout | 30,000 ms | 30 seconds; prevents indefinite resource lock |
| Max BLE scan duration | 15,000 ms | 15 seconds; longer scans risk WiFi instability |
| Max BLE write payload | 512 bytes (decoded) | Standard BLE MTU limits |
| Max NFC NDEF payload | 256 bytes total | Typical NTAG capacity |
| Max NFC NDEF records | 4 | Prevent overly complex write operations |
| Max OTA binary size | 1,048,576 bytes (1 MB) | ESP32C6 OTA partition size |

Any request exceeding these limits returns `400 bad_request` or `413 payload_too_large`.

---

## 21. Timing & Performance Targets

| Metric | Target | Hard Limit |
|--------|--------|------------|
| WiFi connection on boot | < 10 seconds | 45 seconds (3 attempts x 15 sec) before fallback |
| mDNS discoverability after WiFi | < 3 seconds | 5 seconds |
| API response (non-radio) | < 100 ms | 500 ms |
| IR blast execution | < 50 ms from request | 200 ms |
| IR capture (signal present) | < 500 ms to return | Bounded by `timeout_ms` |
| RF blast execution | < 100 ms from request | 300 ms |
| Full round trip (agent → puck → response) | < 500 ms | 1000 ms |
| BLE scan completion | Bounded by `duration_ms` | 15,000 ms max |
| WiFi API latency during BLE | < 500 ms | 2000 ms (BLE deprioritized if exceeded) |

---

## 22. Edge Cases & Error Handling

| Scenario | Behavior |
|----------|----------|
| WiFi disconnects mid-operation | Puck attempts silent reconnection. In-flight HTTP requests are lost. Agent should retry. |
| WiFi reconnection fails for 60 seconds | Puck reboots |
| IR capture with no signal | Returns `408 timeout` after `timeout_ms` |
| IR blast with empty or malformed `raw` array | Returns `400 bad_request` |
| RF capture on unsupported modulation | Best-effort capture; may return noisy or unusable timing data. Agent decides whether to retry. |
| Two simultaneous radio requests | Second request returns `503 busy` |
| NVS storage full | Returns `500 internal_error`. Should not occur given the minimal schema. |
| OTA with corrupted binary | ESP32 OTA rollback reverts to previous firmware on next boot |
| Power loss during OTA write | Dual-partition scheme protects against bricking; previous firmware remains intact |
| Agent sends request to non-existent endpoint | Returns `404 not_found` |
| Agent sends request to endpoint not enabled in current firmware phase | Returns `404 not_found` (endpoint is simply not registered) |
| Request body exceeds 8 KB | Returns `413 payload_too_large` |
| BLE device disconnects during write | Returns `408 timeout` or `500 internal_error` depending on failure mode |
| Captive portal: user enters wrong WiFi password | Portal validates connection, returns error, allows retry without reboot |
| Bearer token lost by user | Factory reset via power-cycle sequence, then re-provision |

---

## 23. Phased Implementation Details

### Phase 1: IR Proof of Concept (MVP)

**Build scope:**
- WiFi connection (hardcoded credentials)
- mDNS broadcast
- HTTP server
- `GET /` manifest
- `GET /openapi.json` (IR endpoints only)
- `GET /status`
- `POST /ir/capture`
- `POST /ir/blast`
- No authentication
- No captive portal

**Hardware wiring:** ESP32C6 + IR LED (D0) + 100ohm resistor + VS1838B (D1) on breadboard. USB-C power.

**Success criteria:**
- Puck is discoverable via `avahi-browse _omninode._tcp` or equivalent
- `curl http://omninode-{mac6}.local/` returns valid manifest JSON
- `curl http://omninode-{mac6}.local/openapi.json` returns valid OpenAPI document containing only IR endpoints
- `curl -X POST http://omninode-{mac6}.local/ir/blast -d '{"raw":[9000,4500,560,560,560,1690,560,560]}'` fires the IR LED (verifiable via phone camera)
- A real TV responds to a transmitted IR power signal
- `POST /ir/capture` returns captured timing data when a remote is pointed at the puck
- Puck reconnects to WiFi and resumes mDNS after a power cycle
- API response times meet targets in Section 21

**If this works, the product thesis is proven.**

### Phase 2: Provisioning, Auth & OTA

**Build scope (additive):**
- Captive portal WiFi provisioning
- Bearer token generation and display
- Authentication on all operational endpoints
- Power-cycle factory reset
- `POST /system/reboot`
- `POST /system/ota` with rollback

**Success criteria:**
- Fresh puck boots into captive portal
- User provisions WiFi and receives bearer token without serial access
- Authenticated requests succeed; unauthenticated requests return `401`
- Power-cycle reset returns puck to captive portal mode
- OTA update flashes new firmware; rollback works on failure
- All Phase 1 endpoints remain functional

### Phase 3: Fixed-Code Sub-GHz RF

**Build scope (additive):**
- CC1101 initialization on SPI bus
- `POST /rf/capture`
- `POST /rf/blast`
- Updated manifest and OpenAPI spec

**Hardware wiring:** Add CC1101 to SPI bus (D3, D8, D9, D10).

**Success criteria:**
- CC1101 initializes without disrupting existing WiFi or IR
- RF capture reliably records a fixed-code 433.92 MHz remote signal
- RF blast replays the signal and target device responds
- All Phase 1 and 2 endpoints remain functional

### Phase 4: NFC

**Build scope (additive):**
- PN532 initialization on I2C bus
- `POST /nfc/read`
- `POST /nfc/write`
- Updated manifest and OpenAPI spec

**Hardware wiring:** Add PN532 to I2C bus (D4, D5).

**Success criteria:**
- PN532 initializes without disrupting existing peripherals
- NFC read returns tag UID and NDEF payload
- NFC write successfully writes data to a writable tag
- All previous phase endpoints remain functional

### Phase 5: BLE (Experimental)

**Build scope (additive):**
- BLE scan, connect, read, write endpoints
- WiFi + BLE coexistence validation
- Updated manifest and OpenAPI spec

**Success criteria:**
- BLE scan returns nearby devices
- A BLE device responds to a write command
- WiFi API latency stays under 2000ms during BLE operations
- All previous phase endpoints remain functional
- If WiFi stability is compromised, BLE endpoints may be disabled or marked experimental in the spec

---

## 24. Document Relationships

| Document | Location | Purpose |
|----------|----------|---------|
| Project Summary | `docs/ProjectSummary.md` | Vision, architecture, strategy, business model |
| **PRD (this document)** | **`docs/PRD.md`** | **Firmware behavior, API contract, hardware spec** |
| OpenAPI Specification | `api-spec/openapi.yaml` | Machine-readable API spec (source of truth, embedded in firmware) |
| Wiring Diagrams | `hardware/` | Physical wiring reference for breadboard and PCB |

The PRD is the implementation contract. If the firmware behaves as specified in this document and the OpenAPI spec matches, the product is built correctly.

---

*OmniNode PRD v1.0 — March 2026*
