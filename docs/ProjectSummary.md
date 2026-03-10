# OMNINODE

**Agentic Peripheral**
*Project Summary & Strategic Architecture — March 2026*

---

## 1. Thesis

OmniNode is an open-source, local-first hardware peripheral that gives AI agents physical execution in the real world.

It is not a smart home hub. It is not an automation platform. It is not a cloud service.

OmniNode is a low-cost puck that joins a local network, advertises itself, describes its capabilities, and executes physical commands through a stable HTTP API. The reference agent is OpenClaw, but the hardware is model-agnostic: any capable local agent, automation runtime, or script can call it.

The product promise is not "zero setup" in the literal sense. The product promise is narrower and more defensible:

- One-time network provisioning and trust setup
- No per-device manufacturer apps
- No custom bridge software
- No hand-written integrations for every appliance

Once provisioned, OmniNode behaves like a discoverable physical tool that an agent can use immediately.

---

## 2. Why This Matters

The smart home industry is still built around manual integration. Users install vendor apps, pair devices one by one, and maintain brittle automations across incompatible ecosystems. Legacy devices are largely excluded because they were never designed for cloud APIs or modern smart-home standards.

At the same time, AI agents are now capable of discovery, reasoning, web research, code generation, and workflow orchestration. What they lack is physical reach.

OmniNode is that missing layer: a simple, discoverable hardware interface that lets an agent act on the physical environment without requiring manufacturer cooperation.

The wedge is not "all devices everywhere." The wedge is much simpler:

- Legacy IR devices (TVs, AC units, air purifiers, set-top boxes)
- Fixed-code sub-GHz RF devices (garage doors, RF outlets, ceiling fans)
- A small number of modern radios over time (BLE, Zigbee)

That wedge is enough to prove the thesis.

---

## 3. Product Definition

### What OmniNode Is

- A cheap, always-on networked puck
- A self-describing physical peripheral
- A stable local API for radio operations
- A hardware endpoint that an agent can discover and use

### What OmniNode Is Not

- An LLM runtime
- A home automation brain
- A device knowledge base
- A mobile app or dashboard
- A middleware bridge between an agent and the puck

The agent owns the intelligence. OmniNode owns execution.

---

## 4. Architecture: Dumb Box, Autonomous Brain

The system is intentionally split into two layers. There is no middleware, no bridge service, and no MCP server. The agent communicates directly with the puck over HTTP on the local network.

### Layer 1: The Brain

The reference integration is OpenClaw (powered by Claude Sonnet 4.6), running locally on user-owned hardware (e.g., a Mac Mini). The user communicates with OpenClaw via natural language through existing chat channels (Telegram, iMessage, etc.).

OpenClaw is responsible for:

- Natural language interaction with the user
- Device identification and room/device naming
- IR and RF code libraries (obtained via web search or learned via the puck)
- Semantic mapping (e.g., "TV power" → raw hex code)
- Automation logic and scheduling
- Dynamic script generation
- Command routing to the correct puck
- Long-term state and user preferences
- All memory, history, and context

**OpenClaw is not part of the OmniNode firmware scope. It already exists and is operational.**

### Layer 2: The Hands

OmniNode is responsible for:

- Joining the local WiFi network
- Advertising itself over mDNS
- Serving a capability manifest and full OpenAPI spec
- Receiving authenticated API requests
- Translating those requests into physical radio operations
- Returning results to the caller
- Persisting only the minimum local state required for device operation

**OmniNode does no reasoning and stores no user-level meaning.**

That separation is the core design rule.

### Why No Middleware

OpenClaw is a full autonomous agent runtime with built-in capabilities for network discovery, web searching, code generation, cron jobs, and tool orchestration. It does not need a bridge service to interact with an HTTP API. The puck broadcasts its presence via mDNS. OpenClaw finds it, reads the API spec, and builds its own integrations dynamically.

If a future use case proves that formal tool registration (e.g., MCP) is necessary, it can be added then. We do not pre-build infrastructure the AI may not need.

---

## 5. Locked Decisions for V1

The following decisions are fixed for the V1 MVP:

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Firmware stack | Arduino / PlatformIO (C++) | Fastest path to working MVP; mature library support for all components |
| Power model | USB-C, always-on | Sub-500ms latency requires active WiFi; battery deferred to V2 |
| Discovery | mDNS with `_omninode._tcp.local` | Zero-config network discovery; no manual IP entry |
| API shape | `GET /` for manifest, `GET /openapi.json` for full spec | Lightweight root; complete spec available on demand |
| Spec delivery | Embedded in firmware, served from the puck | The puck describes itself; no external dependency |
| State ownership | The agent is the system of record | Puck stores only what it needs to survive a reboot |
| Auth model | Bearer token generated on first boot | Displayed once during provisioning; required for all API calls |
| Middleware | None | Agent calls the puck directly over HTTP |
| Primary protocol | IR | Most differentiated demo; controls legacy devices no other platform can touch |
| Reference agent | OpenClaw | But hardware is model-agnostic; any HTTP caller works |

These cuts are intentional. The goal is a buildable product thesis, not architectural completeness.

---

## 6. Hardware Specifications

### 6.1 Core Microcontroller

**Seeed Studio XIAO ESP32C6**

| Attribute | Detail |
|-----------|--------|
| Native radios | WiFi 6 (802.11ax), Bluetooth 5.3 (BLE), 802.15.4 (Zigbee/Thread) |
| Form factor | 21 × 17.5mm |
| Usable GPIO | 11 pins |
| Persistent storage | NVS (Non-Volatile Storage) on flash |
| Power | USB-C |

### 6.2 External Components

| Component | Protocol | Interface | Pins |
|-----------|----------|-----------|------|
| CHANZON 940nm IR LED | IR transmit | GPIO (PWM) | 1 |
| VS1838B IR Receiver | IR receive | GPIO (input) | 1 |
| PN532 NFC Module | NFC read/write | I2C (SDA, SCL) | 2 |
| CC1101 RF Module | 433MHz RF TX/RX | SPI (MOSI, MISO, SCK, CS) | 4 |

### 6.3 GPIO Budget

| Allocation | Pins |
|------------|------|
| IR (LED + receiver) | 2 |
| NFC / PN532 (I2C) | 2 |
| 433MHz RF / CC1101 (SPI) | 4 |
| **Total used** | **8** |
| **Remaining** (debug UART, future expansion) | **3** |

### 6.4 Additional MVP Components

- ELEGOO breadboard + jumper wire kit
- 100Ω resistor for IR LED current limiting
- USB-C cable for power

No physical buttons on the OmniNode. All interaction is through the API. Recovery is handled via a power-cycle reset sequence (see Section 9).

---

## 7. Protocol Scope: Hardware Availability vs. Shipped Capability

This distinction matters. "Radio present" is not the same as "product capability shipped."

| Capability | Hardware Source | V1 Firmware | Notes |
|------------|---------------|-------------|-------|
| WiFi 6 | On-chip | **Yes** | Required for discovery and API |
| IR (940nm) | External LED + receiver | **Yes** | Primary MVP protocol |
| Fixed-code 433MHz RF | External CC1101 | Phase 3 | Start with fixed-code devices; rolling-code systems are explicitly out of scope |
| NFC | External PN532 | Phase 4 | Tag read/write for tap-to-trigger interactions |
| BLE 5.3 | On-chip | Phase 5 | Useful but not required for first proof |
| Zigbee 3.0 / Thread | On-chip 802.15.4 radio | Experimental (Phase 5) | Coexistence with WiFi must be proven before claiming support |
| Matter | Not a V1 claim | **No** | Matter requires significant firmware and ecosystem work beyond radio support |

---

## 8. API Contract

The puck must be agent-readable from the moment it is discovered.

### 8.1 Root Manifest

`GET /` returns a compact JSON manifest that answers:

- Who am I (device ID, MAC address)
- What firmware am I running (version)
- What auth scheme do I require
- What capabilities are currently enabled
- Where is the full API spec

This endpoint is **unauthenticated** so that any agent on the local network can discover the puck's capabilities.

### 8.2 Full Specification

`GET /openapi.json` returns the complete OpenAPI definition with all available endpoints, request/response schemas, and error codes.

This keeps the root endpoint lightweight and stable while giving an agent everything it needs to operate the puck programmatically.

### 8.3 API Design Principles

- Capabilities are explicit and versioned
- Long-running or stateful operations use action-style endpoints, not ambiguous reads
- The puck exposes low-level radio operations, not semantic device abstractions
- The agent owns meaning; the puck owns execution
- All endpoints (except `GET /` and `GET /openapi.json`) require authentication

### 8.4 Representative V1 Endpoints

| Method | Endpoint | Description |
|--------|----------|-------------|
| `GET` | `/` | Capability manifest (unauthenticated) |
| `GET` | `/openapi.json` | Full OpenAPI spec (unauthenticated) |
| `GET` | `/status` | Device info: uptime, MAC, firmware version, enabled protocols |
| `POST` | `/ir/capture` | Activate IR receiver, return captured raw signal |
| `POST` | `/ir/blast` | Transmit a raw IR signal from the provided payload |

### 8.5 Representative Later Endpoints

| Method | Endpoint | Phase |
|--------|----------|-------|
| `POST` | `/rf/capture` | Phase 3 |
| `POST` | `/rf/blast` | Phase 3 |
| `POST` | `/nfc/read` | Phase 4 |
| `POST` | `/nfc/write` | Phase 4 |
| `GET` | `/ble/scan` | Phase 5 |
| `POST` | `/ble/connect` | Phase 5 |

### 8.6 What the Puck Does NOT Store

The puck does not provide `/ir/codes` or `/rf/codes` CRUD endpoints as a system of record. Learned code libraries, device labels, room assignments, and automation logic live entirely on the agent's host machine. When the agent wants to blast an IR signal, it sends the full raw payload every time.

**This is an intentional design decision.** The puck is a stateless radio executor. The agent is the memory. This means if the agent goes down, the puck is inert — it has no local command cache to fall back on. This tradeoff is accepted for V1 in exchange for architectural simplicity and a clean separation of concerns. A future firmware version may add a small local command cache for resilience, but it is not a V1 requirement.

---

## 9. Data Ownership & Persistence

### Stored on the Puck (NVS)

Only what the puck needs to survive a power cycle and resume operation:

- WiFi SSID and password
- API bearer token
- Device identity (MAC-derived node ID)
- Firmware version and configuration flags

### Stored on the Agent Host (Not Our Concern)

Everything else:

- Room assignments and device names
- Semantic labels (e.g., "living room TV power")
- Learned IR/RF code libraries
- Automation scripts and schedules
- Command history
- User preferences

The agent is the system of record. The puck is a peripheral.

---

## 10. Provisioning, Security & Recovery

### 10.1 Provisioning Flow

1. First boot with no saved credentials → puck starts a WiFi hotspot (`OmniNode-XXXX`)
2. User connects to the hotspot from any device (phone, laptop)
3. Captive portal page displays: WiFi network selector, password field
4. On submission, the puck generates a cryptographically secure bearer token and displays it once
5. User provides the token to their agent (e.g., pastes it into a Telegram chat with OpenClaw)
6. Puck saves WiFi credentials and token to NVS, reboots onto the local network
7. Puck begins mDNS broadcast; agent discovers it and begins authenticated communication
8. The token is never displayed again

### 10.2 Security Model

| Protection | Implementation |
|------------|----------------|
| Local network binding | Puck rejects connections from outside the local subnet |
| Bearer token authentication | Required for all API calls except `GET /` and `GET /openapi.json` |
| Token generation | Cryptographically secure random string, generated once at first boot |
| Unauthorized requests | Return `401 Unauthorized` |

**Transport security note:** For bench development on a trusted LAN, HTTP with bearer token is acceptable. For any consumer-facing release, the minimum bar is transport encryption (HTTPS with self-signed certificate or equivalent). The open-source community is expected to contribute additional security hardening: certificate-based mutual authentication, audit logging, anomaly detection, token rotation, and per-device permission scoping.

### 10.3 Recovery

A provisioned puck that loses its network or whose credentials become invalid needs a reprovisioning path. "No user-facing buttons" does not mean "no recovery."

**V1 recovery mechanism: Power-cycle reset sequence.** Powering the puck off and on a defined number of times within a short window (e.g., 5 cycles within 15 seconds) triggers a factory reset, clearing WiFi credentials and the bearer token. The puck reboots into captive portal mode and can be re-provisioned from scratch.

This requires zero additional hardware, zero GPIO, and works on both a breadboard prototype and a final enclosed product.

---

## 11. Phased Implementation Roadmap

Each phase produces a working, testable increment. The OpenAPI spec served at `GET /openapi.json` grows with each phase, allowing the agent to dynamically discover new capabilities as they come online.

**Phase 1 uses hardcoded WiFi credentials for development speed. Phase 2 replaces this with the consumer-ready captive portal.**

---

### Phase 1: IR Proof of Concept (MVP)

**Objective:** Prove that an agent can discover OmniNode, read its API, and use it to control a real-world legacy device through IR.

**Hardware:** ESP32C6 + IR LED + VS1838B on breadboard. USB-C power. Hardcoded WiFi credentials.

**Deliver:**
- ESP32C6 flashed and connected to WiFi
- mDNS broadcast: `omninode-{mac}.local`, service type `_omninode._tcp`
- `GET /` returns capability manifest
- `GET /openapi.json` returns full OpenAPI spec for IR endpoints
- `GET /status` returns device info (uptime, MAC, firmware version, enabled protocols)
- `POST /ir/capture` activates the IR receiver and returns the captured raw signal
- `POST /ir/blast` accepts a raw IR payload and fires the IR LED

**Success Criteria:**
- Puck is discoverable on the LAN via mDNS
- Manifest and spec are readable by the agent
- `POST /ir/blast` fires the IR LED (verifiable with a phone camera)
- A real target device (TV) physically responds to the transmitted IR signal
- `POST /ir/capture` reliably captures a signal from a handheld remote
- Network identity and WiFi connection survive a power cycle
- Command round-trip latency is under 500ms on the local network

**If this works, the product thesis is proven.**

---

### Phase 2: Provisioning, Recovery & OTA

**Objective:** Replace hardcoded WiFi credentials with a consumer-ready provisioning flow. Add recovery and over-the-air firmware updates.

**Hardware:** Same as Phase 1.

**Deliver:**
- Captive portal on first boot (WiFi hotspot → network selection → token display)
- Bearer token authentication on all endpoints (except `GET /` and `GET /openapi.json`)
- Unauthorized requests return `401`
- Power-cycle reset sequence for factory reset / reprovisioning
- OTA firmware update endpoint (`POST /system/ota`) with integrity verification
- `GET /` and `GET /openapi.json` remain unauthenticated for discoverability

**Success Criteria:**
- A fresh, unflashed puck boots into captive portal mode
- User completes provisioning without reflashing or serial access
- API calls without a valid token return `401`
- API calls with the token succeed
- Power-cycle reset clears credentials and returns to captive portal
- OTA update successfully flashes new firmware over WiFi
- All Phase 1 endpoints remain fully functional after OTA

---

### Phase 3: Fixed-Code Sub-GHz RF

**Objective:** Extend the puck to control fixed-code 433MHz RF devices.

**Hardware:** Wire CC1101 module to ESP32C6 via SPI (4 pins).

**Deliver:**
- CC1101 initialization on SPI bus at boot
- `POST /rf/capture` listens and returns captured raw RF signal
- `POST /rf/blast` transmits a raw 433MHz RF signal
- `GET /openapi.json` updated to include RF endpoints

**Success Criteria:**
- `POST /rf/capture` captures a signal from a 433MHz remote
- `POST /rf/blast` replays the signal and the target device responds
- All Phase 1 and Phase 2 endpoints remain fully functional (no regressions)

**Scope boundary:** Rolling-code systems (most modern garage door openers) are explicitly out of scope. Only fixed-code sub-GHz devices are supported.

---

### Phase 4: NFC

**Objective:** Enable tap-to-trigger interactions via NFC tag reading and writing.

**Hardware:** Wire PN532 module to ESP32C6 via I2C (2 pins).

**Deliver:**
- PN532 initialization on I2C bus at boot
- `POST /nfc/read` polls for and returns NFC tag UID and payload
- `POST /nfc/write` writes data to an NFC tag
- `GET /openapi.json` updated to include NFC endpoints

**Success Criteria:**
- `POST /nfc/read` returns UID and payload from a tapped NFC tag
- `POST /nfc/write` successfully writes data to a writable tag
- All previous phase endpoints remain fully functional (no regressions)

---

### Phase 5: On-Chip Radios (BLE, Then 802.15.4 Research)

**Objective:** Activate the ESP32C6's native BLE and 802.15.4 radios. Validate that they operate without degrading the core WiFi control path.

**Hardware:** No additional wiring. On-chip radios only.

**Deliver:**
- `GET /ble/scan` returns discovered BLE devices (name, MAC, RSSI, service UUIDs)
- `POST /ble/connect` establishes a connection to a BLE device by MAC
- `POST /ble/write` writes a value to a BLE GATT characteristic
- `GET /ble/read` reads a value from a BLE GATT characteristic
- Zigbee/Thread investigation only if BLE + WiFi coexistence is stable
- `GET /openapi.json` updated to include BLE (and optionally Zigbee) endpoints

**Success Criteria:**
- `GET /ble/scan` returns nearby BLE devices
- A BLE device responds to a command sent via the API
- WiFi API latency and stability are not degraded during BLE/Zigbee operation
- All previous phase endpoints remain fully functional (no regressions)

**Note:** WiFi and 802.15.4 (Zigbee/Thread) share the ESP32C6's physical radio and require time-slicing. If coexistence proves unstable, Zigbee/Thread support is deferred to a future hardware revision or a dedicated coordinator design. This is an engineering constraint, not a failure.

---

This phase order is intentional. **Product shape and reliability come before protocol breadth.**

---

## 12. Non-Goals for V1

The following are explicitly out of scope for the first product proof:

- Battery power
- Retail enclosure
- Mobile app or dashboard
- Cloud dependency
- Home Assistant integration
- BLE productization (Phase 5 is experimental)
- Zigbee/Thread productization
- Matter support
- Rolling-code RF systems
- Enterprise features
- Multi-user permissions
- Per-device permission scoping
- WiFi CSI / spatial awareness

These are expansion paths, not MVP requirements.

---

## 13. Future Horizons (V2 & Beyond)

### Battery Power
Deep sleep cycles with wake-on-command architecture. Requires significant firmware optimization. Deferred from V1 because always-on WiFi and continuous radio listening make meaningful battery life physically impossible in a small form factor at acceptable latency.

### WiFi CSI Spatial Awareness
The ESP32C6 is capable of capturing Channel State Information (CSI) from WiFi radio waves. In V2, the OmniNode could stream raw CSI data to the agent host, enabling neural network processing for human presence detection, movement tracking, and gesture recognition — without any camera.

### Custom PCB & Enclosure
Replace the breadboard prototype with a purpose-designed PCB. 3D-printed and eventually injection-molded enclosure for a retail-ready form factor.

### Home Assistant Integration
Official integration for the Home Assistant platform (2M+ active installations). HA users are the ideal early-adopter demographic: technically sophisticated, hardware-hungry, and already committed to local-first, open-source automation. This is the highest-leverage distribution channel available, but it is a post-MVP deliverable.

### FCC Certification
Required for commercial sale of any radio-emitting device in the United States. Gated on finalized PCB design.

---

## 14. Business Model

### The Money Is in the Box

Pre-built, polished OmniNode pucks sold direct-to-consumer at **$49–79**. BOM cost at scale: **$8–12**. Healthy margins even at aggressive pricing. Volume is the priority in early stages to grow the installed base and open-source community simultaneously.

### Open Source Flywheel

Hardware schematics and firmware are fully open-source from day one under a permissive license. We do not pay engineers to build software integrations. The open-source community and autonomous AI agents build the software layer. More contributors → more device support → more users → more contributors.

### Model Agnostic

While the reference integration is OpenClaw, the raw REST API approach means the hardware works with any capable LLM, agentic framework, or local automation system (Home Assistant, n8n, custom scripts) that a user chooses to run. The puck does not care who is calling the API.

### Future Revenue Streams

- OpenClaw Premium subscription (cloud backup, advanced reasoning, multi-environment support)
- Enterprise licensing (hospitality, healthcare, commercial real estate)
- Device profile marketplace (community-curated IR/RF code libraries)

---

## 15. Repository Structure

```
omninode/
├── firmware/       → Puck firmware (Arduino/PlatformIO C++)
├── api-spec/       → OpenAPI YAML specification (source of truth)
├── hardware/       → Schematics, wiring diagrams, BOM, pinout reference
└── docs/           → Project summary, PRD, contributing guide
```

No middleware. No CLI. No companion app. The open-source community extends the repo as they see fit.

---

## 16. Document Hierarchy

This Project Summary is the strategic foundation. The following documents build on it:

| Document | Purpose | Status |
|----------|---------|--------|
| `docs/ProjectSummary.md` | Vision, architecture, decisions, roadmap (this document) | **Complete** |
| `docs/PRD.md` | Exact firmware behavior, payload formats, error codes, edge cases | Next |
| `api-spec/openapi.yaml` | Machine-readable API contract; source of truth for firmware and agents | Follows PRD |

The Project Summary defines what we build and why. The PRD defines exactly how it behaves. The OpenAPI spec makes it machine-readable.

---

*OmniNode — Open Source · Local First · Model Agnostic*
*Powered by OpenClaw*
