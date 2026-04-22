#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Adafruit_NeoPixel.h>

// ── Configuration ─────────────────────────────────────────────────────────────
#define WIFI_SSID             "neuralis"
#define WIFI_PASSWORD         "YOUR_WIFI_PASSWORD"

#define NB_API_TOKEN          "YOUR_API_TOKEN"
#define NB_USER_KEY           "YOUR_USER_KEY"
#define NB_DEVICE_CODE        "YOUR_DEVICE_CODE"

#define HEARTBEAT_INTERVAL_MS (1UL * 60 * 60 * 1000)
#define WIFI_RETRY_INTERVAL   30000UL
#define POST_MAX_RETRIES      5

// LoPy4: onboard WS2812B on GPIO0
// PySense v1.1 button is on P14 = GPIO37 (input-only, no internal pull-up — board has external pull-up)
#define RGB_PIN               0
#define BOOT_BUTTON_PIN       37
// ─────────────────────────────────────────────────────────────────────────────

static Adafruit_NeoPixel led(1, RGB_PIN, NEO_GRB + NEO_KHZ800);

// Dim factor (0–255) — the LoPy4 LED is very bright at full power
#define BRIGHTNESS 40

static inline uint32_t color(uint8_t r, uint8_t g, uint8_t b) {
  return led.Color(r * BRIGHTNESS / 255, g * BRIGHTNESS / 255, b * BRIGHTNESS / 255);
}

#define RED    color(255,   0,   0)
#define GREEN  color(  0, 255,   0)
#define BLUE   color(  0,   0, 255)
#define OFF    color(  0,   0,   0)

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
      // Fast red blink 100 ms
      if (now - lastToggleMs >= 100) {
        ledOn = !ledOn;
        led.setPixelColor(0, ledOn ? RED : OFF);
        led.show();
        lastToggleMs = now;
      }
      break;

    case LED_DISCONNECTED:
      // Slow red blink 500 ms
      if (now - lastToggleMs >= 500) {
        ledOn = !ledOn;
        led.setPixelColor(0, ledOn ? RED : OFF);
        led.show();
        lastToggleMs = now;
      }
      break;

    case LED_SENDING:
      // 3 rapid blue blinks (50 ms on/off), then back to idle
      if (blinkCount < 6) {
        if (now - lastToggleMs >= 50) {
          ledOn = !ledOn;
          led.setPixelColor(0, ledOn ? BLUE : OFF);
          led.show();
          lastToggleMs = now;
          blinkCount++;
        }
      } else {
        blinkCount = 0;
        setLedMode(LED_IDLE);
      }
      break;

    case LED_IDLE:
      // Brief green flash every 3 s
      if (!ledOn && now - lastToggleMs >= 3000) {
        ledOn = true;
        led.setPixelColor(0, GREEN);
        led.show();
        lastToggleMs = now;
      } else if (ledOn && now - lastToggleMs >= 50) {
        ledOn = false;
        led.setPixelColor(0, OFF);
        led.show();
        lastToggleMs = now;
      }
      break;
  }
}

// ── WiFi ──────────────────────────────────────────────────────────────────────
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

    Serial.printf("Attempt %d/%d failed (%s) — retry in %lums\n",
                  attempt, POST_MAX_RETRIES,
                  http.errorToString(code).c_str(), backoff);
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

// ── BOOT button ISR ───────────────────────────────────────────────────────────
static volatile bool buttonPressed = false;

void IRAM_ATTR onBootButton() { buttonPressed = true; }

// ─────────────────────────────────────────────────────────────────────────────

static unsigned long lastHeartbeatMs = 0;

void setup() {
  Serial.begin(115200);

  led.begin();
  led.setBrightness(255); // brightness handled per-color via BRIGHTNESS constant
  led.setPixelColor(0, OFF);
  led.show();

  pinMode(BOOT_BUTTON_PIN, INPUT); // GPIO37 is input-only, pull-up is on the PySense board
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
