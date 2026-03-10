# OMNINODE — Phase 1 MVP Production Roadmap

**Build Guide & Day-by-Day Execution Plan**
*Version 1.0 — March 2026*

---

## Overview

This roadmap is a linear sequence of work blocks that takes OmniNode from documentation to a working MVP. Each block has a clear deliverable and a "done when" checkpoint. Do not move to the next block until the current one passes its checkpoint.

### The Three Layers

```
HARDWARE (breadboard)  →  FIRMWARE (software on the chip)  →  INTEGRATION (OpenClaw talks to the puck)
      Blocks 1-2                   Blocks 3-7                         Blocks 8-10
```

Hardware first because nothing works without a functioning circuit. Firmware second because the puck needs to be alive on the network before anything can talk to it. OpenAPI spec and OpenClaw integration last because they depend on a working, stable puck.

### Estimated Total Time: 15-27 hours

---

## Block 1: Repo & Tooling Setup

**Time estimate:** 1-2 hours
**Depends on:** Nothing

### Purpose

De-risk the entire project with a 30-minute smoke test. If you can't flash the chip, nothing else matters.

### Tasks

1. Install PlatformIO (VS Code extension or CLI)
2. Create the PlatformIO project inside `firmware/` targeting the Seeed Studio XIAO ESP32C6
3. Verify the toolchain: create a bare-minimum sketch that blinks the onboard LED (GPIO15), compile it, and flash it to the board via USB-C
4. Confirm the repo structure:

```
omninode/
├── firmware/          → PlatformIO project
├── api-spec/          → Empty for now (OpenAPI comes in Block 8)
├── hardware/          → Wiring notes, pinout reference
├── docs/
│   ├── ProjectSummary.md
│   ├── PRD.md
│   └── Roadmap.md
└── README.md
```

### Done When

- [ ] PlatformIO compiles without errors for the XIAO ESP32C6 target
- [ ] Firmware flashes to the board via USB-C
- [ ] The onboard LED blinks
- [ ] The chip works, USB flashing works, the toolchain works

---

## Block 2: Hardware — IR Circuit on Breadboard

**Time estimate:** 1-2 hours
**Depends on:** Block 1 (need working flash to test the circuit)

### Purpose

Wire the Phase 1 circuit. IR only — no NFC, no RF, no CC1101, no PN532 yet.

### Components

- Seeed Studio XIAO ESP32C6
- CHANZON 940nm IR LED
- VS1838B IR receiver
- 100Ω resistor
- ELEGOO breadboard
- Jumper wires
- USB-C cable

### Wiring

| Component | Pin | ESP32C6 GPIO | Connection |
|-----------|-----|-------------|------------|
| IR LED anode | D0 | GPIO0 | Through 100Ω resistor |
| IR LED cathode | — | — | GND |
| VS1838B signal | D1 | GPIO1 | Direct |
| VS1838B VCC | — | — | 3V3 |
| VS1838B GND | — | — | GND |

**Important:** Check the VS1838B datasheet for pin orientation — the pinout varies by package. Getting VCC and GND reversed will damage the receiver.

### Tasks

1. Wire the circuit on the breadboard per the pin table above
2. Double-check: IR LED polarity, resistor placement, VS1838B pin orientation
3. Write a simple test sketch: fire the IR LED in a loop on GPIO0
4. Verify IR emission with your phone camera (phone cameras can see 940nm IR as a faint purple glow)
5. Take a photo of the completed breadboard for the `hardware/` directory

### Done When

- [ ] The IR LED visibly fires (phone camera test)
- [ ] The VS1838B is wired and powered (3V3 and GND confirmed)
- [ ] The circuit matches the PRD pin assignments (Section 18) exactly
- [ ] Photo saved to `hardware/`

---

## Block 3: Firmware — WiFi + mDNS

**Time estimate:** 2-4 hours
**Depends on:** Block 2

### Purpose

Get the puck on the local network and discoverable. This is the foundation — every feature after this depends on the puck being reachable.

### Tasks

1. Hardcode your WiFi SSID and password in the firmware (Phase 1 only — captive portal is Phase 2)
2. Connect to WiFi on boot
3. Handle connection failures: retry 3 times with 10-second timeout per attempt, blink onboard LED as error indicator on failure
4. Start mDNS:
   - Hostname: `omninode-{mac6}.local` (last 6 hex characters of MAC, lowercase, no separators)
   - Service type: `_omninode._tcp`
   - Port: `80`
   - TXT record: `fw=0.1.0`
   - TXT record: `protocols=ir`
5. Start an HTTP server on port 80 (ESPAsyncWebServer or equivalent)
6. Implement a temporary `GET /` that returns `{"status": "ok"}` (just to prove the server works)

### Done When

- [ ] Puck connects to WiFi automatically on boot
- [ ] `ping omninode-{mac6}.local` works from the Mac Mini
- [ ] mDNS service discovery shows the puck (e.g., `dns-sd -B _omninode._tcp` on macOS)
- [ ] `curl http://omninode-{mac6}.local/` returns a JSON response
- [ ] WiFi reconnects automatically after a brief dropout

---

## Block 4: Firmware — IR Capture

**Time estimate:** 2-4 hours
**Depends on:** Block 3

### Purpose

Make the puck receive and decode IR signals from any remote control.

### Tasks

1. Integrate an IR receiving library (IRremoteESP8266 or raw interrupt-based capture on GPIO1)
2. Implement `POST /ir/capture` endpoint per PRD Section 11.2:
   - Accept optional `timeout_ms` in JSON request body (default: 10000, min: 1000, max: 30000)
   - Activate the IR receiver in listen mode
   - On signal received: capture the raw timing array (alternating mark/space microsecond pairs)
   - Return success response:
     ```json
     {"success": true, "data": {"raw": [...], "length": N}}
     ```
   - On timeout with no signal: return `408`
   - If already capturing: return `503 busy`
3. Implement the global radio concurrency lock (one radio operation at a time; any overlapping request returns `503`)

### Done When

- [ ] Point any IR remote at the breadboard, press a button
- [ ] `curl -X POST http://omninode-{mac6}.local/ir/capture -H "Content-Type: application/json" -d '{}'` returns the raw timing array
- [ ] Different remote buttons produce different timing arrays
- [ ] Timeout works correctly (returns 408 after the specified duration with no signal)
- [ ] A second capture request while one is in progress returns 503 busy
- [ ] The timing data looks plausible (NEC protocol: first mark ~9000μs, first space ~4500μs)

---

## Block 5: Firmware — IR Blast

**Time estimate:** 2-3 hours
**Depends on:** Block 4

### Purpose

Make the puck transmit IR signals. This is where the product thesis gets proven.

### Tasks

1. Implement IR transmit on GPIO0 (PWM at 38kHz carrier modulated by the raw timing array)
2. Implement `POST /ir/blast` endpoint per PRD Section 11.3:
   - Accept required `raw` (integer array), optional `frequency_khz` (default: 38), optional `repeat` (default: 1)
   - Validate: `raw` length between 2 and 1024, `repeat` between 1 and 10, `frequency_khz` between 30 and 60
   - Fire the IR LED with the specified timing
   - Return success response:
     ```json
     {"success": true, "data": {"length": N, "frequency_khz": 38, "repeat": 1}}
     ```
   - Return `400` for invalid payloads, `503` if a radio operation is in progress

### Validation Test

1. Use `POST /ir/capture` to capture the power button signal from your TV remote
2. Copy the `raw` array from the capture response
3. Send it back via `POST /ir/blast`
4. **The TV physically responds**

### Done When

- [ ] Captured IR signal can be replayed via blast
- [ ] **A real TV turns on or off in response to the blast command**
- [ ] Repeat parameter works (signal sent multiple times)
- [ ] Invalid payloads return 400
- [ ] Busy state returns 503

**This is the critical milestone. If the TV responds, the product thesis is proven. Everything after this is polish.**

---

## Block 6: Firmware — Manifest + Status Endpoints

**Time estimate:** 1-2 hours
**Depends on:** Block 3 (can be done in parallel with Blocks 4-5)

### Purpose

Make the puck properly describe itself so an agent can understand what it is and what it can do.

### Tasks

1. Replace the temporary `GET /` with the full manifest per PRD Section 7:
   ```json
   {
     "device": "omninode",
     "id": "omninode-{mac6}",
     "mac": "AA:BB:CC:DD:EE:FF",
     "firmware_version": "0.1.0",
     "manifest_version": 1,
     "api_version": "1.0",
     "uptime_seconds": 3842,
     "protocols": ["ir"],
     "api_spec": "/openapi.json"
   }
   ```
   Note: No `auth` field in Phase 1 (auth is not implemented yet).

2. Implement `GET /status` per PRD Section 9:
   - Return: device info, uptime, free heap, WiFi RSSI, WiFi SSID, IP address, per-protocol status, radio_busy flag
   - IR status: `ready` or `busy`
   - All other protocols: `not_available`

### Done When

- [ ] `curl http://omninode-{mac6}.local/` returns a valid manifest matching the PRD schema
- [ ] All manifest fields contain real, accurate values (MAC is real, uptime increments, etc.)
- [ ] `curl http://omninode-{mac6}.local/status` returns accurate live operational data
- [ ] Protocol statuses correctly reflect current state (IR shows `busy` during capture, `ready` otherwise)

---

## Block 7: Firmware — Persistence + Reboot Resilience

**Time estimate:** 1-2 hours
**Depends on:** Block 3 (can be done in parallel with Blocks 4-5)

### Purpose

Make sure the puck survives power loss gracefully. Build the NVS infrastructure that Phase 2 will depend on.

### Tasks

1. Implement NVS read/write for the storage schema defined in PRD Section 19:
   - `wifi/ssid` and `wifi/pass` (stored even though hardcoded in Phase 1 — infrastructure for Phase 2)
   - `device/id` (generated and stored on first boot)
   - `system/fw_ver`
   - `system/boot_cnt` (for future power-cycle reset)
2. On boot: read device ID from NVS. If not present, generate it from MAC address and store it.
3. Test the full reboot cycle: unplug USB, plug back in, confirm everything comes back up.

### Done When

- [ ] Pull the USB cable, plug it back in
- [ ] Puck reconnects to WiFi within 15 seconds
- [ ] mDNS re-registers and puck is discoverable
- [ ] API is reachable and all endpoints respond correctly
- [ ] `GET /` returns the same device ID as before the reboot
- [ ] No manual intervention required at any point

---

## Block 8: OpenAPI Spec

**Time estimate:** 2-3 hours
**Depends on:** Blocks 5 and 6 (endpoints must be stable before writing the spec)

### Purpose

Write the machine-readable API contract and embed it in the firmware. This is what makes the puck self-documenting.

### Tasks

1. Write `api-spec/openapi.yaml`:
   - OpenAPI 3.0.3
   - Relative server URL: `"/"`
   - Phase 1 only: 5 endpoints, 3 tags (`discovery`, `status`, `ir`)
   - No auth scheme (Phase 1 doesn't enforce it)
   - Strict schema naming: inner payloads (`ManifestData`, `StatusData`, `IrCaptureData`, `IrBlastData`) separate from envelope
   - One shared `ErrorResponse` schema
   - Hard numeric limits in schema constraints (array lengths, timeouts, repeat counts)
   - Realistic examples for every endpoint and at least one error example
   - No future-phase content whatsoever
2. Convert `openapi.yaml` to JSON (build script or manual conversion)
3. Embed the JSON in firmware as a static `const char[]` string
4. Implement `GET /openapi.json`:
   - Returns the raw OpenAPI JSON document directly
   - **Not wrapped in the success envelope** (must be parseable by standard OpenAPI tooling)
   - Content-Type: `application/json`
5. Validate the spec with an OpenAPI linter (Spectral, swagger-cli, or Swagger Editor)

### Acceptance Criteria (from agent review)

1. Only five Phase 1 paths
2. Relative server URL
3. No auth in the spec
4. Small, compact manifest schema
5. Strict inner-payload vs wrapped-response schema naming
6. No `frequency_khz` in IR capture response
7. All action endpoints are POST
8. Hard numeric limits in the schema
9. Realistic examples for every endpoint
10. Zero future-phase content

### Done When

- [ ] `curl http://omninode-{mac6}.local/openapi.json` returns a valid OpenAPI 3.0.3 document
- [ ] The spec passes an OpenAPI linter with zero errors and zero warnings
- [ ] Every field, constraint, default, and error code in the spec matches the PRD
- [ ] An LLM can read the spec and correctly generate `curl` commands for each endpoint
- [ ] The embedded JSON fits comfortably in flash (should be well under 10KB)

---

## Block 9: End-to-End Integration Test with OpenClaw

**Time estimate:** 1-3 hours
**Depends on:** Block 8

### Purpose

The real test. Point OpenClaw at the puck and close the full loop: natural language → agent → API → IR → physical result.

### Tasks

1. Tell OpenClaw (via Telegram) that an OmniNode is on the network
2. Give OpenClaw the hostname or let it discover via mDNS
3. Let OpenClaw read `GET /` (manifest)
4. Let OpenClaw read `GET /openapi.json` (full API spec)
5. Ask OpenClaw to learn your TV remote:
   - OpenClaw should call `POST /ir/capture`
   - OpenClaw should prompt you via Telegram: "Point your remote at the puck and press Power"
   - OpenClaw receives the raw timing data and stores it on its end
6. Ask OpenClaw to turn off the TV:
   - OpenClaw should call `POST /ir/blast` with the stored raw timing array
   - **The TV turns off**

### The Moment of Truth

You text OpenClaw: "Turn off the TV."

The TV turns off.

No integration code written by hand. No bridge software. No middleware. OpenClaw discovered the puck, read its API, learned the remote codes, and executed the command autonomously.

### Done When

- [ ] OpenClaw discovers or is given the puck address
- [ ] OpenClaw reads and understands the manifest and API spec
- [ ] OpenClaw successfully calls `POST /ir/capture` and receives timing data
- [ ] OpenClaw successfully calls `POST /ir/blast` and the TV responds
- [ ] The full loop works through natural language: "turn off the TV" → TV turns off
- [ ] No hand-written integration code, no middleware, no manual API calls

---

## Block 10: Cleanup + Documentation

**Time estimate:** 1-2 hours
**Depends on:** Block 9

### Purpose

Wrap Phase 1 as a complete, reproducible deliverable.

### Tasks

1. Clean up firmware code:
   - Remove debug `Serial.println` statements (or gate them behind a debug flag)
   - Remove any hardcoded test values
   - Ensure all TODO comments are resolved
2. Write `README.md` for the repo:
   - What OmniNode is (one paragraph)
   - What you need (parts list)
   - How to wire the breadboard (link to `hardware/` photo/diagram)
   - How to flash the firmware
   - How to test (curl examples)
   - How to connect to OpenClaw (or any agent)
3. Add wiring diagram or annotated photo to `hardware/`
4. Final commit and git tag: `v0.1.0`
5. Optional: record a short demo video (text OpenClaw → TV turns off)

### Done When

- [ ] Someone could clone the repo, follow the README, build the breadboard, flash the firmware, and have a working OmniNode puck
- [ ] All code is clean and documented
- [ ] `v0.1.0` is tagged in git
- [ ] Phase 1 MVP is complete

---

## Block Summary

| Block | What | Est. Hours | Depends On | Critical? |
|-------|------|-----------|------------|-----------|
| 1 | Repo + tooling setup | 1-2 | Nothing | Yes — gates everything |
| 2 | Hardware — IR breadboard | 1-2 | Block 1 | Yes — gates firmware |
| 3 | Firmware — WiFi + mDNS | 2-4 | Block 2 | Yes — gates all network features |
| 4 | Firmware — IR capture | 2-4 | Block 3 | Yes — gates IR blast |
| **5** | **Firmware — IR blast** | **2-3** | **Block 4** | **YES — THESIS PROOF** |
| 6 | Firmware — manifest + status | 1-2 | Block 3 | Parallel with 4-5 |
| 7 | Firmware — persistence | 1-2 | Block 3 | Parallel with 4-5 |
| 8 | OpenAPI spec | 2-3 | Blocks 5, 6 | Yes — gates integration |
| 9 | OpenClaw integration test | 1-3 | Block 8 | Yes — the real test |
| 10 | Cleanup + docs | 1-2 | Block 9 | Yes — makes it reproducible |
| | **Total** | **~15-27** | | |

### Parallel Opportunities

Blocks 6 and 7 can run in parallel with Blocks 4 and 5 since they depend only on Block 3 (WiFi + HTTP server), not on IR functionality. If you want to break up the IR work with some variety, interleave them:

```
Block 3 (WiFi + mDNS)
  ├── Block 4 (IR capture)
  │     └── Block 5 (IR blast)  ← THESIS PROOF
  ├── Block 6 (manifest + status)
  └── Block 7 (persistence)
Block 8 (OpenAPI spec)
Block 9 (OpenClaw integration)
Block 10 (cleanup + docs)
```

---

## What Comes After Phase 1

Once `v0.1.0` is tagged and the MVP is proven, the Phase 2 roadmap picks up:

1. **Phase 2:** Captive portal, bearer token auth, power-cycle factory reset, OTA updates
2. **Phase 3:** Wire CC1101, implement RF capture and blast
3. **Phase 4:** Wire PN532, implement NFC read and write
4. **Phase 5:** Activate on-chip BLE, validate WiFi coexistence

Each phase follows the same pattern: wire hardware → implement firmware → update OpenAPI spec → test with OpenClaw → tag release.

Phase 2+ roadmaps will be written after Phase 1 ships. One phase at a time.

---

*OmniNode Phase 1 MVP Roadmap v1.0 — March 2026*
