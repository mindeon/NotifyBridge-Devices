#include <Arduino.h>
#include <WiFiNINA.h>
#include <ArduinoHttpClient.h>
#include <Arduino_LSM6DSOX.h>

// ── Configuration ─────────────────────────────────────────────────────────────
#define WIFI_SSID             "neuralis"
#define WIFI_PASSWORD         "YOUR_WIFI_PASSWORD"

#define NB_API_TOKEN          "YOUR_API_TOKEN"
#define NB_USER_KEY           "YOUR_USER_KEY"
#define NB_DEVICE_CODE        "YOUR_DEVICE_CODE"

#define HEARTBEAT_INTERVAL_MS (1UL * 60 * 60 * 10)
#define WIFI_RETRY_INTERVAL   30000UL
#define WIFI_CONNECT_TIMEOUT  30000UL
#define POST_MAX_RETRIES      5

#define NB_HOST "notifybridge.mindeon.net"
#define NB_PORT 443
#define NB_PATH "/v1/messages/send"
// ─────────────────────────────────────────────────────────────────────────────

// ── LSM6DSOX temperature sensor ───────────────────────────────────────────────
float readChipTemp() {
  int temp;
  if (IMU.readTemperature(temp)) return (float)temp;
  return 0.0f;
}

#define STATUS_LED LED_BUILTIN

void writeLed(bool on) { digitalWrite(STATUS_LED, on ? HIGH : LOW); }

// ── LED state machine ─────────────────────────────────────────────────────────
enum LedMode { LED_CONNECTING, LED_IDLE, LED_SENDING, LED_DISCONNECTED };
static LedMode ledMode = LED_CONNECTING;

void setLedMode(LedMode mode) { ledMode = mode; }

void updateLed() {
  static unsigned long lastToggleMs = 0;
  static bool          ledOn        = false;
  static int           blinkCount   = 0;
  unsigned long        now          = millis();

  switch (ledMode) {

    case LED_CONNECTING:
      if (now - lastToggleMs >= 100) {
        ledOn = !ledOn; writeLed(ledOn); lastToggleMs = now;
      }
      break;

    case LED_DISCONNECTED:
      if (now - lastToggleMs >= 500) {
        ledOn = !ledOn; writeLed(ledOn); lastToggleMs = now;
      }
      break;

    case LED_SENDING:
      if (blinkCount < 6) {
        if (now - lastToggleMs >= 50) {
          ledOn = !ledOn; writeLed(ledOn); lastToggleMs = now; blinkCount++;
        }
      } else {
        blinkCount = 0; setLedMode(LED_IDLE);
      }
      break;

    case LED_IDLE:
      if (!ledOn && now - lastToggleMs >= 3000) {
        ledOn = true; writeLed(true); lastToggleMs = now;
      } else if (ledOn && now - lastToggleMs >= 50) {
        ledOn = false; writeLed(false); lastToggleMs = now;
      }
      break;
  }
}

// ── WiFi ──────────────────────────────────────────────────────────────────────
void connectWiFi() {
  Serial.print("Connecting to "); Serial.println(WIFI_SSID);
  setLedMode(LED_CONNECTING);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start >= WIFI_CONNECT_TIMEOUT) {
      Serial.println("\nConnect timed out — will retry in loop");
      setLedMode(LED_DISCONNECTED);
      return;
    }
    updateLed();
    delay(100);
    Serial.print(".");
  }

  char buf[48];
  snprintf(buf, sizeof(buf), "\nConnected  RSSI: %d dBm", WiFi.RSSI());
  Serial.println(buf);
  setLedMode(LED_IDLE);
}

bool ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;

  static unsigned long lastAttemptMs = 0;
  if (millis() - lastAttemptMs < WIFI_RETRY_INTERVAL) return false;
  lastAttemptMs = millis();

  Serial.print("Reconnecting to "); Serial.println(WIFI_SSID);
  setLedMode(LED_CONNECTING);
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    updateLed();
    delay(100);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    char buf[48];
    snprintf(buf, sizeof(buf), "\nReconnected  RSSI: %d dBm", WiFi.RSSI());
    Serial.println(buf);
    setLedMode(LED_IDLE);
    return true;
  }

  Serial.println("\nReconnect failed — will retry later");
  setLedMode(LED_DISCONNECTED);
  return false;
}

// ── HTTP POST with exponential backoff ────────────────────────────────────────
bool postNotification(const String& message, int priority = 0) {
  if (!ensureWiFi()) {
    Serial.println("No WiFi — notification dropped");
    return false;
  }

  String payload =
    String("{") +
      "\"api_token\":\""   + NB_API_TOKEN   + "\","
      "\"user_key\":\""    + NB_USER_KEY    + "\","
      "\"device_code\":\"" + NB_DEVICE_CODE + "\","
      "\"message\":\""     + message        + "\","
      "\"priority\":"      + priority       +
    "}";

  setLedMode(LED_SENDING);
  unsigned long backoff = 2000;

  for (int attempt = 1; attempt <= POST_MAX_RETRIES; attempt++) {
    WiFiSSLClient wifiClient;
    HttpClient http(wifiClient, NB_HOST, NB_PORT);
    http.setTimeout(10000);

    int err = http.post(NB_PATH, "application/json", payload);
    if (err == 0) {
      int    code = http.responseStatusCode();
      String body = http.responseBody();
      http.stop();
      char buf[40];
      snprintf(buf, sizeof(buf), "NotifyBridge [%d]: ", code);
      Serial.print(buf); Serial.println(body);
      setLedMode(LED_IDLE);
      return (code >= 200 && code < 300);
    }

    char buf[64];
    snprintf(buf, sizeof(buf), "Attempt %d/%d failed (err %d) — retry in %lums",
             attempt, POST_MAX_RETRIES, err, backoff);
    Serial.println(buf);
    http.stop();
    delay(backoff);
    backoff = min(backoff * 2, 60000UL);
  }

  Serial.println("All retries exhausted");
  setLedMode(LED_IDLE);
  return false;
}

// ── Heartbeat ─────────────────────────────────────────────────────────────────
void sendHeartbeat() {
  unsigned long sec = millis() / 1000;
  float temp = readChipTemp();
  char msg[220];
  snprintf(msg, sizeof(msg),
    "Heartbeat | uptime %luh %02lum %02lus | RSSI %d dBm | temp %.0fC",
    sec / 3600, (sec % 3600) / 60, sec % 60,
    WiFi.RSSI(), temp);
  postNotification(msg, -1);
}

// ─────────────────────────────────────────────────────────────────────────────

static unsigned long lastHeartbeatMs = 0;

void setup() {
  Serial.begin(115200);
  delay(3000);
  Serial.println("NotifyBridge booting...");

  pinMode(STATUS_LED, OUTPUT);
  writeLed(false);

  if (!IMU.begin()) {
    Serial.println("IMU init failed — temperature will read 0");
  }

  connectWiFi();
  postNotification("Device is alive");
  lastHeartbeatMs = millis();
}

void loop() {
  updateLed();

  if (millis() - lastHeartbeatMs >= HEARTBEAT_INTERVAL_MS) {
    sendHeartbeat();
    lastHeartbeatMs = millis();
  }

  static bool wasConnected = true;
  bool connected = ensureWiFi();
  if (connected && !wasConnected) {
    postNotification("WiFi reconnected", 2);
  }
  wasConnected = connected;

  delay(100);
}
