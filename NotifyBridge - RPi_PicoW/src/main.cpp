#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>

// ── Configuration ─────────────────────────────────────────────────────────────
#define WIFI_SSID             "neuralis"
#define WIFI_PASSWORD         "YOUR_WIFI_PASSWORD"

#define NB_API_TOKEN          "YOUR_API_TOKEN"
#define NB_USER_KEY           "YOUR_USER_KEY"
#define NB_DEVICE_CODE        "YOUR_DEVICE_CODE"

#define HEARTBEAT_INTERVAL_MS (1UL * 60 * 60 * 1000)
#define WIFI_RETRY_INTERVAL   30000UL
#define POST_MAX_RETRIES      5
#define BOOT_BUTTON_PIN       15          // GP15 — wire a button between GP15 and GND
#define LED_PIN               LED_BUILTIN // onboard LED via CYW43 chip
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
      // Fast 100 ms toggle
      if (now - lastToggleMs >= 100) {
        ledOn = !ledOn;
        digitalWrite(LED_PIN, ledOn);
        lastToggleMs = now;
      }
      break;

    case LED_DISCONNECTED:
      // Slow 500 ms toggle
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
// Blocking connect used only at boot — no cooldown, runs as fast as the AP allows.
void connectWiFi() {
  Serial.printf("Connecting to %s", WIFI_SSID);
  setLedMode(LED_CONNECTING);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    updateLed();
    delay(100);
    Serial.print(".");
  }
  Serial.printf("\nConnected — IP: %s\n", WiFi.localIP().toString().c_str());
  setLedMode(LED_IDLE);
}

// Non-blocking reconnect used in the loop — respects cooldown to avoid hammering.
// Returns true if connected.
bool ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;

  static unsigned long lastAttemptMs = 0;
  if (millis() - lastAttemptMs < WIFI_RETRY_INTERVAL) return false;
  lastAttemptMs = millis();

  Serial.printf("Reconnecting to %s", WIFI_SSID);
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
    Serial.printf("\nConnected — IP: %s\n", WiFi.localIP().toString().c_str());
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
    HTTPClient http;
    http.begin("https://notifybridge.mindeon.net/v1/messages/send");
    http.addHeader("Content-Type", "application/json");

    int code = http.POST(payload);
    if (code > 0) {
      Serial.printf("NotifyBridge [%d]: %s\n", code, http.getString().c_str());
      http.end();
      setLedMode(LED_IDLE);
      return (code >= 200 && code < 300);
    }

    Serial.printf("Attempt %d/%d failed (error %d) — retry in %lums\n",
                  attempt, POST_MAX_RETRIES, code, backoff);
    http.end();
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

  // Button press — debounce in software, send at priority 1
  if (buttonPressed) {
    buttonPressed = false;
    delay(50);
    if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
      postNotification("Button pressed!", 1);
    }
  }

  // Periodic heartbeat
  if (millis() - lastHeartbeatMs >= HEARTBEAT_INTERVAL_MS) {
    sendHeartbeat();
    lastHeartbeatMs = millis();
  }

  // WiFi watchdog — notify when link is restored
  static bool wasConnected = true;
  bool connected = ensureWiFi();
  if (connected && !wasConnected) {
    postNotification("WiFi reconnected", 2);
  }
  wasConnected = connected;

  delay(100);
}
