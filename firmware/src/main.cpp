#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <Preferences.h>
#include <IRrecv.h>
#include <IRsend.h>
#include <IRutils.h>
#include "secrets.h"

#define LED_PIN 15
#define IR_LED_PIN 0
#define IR_RECV_PIN 1
#define WIFI_MAX_RETRIES 3
#define WIFI_RETRY_TIMEOUT_MS 10000
#define WIFI_RECONNECT_REBOOT_MS 60000
#define BOOT_STABLE_MS 3000
#define IR_CAPTURE_TIMEOUT_DEFAULT 10000
#define IR_CAPTURE_TIMEOUT_MIN 1000
#define IR_CAPTURE_TIMEOUT_MAX 30000
#define IR_RAW_BUF_SIZE 1024
#define IR_RAW_MIN_LEN 2
#define IR_RAW_MAX_LEN 1024
#define IR_FREQ_MIN 30
#define IR_FREQ_MAX 60
#define IR_FREQ_DEFAULT 38
#define IR_REPEAT_MIN 1
#define IR_REPEAT_MAX 10
#define IR_REPEAT_DEFAULT 1
#define FW_VERSION "0.1.0"
#define API_VERSION "1.0"
#define MANIFEST_VERSION 1

WebServer server(80);
Preferences prefs;
IRrecv irrecv(IR_RECV_PIN, IR_RAW_BUF_SIZE, 15, true);
IRsend irsend(IR_LED_PIN);
decode_results irResults;

String deviceId;
String macAddress;
unsigned long wifiLostTime = 0;
unsigned long bootTime = 0;
bool bootCountCleared = false;
volatile bool radioBusy = false;

// ─── Utility functions ──────────────────────────────────────────

// Get the last 6 hex chars of the MAC address, lowercase, no separators
String getMac6() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char mac6[7];
  snprintf(mac6, sizeof(mac6), "%02x%02x%02x", mac[3], mac[4], mac[5]);
  // Store full MAC address in AA:BB:CC:DD:EE:FF format
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  macAddress = String(macStr);
  return String(mac6);
}

// Blink LED rapidly to indicate error, then halt
void errorHalt() {
  while (true) {
    digitalWrite(LED_PIN, HIGH);
    delay(100);
    digitalWrite(LED_PIN, LOW);
    delay(100);
  }
}

// ─── NVS functions (Block 7) ────────────────────────────────────

// Load or generate device ID from NVS
void initDeviceId() {
  String mac6 = getMac6();
  prefs.begin("device", false);
  String storedId = prefs.getString("id", "");
  if (storedId.length() > 0) {
    deviceId = storedId;
    Serial.printf("Device ID loaded from NVS: %s\n", deviceId.c_str());
  } else {
    deviceId = "omninode-" + mac6;
    prefs.putString("id", deviceId);
    Serial.printf("Device ID generated and saved: %s\n", deviceId.c_str());
  }
  prefs.end();
}

// Store WiFi credentials in NVS (infrastructure for Phase 2 captive portal)
void storeWifiCreds() {
  prefs.begin("wifi", false);
  prefs.putString("ssid", WIFI_SSID);
  prefs.putString("pass", WIFI_PASS);
  prefs.end();
  Serial.println("WiFi credentials stored in NVS.");
}

// Store firmware version in NVS
void storeFwVersion() {
  prefs.begin("system", false);
  prefs.putString("fw_ver", FW_VERSION);
  prefs.end();
}

// Increment boot counter (for future power-cycle factory reset)
void incrementBootCount() {
  prefs.begin("system", false);
  uint8_t count = prefs.getUChar("boot_cnt", 0);
  count++;
  prefs.putUChar("boot_cnt", count);
  Serial.printf("Boot count: %d\n", count);
  prefs.end();
}

// Reset boot counter to 0 after stable uptime
void clearBootCount() {
  prefs.begin("system", false);
  prefs.putUChar("boot_cnt", 0);
  prefs.end();
  Serial.println("Boot count reset (stable uptime reached).");
}

// ─── WiFi functions (Block 3) ───────────────────────────────────

bool connectWiFi() {
  WiFi.mode(WIFI_STA);
  for (int attempt = 1; attempt <= WIFI_MAX_RETRIES; attempt++) {
    Serial.printf("WiFi attempt %d/%d: connecting to %s\n", attempt, WIFI_MAX_RETRIES, WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_RETRY_TIMEOUT_MS) {
      delay(250);
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("WiFi connected! IP: %s\n", WiFi.localIP().toString().c_str());
      return true;
    }
    Serial.println("WiFi attempt failed.");
    WiFi.disconnect();
  }
  return false;
}

void startMDNS() {
  if (MDNS.begin(deviceId.c_str())) {
    MDNS.addService("_omninode", "_tcp", 80);
    MDNS.addServiceTxt("_omninode", "_tcp", "fw", FW_VERSION);
    MDNS.addServiceTxt("_omninode", "_tcp", "protocols", "ir");
    Serial.printf("mDNS started: %s.local\n", deviceId.c_str());
  } else {
    Serial.println("mDNS failed to start.");
  }
}

// ─── HTTP handlers (Blocks 6 + 4) ──────────────────────────────

// GET / — Root manifest per PRD Section 7
void handleRoot() {
  unsigned long uptime = millis() / 1000;
  String json = "{";
  json += "\"device\":\"omninode\",";
  json += "\"id\":\"" + deviceId + "\",";
  json += "\"mac\":\"" + macAddress + "\",";
  json += "\"firmware_version\":\"" FW_VERSION "\",";
  json += "\"manifest_version\":" + String(MANIFEST_VERSION) + ",";
  json += "\"api_version\":\"" API_VERSION "\",";
  json += "\"uptime_seconds\":" + String(uptime) + ",";
  json += "\"protocols\":[\"ir\"],";
  json += "\"endpoints\":{";
  json += "\"spec\":\"/openapi.json\",";
  json += "\"status\":\"/status\"";
  json += "}";
  json += "}";
  server.send(200, "application/json", json);
}

// GET /status — Operational status per PRD Section 9
void handleStatus() {
  unsigned long uptime = millis() / 1000;
  String irStatus = radioBusy ? "busy" : "ready";
  String json = "{";
  json += "\"success\":true,";
  json += "\"data\":{";
  json += "\"device\":\"omninode\",";
  json += "\"id\":\"" + deviceId + "\",";
  json += "\"mac\":\"" + macAddress + "\",";
  json += "\"firmware_version\":\"" FW_VERSION "\",";
  json += "\"api_version\":\"" API_VERSION "\",";
  json += "\"uptime_seconds\":" + String(uptime) + ",";
  json += "\"free_heap_bytes\":" + String(ESP.getFreeHeap()) + ",";
  json += "\"wifi\":{";
  json += "\"ssid\":\"" + WiFi.SSID() + "\",";
  json += "\"rssi_dbm\":" + String(WiFi.RSSI()) + ",";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\"";
  json += "},";
  json += "\"protocols\":{";
  json += "\"ir\":\"" + irStatus + "\",";
  json += "\"rf\":\"not_available\",";
  json += "\"nfc\":\"not_available\",";
  json += "\"ble\":\"not_available\"";
  json += "},";
  json += "\"radio_busy\":" + String(radioBusy ? "true" : "false");
  json += "}";
  json += "}";
  server.send(200, "application/json", json);
}

// POST /ir/capture — IR signal capture per PRD Section 11.2
void handleIrCapture() {
  // Check radio concurrency lock
  if (radioBusy) {
    server.send(503, "application/json",
      "{\"success\":false,\"error\":\"busy\",\"message\":\"A radio operation is already in progress\"}");
    return;
  }

  // Parse timeout_ms from request body
  int timeoutMs = IR_CAPTURE_TIMEOUT_DEFAULT;
  if (server.hasArg("plain")) {
    String body = server.arg("plain");
    // Simple JSON parsing for timeout_ms
    int idx = body.indexOf("\"timeout_ms\"");
    if (idx >= 0) {
      int colonIdx = body.indexOf(':', idx);
      if (colonIdx >= 0) {
        String valStr = "";
        for (int i = colonIdx + 1; i < body.length(); i++) {
          char c = body.charAt(i);
          if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
          if (c >= '0' && c <= '9') {
            valStr += c;
          } else {
            break;
          }
        }
        if (valStr.length() > 0) {
          timeoutMs = valStr.toInt();
        }
      }
    }
  }

  // Validate timeout_ms
  if (timeoutMs < IR_CAPTURE_TIMEOUT_MIN || timeoutMs > IR_CAPTURE_TIMEOUT_MAX) {
    server.send(400, "application/json",
      "{\"success\":false,\"error\":\"bad_request\",\"message\":\"timeout_ms must be between 1000 and 30000\"}");
    return;
  }

  // Lock the radio
  radioBusy = true;
  Serial.printf("IR capture started (timeout: %d ms)\n", timeoutMs);

  // Flush any old data from the receiver buffer
  irrecv.resume();

  // Wait for IR signal or timeout
  unsigned long startTime = millis();
  bool signalReceived = false;

  while (millis() - startTime < (unsigned long)timeoutMs) {
    if (irrecv.decode(&irResults)) {
      signalReceived = true;
      break;
    }
    delay(50);
    yield();
  }

  if (signalReceived) {
    // Build JSON response with raw timing array
    // rawbuf[0] is an overflow/gap marker — skip it, start from index 1
    uint16_t rawLen = irResults.rawlen - 1;
    Serial.printf("IR signal captured: %d timing values\n", rawLen);

    String json = "{\"success\":true,\"data\":{\"raw\":[";
    for (uint16_t i = 1; i < irResults.rawlen; i++) {
      if (i > 1) json += ",";
      // rawbuf stores values in ticks — multiply by RAWTICK (2) to get microseconds
      json += String(irResults.rawbuf[i] * RAWTICK);
    }
    json += "],\"length\":" + String(rawLen) + "}}";

    radioBusy = false;
    irrecv.resume();
    server.send(200, "application/json", json);
  } else {
    // Timeout — no signal received
    Serial.println("IR capture timeout — no signal received.");
    radioBusy = false;
    irrecv.resume();
    server.send(408, "application/json",
      "{\"success\":false,\"error\":\"timeout\",\"message\":\"No IR signal received\"}");
  }
}

// Manual IR send — bit-bang the 38kHz carrier since IRsend LEDC doesn't work on ESP32C6
void manualIrSend(uint16_t *buf, uint16_t len, uint16_t freqKhz) {
  uint16_t halfPeriodUs = 500 / freqKhz;  // half of one carrier cycle (e.g. 500/38 = 13μs)
  pinMode(IR_LED_PIN, OUTPUT);
  digitalWrite(IR_LED_PIN, LOW);

  for (uint16_t i = 0; i < len; i++) {
    if (i % 2 == 0) {
      // MARK: pulse the carrier at freqKhz
      unsigned long start = micros();
      while (micros() - start < buf[i]) {
        digitalWrite(IR_LED_PIN, HIGH);
        delayMicroseconds(halfPeriodUs);
        digitalWrite(IR_LED_PIN, LOW);
        delayMicroseconds(halfPeriodUs);
      }
    } else {
      // SPACE: keep pin LOW
      digitalWrite(IR_LED_PIN, LOW);
      delayMicroseconds(buf[i]);
    }
  }
  digitalWrite(IR_LED_PIN, LOW);
}

// Helper: parse an integer field from JSON body (reused by capture and blast)
int parseJsonInt(const String &body, const char *key, int defaultVal) {
  int idx = body.indexOf(key);
  if (idx < 0) return defaultVal;
  int colonIdx = body.indexOf(':', idx);
  if (colonIdx < 0) return defaultVal;
  String valStr = "";
  for (int i = colonIdx + 1; i < (int)body.length(); i++) {
    char c = body.charAt(i);
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
    if (c >= '0' && c <= '9') {
      valStr += c;
    } else {
      break;
    }
  }
  return valStr.length() > 0 ? valStr.toInt() : defaultVal;
}

// POST /ir/blast — IR signal transmission per PRD Section 11.3
void handleIrBlast() {
  // Check radio concurrency lock
  if (radioBusy) {
    server.send(503, "application/json",
      "{\"success\":false,\"error\":\"busy\",\"message\":\"A radio operation is already in progress\"}");
    return;
  }

  // Must have a request body
  if (!server.hasArg("plain")) {
    server.send(400, "application/json",
      "{\"success\":false,\"error\":\"bad_request\",\"message\":\"Request body is required\"}");
    return;
  }

  String body = server.arg("plain");

  // Parse the raw array from JSON
  int rawStart = body.indexOf("\"raw\"");
  if (rawStart < 0) {
    server.send(400, "application/json",
      "{\"success\":false,\"error\":\"bad_request\",\"message\":\"Missing required field: raw\"}");
    return;
  }

  int bracketStart = body.indexOf('[', rawStart);
  int bracketEnd = body.indexOf(']', bracketStart);
  if (bracketStart < 0 || bracketEnd < 0 || bracketEnd <= bracketStart + 1) {
    server.send(400, "application/json",
      "{\"success\":false,\"error\":\"bad_request\",\"message\":\"raw must be a non-empty array\"}");
    return;
  }

  // Extract the array contents between [ and ]
  String arrayStr = body.substring(bracketStart + 1, bracketEnd);

  // Parse comma-separated integers into a buffer
  uint16_t rawBuf[IR_RAW_MAX_LEN];
  uint16_t rawLen = 0;
  int pos = 0;

  while (pos < (int)arrayStr.length() && rawLen < IR_RAW_MAX_LEN) {
    // Skip whitespace and commas
    char c = arrayStr.charAt(pos);
    if (c == ' ' || c == ',' || c == '\t' || c == '\n' || c == '\r') {
      pos++;
      continue;
    }
    // Read a number
    String numStr = "";
    while (pos < (int)arrayStr.length()) {
      c = arrayStr.charAt(pos);
      if (c >= '0' && c <= '9') {
        numStr += c;
        pos++;
      } else {
        break;
      }
    }
    if (numStr.length() > 0) {
      rawBuf[rawLen++] = (uint16_t)numStr.toInt();
    }
  }

  // Validate raw array length
  if (rawLen < IR_RAW_MIN_LEN || rawLen > IR_RAW_MAX_LEN) {
    server.send(400, "application/json",
      "{\"success\":false,\"error\":\"bad_request\",\"message\":\"raw array length must be between 2 and 1024\"}");
    return;
  }

  // Parse optional fields
  int freqKhz = parseJsonInt(body, "\"frequency_khz\"", IR_FREQ_DEFAULT);
  int repeat = parseJsonInt(body, "\"repeat\"", IR_REPEAT_DEFAULT);

  // Validate frequency_khz
  if (freqKhz < IR_FREQ_MIN || freqKhz > IR_FREQ_MAX) {
    server.send(400, "application/json",
      "{\"success\":false,\"error\":\"bad_request\",\"message\":\"frequency_khz must be between 30 and 60\"}");
    return;
  }

  // Validate repeat
  if (repeat < IR_REPEAT_MIN || repeat > IR_REPEAT_MAX) {
    server.send(400, "application/json",
      "{\"success\":false,\"error\":\"bad_request\",\"message\":\"repeat must be between 1 and 10\"}");
    return;
  }

  // Lock the radio
  radioBusy = true;
  Serial.printf("IR blast: %d values, %d kHz, repeat %d\n", rawLen, freqKhz, repeat);

  // Disable IR receiver to avoid timer conflicts during transmission
  irrecv.disableIRIn();
  delay(10);

  // Fire the IR signal using manual bit-bang (IRsend LEDC broken on ESP32C6)
  for (int r = 0; r < repeat; r++) {
    manualIrSend(rawBuf, rawLen, (uint16_t)freqKhz);
    if (r < repeat - 1) {
      delay(40);  // Inter-signal gap between repeats
    }
  }

  // Re-enable IR receiver
  delay(10);
  irrecv.enableIRIn();

  // Unlock the radio
  radioBusy = false;

  // Build success response
  String json = "{\"success\":true,\"data\":{";
  json += "\"length\":" + String(rawLen) + ",";
  json += "\"frequency_khz\":" + String(freqKhz) + ",";
  json += "\"repeat\":" + String(repeat);
  json += "}}";
  server.send(200, "application/json", json);
}

// ─── Setup & Loop ───────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== OmniNode Phase 1 ===");

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // NVS: increment boot counter (for future power-cycle factory reset)
  incrementBootCount();
  bootTime = millis();

  // NVS: store firmware version
  storeFwVersion();

  // Connect to WiFi (must happen before initDeviceId so MAC address is available)
  if (!connectWiFi()) {
    Serial.println("All WiFi attempts failed. Halting.");
    errorHalt();
  }

  // NVS: load or generate device ID (after WiFi init so MAC is valid)
  initDeviceId();

  // NVS: store WiFi credentials (Phase 2 infrastructure)
  storeWifiCreds();

  // Start mDNS
  startMDNS();

  // Initialize IR receiver and transmitter
  irrecv.enableIRIn();
  irsend.begin();
  Serial.println("IR receiver (GPIO1) and transmitter (GPIO0) initialized.");

  // Start HTTP server
  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/ir/capture", HTTP_POST, handleIrCapture);
  server.on("/ir/blast", HTTP_POST, handleIrBlast);
  server.begin();
  Serial.println("HTTP server started on port 80.");

  // LED solid on = connected and operational
  digitalWrite(LED_PIN, HIGH);

  Serial.println("=== OmniNode ready ===");
}

void loop() {
  server.handleClient();

  // Reset boot counter after 3 seconds of stable uptime
  if (!bootCountCleared && millis() - bootTime >= BOOT_STABLE_MS) {
    clearBootCount();
    bootCountCleared = true;
  }

  // WiFi auto-reconnect per PRD Section 4.1
  if (WiFi.status() != WL_CONNECTED) {
    if (wifiLostTime == 0) {
      wifiLostTime = millis();
      Serial.println("WiFi disconnected. Attempting reconnect...");
    }
    if (millis() - wifiLostTime >= WIFI_RECONNECT_REBOOT_MS) {
      Serial.println("WiFi down for 60s. Rebooting.");
      ESP.restart();
    }
  } else {
    if (wifiLostTime != 0) {
      Serial.println("WiFi reconnected.");
      wifiLostTime = 0;
    }
  }
}
