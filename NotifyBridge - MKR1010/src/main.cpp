#include <Arduino.h>
#include <WiFiNINA.h>
#include <ArduinoHttpClient.h>

// ── Configuration ─────────────────────────────────────────────────────────────
#define WIFI_SSID             "neuralis"
#define WIFI_PASSWORD         "YOUR_WIFI_PASSWORD"

#define NB_API_TOKEN          "YOUR_API_TOKEN"
#define NB_USER_KEY           "YOUR_USER_KEY"
#define NB_DEVICE_CODE        "YOUR_DEVICE_CODE"

#define HEARTBEAT_INTERVAL_MS (1UL * 60 * 60 * 1000)
#define WIFI_RETRY_INTERVAL   30000UL
#define POST_MAX_RETRIES      5
#define BOOT_BUTTON_PIN       7            // external button on D7, wired active-LOW
#define LED_PIN               LED_BUILTIN  // pin 6 on MKR WiFi 1010
// ─────────────────────────────────────────────────────────────────────────────

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
        ledOn = !ledOn;
        digitalWrite(LED_PIN, ledOn);
        lastToggleMs = now;
      }
      break;

    case LED_DISCONNECTED:
      if (now - lastToggleMs >= 500) {
        ledOn = !ledOn;
        digitalWrite(LED_PIN, ledOn);
        lastToggleMs = now;
      }
      break;

    case LED_SENDING:
      // 3 rapid blinks (50 ms on/off), then back to idle
      if (blinkCount < 6) {
        if (now - lastToggleMs >= 50) {
          ledOn = !ledOn;
          digitalWrite(LED_PIN, ledOn);
          lastToggleMs = now;
          blinkCount++;
        }
      } else {
        blinkCount = 0;
        setLedMode(LED_IDLE);
      }
      break;

    case LED_IDLE:
      // Brief 50 ms flash every 3 s
      if (!ledOn && now - lastToggleMs >= 3000) {
        ledOn = true;
        digitalWrite(LED_PIN, HIGH);
        lastToggleMs = now;
      } else if (ledOn && now - lastToggleMs >= 50) {
        ledOn = false;
        digitalWrite(LED_PIN, LOW);
        lastToggleMs = now;
      }
      break;
  }
}

// ── WiFi ──────────────────────────────────────────────────────────────────────
void connectWiFi() {
  Serial.print("Connecting to ");
  Serial.print(WIFI_SSID);
  setLedMode(LED_CONNECTING);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    updateLed();
    delay(100);
    Serial.print(".");
  }
  Serial.print("\nConnected — IP: ");
  Serial.println(WiFi.localIP());
  setLedMode(LED_IDLE);
}

bool ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;

  static unsigned long lastAttemptMs = 0;
  if (millis() - lastAttemptMs < WIFI_RETRY_INTERVAL) return false;
  lastAttemptMs = millis();

  Serial.print("Reconnecting to ");
  Serial.print(WIFI_SSID);
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
    Serial.print("\nConnected — IP: ");
    Serial.println(WiFi.localIP());
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
    WiFiSSLClient sslClient;
    HttpClient http(sslClient, "notifybridge.mindeon.net", 443);

    int err = http.post("/v1/messages/send", "application/json", payload);
    if (err == 0) {
      int    code = http.responseStatusCode();
      String body = http.responseBody();
      Serial.print("NotifyBridge [");
      Serial.print(code);
      Serial.print("]: ");
      Serial.println(body);
      http.stop();
      setLedMode(LED_IDLE);
      return (code >= 200 && code < 300);
    }

    Serial.print("Attempt ");
    Serial.print(attempt);
    Serial.print("/");
    Serial.print(POST_MAX_RETRIES);
    Serial.print(" failed (error ");
    Serial.print(err);
    Serial.print(") — retry in ");
    Serial.print(backoff);
    Serial.println("ms");
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
  char msg[180];
  snprintf(msg, sizeof(msg),
    "Heartbeat | uptime %luh %02lum %02lus | RSSI %d dBm | IP %s",
    sec / 3600, (sec % 3600) / 60, sec % 60,
    WiFi.RSSI(),
    WiFi.localIP().toString().c_str());

  postNotification(msg, -1);
}

// ── Button ISR ────────────────────────────────────────────────────────────────
static volatile bool buttonPressed = false;

void onBootButton() { buttonPressed = true; }

// ─────────────────────────────────────────────────────────────────────────────

static unsigned long lastHeartbeatMs = 0;

void setup() {
  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BOOT_BUTTON_PIN), onBootButton, FALLING);

  connectWiFi();
  postNotification("Device is alive");
  lastHeartbeatMs = millis();
}

void loop() {
  updateLed();

  if (buttonPressed) {
    buttonPressed = false;
    delay(50);
    if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
      postNotification("Button pressed!", 1);
    }
  }

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
