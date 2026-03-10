#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <Preferences.h>
#include "secrets.h"

#define LED_PIN 15
#define WIFI_MAX_RETRIES 3
#define WIFI_RETRY_TIMEOUT_MS 10000
#define WIFI_RECONNECT_REBOOT_MS 60000
#define BOOT_STABLE_MS 3000
#define FW_VERSION "0.1.0"
#define API_VERSION "1.0"
#define MANIFEST_VERSION 1

WebServer server(80);
Preferences prefs;

String deviceId;
String macAddress;
unsigned long wifiLostTime = 0;
unsigned long bootTime = 0;
bool bootCountCleared = false;
volatile bool radioBusy = false;

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

// Load or generate device ID from NVS
void initDeviceId() {
  // Always populate macAddress via getMac6()
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

// Blink LED rapidly to indicate error, then halt
void errorHalt() {
  while (true) {
    digitalWrite(LED_PIN, HIGH);
    delay(100);
    digitalWrite(LED_PIN, LOW);
    delay(100);
  }
}

// Connect to WiFi with retries per PRD Section 3.1
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

// Start mDNS per PRD Section 4.2
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

  // Start HTTP server
  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, handleStatus);
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
    // Reboot if disconnected for 60+ seconds
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
