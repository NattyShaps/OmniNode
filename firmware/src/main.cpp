#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include "secrets.h"

#define LED_PIN 15
#define WIFI_MAX_RETRIES 3
#define WIFI_RETRY_TIMEOUT_MS 10000
#define WIFI_RECONNECT_REBOOT_MS 60000

WebServer server(80);

String deviceId;
unsigned long wifiLostTime = 0;

// Get the last 6 hex chars of the MAC address, lowercase, no separators
String getMac6() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char mac6[7];
  snprintf(mac6, sizeof(mac6), "%02x%02x%02x", mac[3], mac[4], mac[5]);
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
    MDNS.addServiceTxt("_omninode", "_tcp", "fw", "0.1.0");
    MDNS.addServiceTxt("_omninode", "_tcp", "protocols", "ir");
    Serial.printf("mDNS started: %s.local\n", deviceId.c_str());
  } else {
    Serial.println("mDNS failed to start.");
  }
}

// Temporary GET / handler — will be replaced with full manifest in Block 6
void handleRoot() {
  server.send(200, "application/json", "{\"status\": \"ok\"}");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== OmniNode Phase 1 ===");

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Connect to WiFi
  if (!connectWiFi()) {
    Serial.println("All WiFi attempts failed. Halting.");
    errorHalt();
  }

  // Build device ID from MAC
  deviceId = "omninode-" + getMac6();
  Serial.printf("Device ID: %s\n", deviceId.c_str());

  // Start mDNS
  startMDNS();

  // Start HTTP server
  server.on("/", HTTP_GET, handleRoot);
  server.begin();
  Serial.println("HTTP server started on port 80.");

  // LED solid on = connected and operational
  digitalWrite(LED_PIN, HIGH);

  Serial.println("=== OmniNode ready ===");
}

void loop() {
  server.handleClient();

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
