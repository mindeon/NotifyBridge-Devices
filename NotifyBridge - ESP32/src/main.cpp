#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>

// ── Configuration ─────────────────────────────────────────────────────────────
#define WIFI_SSID     "neuralis"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

#define NB_API_TOKEN  "YOUR_API_TOKEN"   // UUID from the iOS app
#define NB_USER_KEY   "YOUR_USER_KEY"    // Your account identifier
#define NB_DEVICE_CODE "YOUR_DEVICE_CODE"                        // Relay device code
// ─────────────────────────────────────────────────────────────────────────────

void connectWiFi() {
  Serial.printf("Connecting to WiFi: %s", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\nConnected — IP: %s\n", WiFi.localIP().toString().c_str());
}

void sendAliveNotification() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected — skipping notification");
    return;
  }

  HTTPClient http;
  http.begin("https://notifybridge.mindeon.net/v1/messages/send");
  http.addHeader("Content-Type", "application/json");

  String payload =
    "{"
      "\"api_token\":\"" NB_API_TOKEN "\","
      "\"user_key\":\"" NB_USER_KEY "\","
      "\"device_code\":\"" NB_DEVICE_CODE "\","
      "\"message\":\"Device is alive\","
      "\"priority\":0"
    "}";

  int httpCode = http.POST(payload);

  if (httpCode > 0) {
    Serial.printf("NotifyBridge response [%d]: %s\n", httpCode, http.getString().c_str());
  } else {
    Serial.printf("Request failed: %s\n", http.errorToString(httpCode).c_str());
  }

  http.end();
}

void setup() {
  Serial.begin(115200);
  connectWiFi();
  sendAliveNotification();
}

void loop() {
}
