#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <Adafruit_LTR329_LTR303.h>
#include <Adafruit_MPL3115A2.h>
#include <Adafruit_Si7021.h>
#include <rhio-LIS2HH12.h>

// ── Configuration ─────────────────────────────────────────────────────────────
#define WIFI_SSID             "neuralis"
#define WIFI_PASSWORD         "YOUR_WIFI_PASSWORD"

#define NB_API_TOKEN          "YOUR_API_TOKEN"
#define NB_USER_KEY           "YOUR_USER_KEY"
#define NB_DEVICE_CODE        "YOUR_DEVICE_CODE"

#define HEARTBEAT_INTERVAL_MS (30UL * 60 * 1000)
#define WIFI_RETRY_INTERVAL   30000UL
#define POST_MAX_RETRIES      5

// ── Alert thresholds ──────────────────────────────────────────────────────────
#define TEMP_HIGH_C         35.0f
#define TEMP_LOW_C           5.0f
#define HUM_HIGH_PCT        80.0f
#define HUM_LOW_PCT         20.0f
#define PRES_LOW_HPA       970.0f
#define LIGHT_HIGH_LUX   10000.0f
#define ACCEL_QUAKE_G       0.20f   // deviation from 1g to count as shaking
#define ACCEL_QUAKE_SAMPLES     5   // consecutive samples needed (~500ms)
#define ALERT_COOLDOWN_MS  (5UL * 60 * 1000)
#define SENSOR_CHECK_MS     (1UL * 60 * 1000)

// LoPy4: onboard WS2812B on GPIO0
// PySense v1.1 button: P14 = GPIO37 (input-only, external pull-up on board)
// PySense v1.1 I2C:    SDA=P22=GPIO25, SCL=P21=GPIO26
#define RGB_PIN          0
#define BOOT_BUTTON_PIN  37
#define I2C_SDA          25
#define I2C_SCL          26
// ─────────────────────────────────────────────────────────────────────────────

// ── Hardware instances ────────────────────────────────────────────────────────
static Adafruit_NeoPixel  led(1, RGB_PIN, NEO_GRB + NEO_KHZ800);
static Adafruit_LTR329    ltr;
static Adafruit_MPL3115A2 baro;
static Adafruit_Si7021    rh;
static LIS2HH12           lis;

static bool ltrReady  = false;
static bool baroReady = false;
static bool rhReady   = false;
static bool lisReady  = false;

// ── RGB LED ───────────────────────────────────────────────────────────────────
#define BRIGHTNESS 40

static inline uint32_t color(uint8_t r, uint8_t g, uint8_t b) {
  return led.Color(r * BRIGHTNESS / 255, g * BRIGHTNESS / 255, b * BRIGHTNESS / 255);
}

#define RED   color(255,   0,   0)
#define GREEN color(  0, 255,   0)
#define BLUE  color(  0,   0, 255)
#define OFF   color(  0,   0,   0)

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
        led.setPixelColor(0, ledOn ? RED : OFF);
        led.show();
        lastToggleMs = now;
      }
      break;
    case LED_DISCONNECTED:
      if (now - lastToggleMs >= 500) {
        ledOn = !ledOn;
        led.setPixelColor(0, ledOn ? RED : OFF);
        led.show();
        lastToggleMs = now;
      }
      break;
    case LED_SENDING:
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
  Serial.printf("[WiFi] Connecting to %s ...", WIFI_SSID);
  setLedMode(LED_CONNECTING);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    updateLed();
    delay(100);
    Serial.print(".");
  }
  Serial.printf("\n[WiFi] Connected — IP: %s  RSSI: %d dBm\n",
                WiFi.localIP().toString().c_str(), WiFi.RSSI());
  setLedMode(LED_IDLE);
}

bool ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;

  static unsigned long lastAttemptMs = 0;
  if (millis() - lastAttemptMs < WIFI_RETRY_INTERVAL) return false;
  lastAttemptMs = millis();

  Serial.printf("[WiFi] Reconnecting to %s ...", WIFI_SSID);
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
    Serial.printf("\n[WiFi] Reconnected — IP: %s\n", WiFi.localIP().toString().c_str());
    setLedMode(LED_IDLE);
    return true;
  }

  Serial.println("\n[WiFi] Reconnect failed — will retry later");
  setLedMode(LED_DISCONNECTED);
  return false;
}

bool readAllSensors(char* buf, size_t len); // forward declaration

// ── HTTP POST ─────────────────────────────────────────────────────────────────
bool postNotification(const String& message, int priority = 0) {
  Serial.printf("[POST] Sending: \"%s\" (priority %d)\n", message.c_str(), priority);

  if (!ensureWiFi()) {
    Serial.println("[POST] No WiFi — notification dropped");
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
      Serial.printf("[POST] Response %d: %s\n", code, http.getString().c_str());
      http.end();
      setLedMode(LED_IDLE);
      return (code >= 200 && code < 300);
    }

    Serial.printf("[POST] Attempt %d/%d failed (%s) — retry in %lums\n",
                  attempt, POST_MAX_RETRIES,
                  http.errorToString(code).c_str(), backoff);
    http.end();
    delay(backoff);
    backoff = min(backoff * 2, 60000UL);
  }

  Serial.println("[POST] All retries exhausted");
  setLedMode(LED_IDLE);
  return false;
}

// ── Heartbeat ─────────────────────────────────────────────────────────────────
void sendHeartbeat() {
  unsigned long sec = millis() / 1000;
  char sensorData[256];
  readAllSensors(sensorData, sizeof(sensorData));

  char msg[380];
  snprintf(msg, sizeof(msg),
    "Heartbeat | uptime %luh %02lum %02lus | RSSI %d dBm | %s",
    sec / 3600, (sec % 3600) / 60, sec % 60,
    WiFi.RSSI(),
    sensorData);
  Serial.printf("[HB] %s\n", msg);
  postNotification(msg, -1);
}

static float calcLux(uint16_t ch0, uint16_t ch1); // forward declaration

// ── Sensor threshold checks ───────────────────────────────────────────────────
void checkSensorThresholds() {
  unsigned long now = millis();
  unsigned long sec = now / 1000;
  char uptime[32];
  snprintf(uptime, sizeof(uptime), "%luh %02lum %02lus",
           sec / 3600, (sec % 3600) / 60, sec % 60);

  static unsigned long lastAlertTempMs  = 0;
  static unsigned long lastAlertHumMs   = 0;
  static unsigned long lastAlertPresMs  = 0;
  static unsigned long lastAlertLightMs = 0;

  if (rhReady) {
    float t = rh.readTemperature();
    float h = rh.readHumidity();

    if (t > TEMP_HIGH_C && now - lastAlertTempMs >= ALERT_COOLDOWN_MS) {
      char msg[120];
      snprintf(msg, sizeof(msg), "Alert | T:%.1fC > %.0fC | uptime %s", t, (double)TEMP_HIGH_C, uptime);
      Serial.printf("[ALERT] %s\n", msg);
      postNotification(msg, 2);
      lastAlertTempMs = millis();
    } else if (t < TEMP_LOW_C && now - lastAlertTempMs >= ALERT_COOLDOWN_MS) {
      char msg[120];
      snprintf(msg, sizeof(msg), "Alert | T:%.1fC < %.0fC | uptime %s", t, (double)TEMP_LOW_C, uptime);
      Serial.printf("[ALERT] %s\n", msg);
      postNotification(msg, 2);
      lastAlertTempMs = millis();
    }

    if (h > HUM_HIGH_PCT && now - lastAlertHumMs >= ALERT_COOLDOWN_MS) {
      char msg[120];
      snprintf(msg, sizeof(msg), "Alert | H:%.0f%% > %.0f%% | uptime %s", h, (double)HUM_HIGH_PCT, uptime);
      Serial.printf("[ALERT] %s\n", msg);
      postNotification(msg, 2);
      lastAlertHumMs = millis();
    } else if (h < HUM_LOW_PCT && now - lastAlertHumMs >= ALERT_COOLDOWN_MS) {
      char msg[120];
      snprintf(msg, sizeof(msg), "Alert | H:%.0f%% < %.0f%% | uptime %s", h, (double)HUM_LOW_PCT, uptime);
      Serial.printf("[ALERT] %s\n", msg);
      postNotification(msg, 2);
      lastAlertHumMs = millis();
    }
  }

  if (baroReady) {
    float p = baro.getPressure();
    if (p < PRES_LOW_HPA && now - lastAlertPresMs >= ALERT_COOLDOWN_MS) {
      char msg[120];
      snprintf(msg, sizeof(msg), "Alert | P:%.0fhPa < %.0fhPa | uptime %s", p, (double)PRES_LOW_HPA, uptime);
      Serial.printf("[ALERT] %s\n", msg);
      postNotification(msg, 2);
      lastAlertPresMs = millis();
    }
  }

  if (ltrReady) {
    uint16_t ch0 = 0, ch1 = 0;
    unsigned long deadline = millis() + 500;
    while (!ltr.newDataAvailable() && millis() < deadline) delay(10);
    if (ltr.readBothChannels(ch0, ch1)) {
      float lux = calcLux(ch0, ch1);
      if (lux > LIGHT_HIGH_LUX && now - lastAlertLightMs >= ALERT_COOLDOWN_MS) {
        char msg[120];
        snprintf(msg, sizeof(msg), "Alert | L:%.0flux > %.0flux | uptime %s", lux, (double)LIGHT_HIGH_LUX, uptime);
        Serial.printf("[ALERT] %s\n", msg);
        postNotification(msg, 2);
        lastAlertLightMs = millis();
      }
    }
  }
}

// ── Sensors ───────────────────────────────────────────────────────────────────
void initSensors() {
  Serial.println("[SENSOR] Initializing sensors...");

  // LTR329 — ambient light
  if (ltr.begin(&Wire)) {
    ltr.setGain(LTR3XX_GAIN_1);
    ltr.setIntegrationTime(LTR3XX_INTEGTIME_100);
    ltr.setMeasurementRate(LTR3XX_MEASRATE_200);
    delay(220); // wait for first measurement cycle
    ltrReady = true;
    Serial.println("[SENSOR] LTR329  (light)       OK");
  } else {
    Serial.println("[SENSOR] LTR329  (light)       NOT FOUND");
  }

  // MPL3115A2 — pressure / altitude
  if (baro.begin(&Wire)) {
    baroReady = true;
    Serial.println("[SENSOR] MPL3115A2 (pressure)  OK");
  } else {
    Serial.println("[SENSOR] MPL3115A2 (pressure)  NOT FOUND");
  }

  // Si7006 — temperature / humidity
  if (rh.begin()) {
    rhReady = true;
    Serial.printf("[SENSOR] Si7006  (temp/hum)    OK  model=%d rev=%d\n",
                  rh.getModel(), rh.getRevision());
  } else {
    Serial.println("[SENSOR] Si7006  (temp/hum)    NOT FOUND");
  }

  // LIS2HH12 — accelerometer (begin() has no return value; assume present)
  lis.setI2C(0x1E);
  lis.begin();
  lis.setBasicConfig();
  lis.setFrequency(48); // 100 Hz
  lis.setAxis(7);       // X + Y + Z
  lis.setFS(0);         // ±2 g
  lisReady = true;
  Serial.println("[SENSOR] LIS2HH12 (accel)      initialized");

  Serial.printf("[SENSOR] Ready: light=%d baro=%d rh=%d accel=%d\n",
                ltrReady, baroReady, rhReady, lisReady);
}

// LTR329 lux formula from datasheet (gain=1x, integ=100ms → no scaling needed)
// ch0 = full spectrum (vis+IR), ch1 = IR only
static float calcLux(uint16_t ch0, uint16_t ch1) {
  if (ch0 == 0 && ch1 == 0) return 0.0f;
  float ratio = (float)ch1 / (float)(ch0 + ch1);
  float lux;
  if      (ratio < 0.45f) lux = 1.7743f * ch0 + 1.1059f * ch1;
  else if (ratio < 0.64f) lux = 4.2785f * ch0 - 1.9548f * ch1;
  else if (ratio < 0.85f) lux = 0.5926f * ch0 + 0.1185f * ch1;
  else                    lux = 0.0f;
  return lux < 0.0f ? 0.0f : lux;
}

// Builds a sensor snapshot string into buf. Returns false if no sensors available.
bool readAllSensors(char* buf, size_t len) {
  size_t pos = 0;
  bool any = false;

  // Temperature / humidity
  if (rhReady) {
    float t = rh.readTemperature();
    float h = rh.readHumidity();
    Serial.printf("[SENSOR] Si7006  temp=%.1fC hum=%.0f%%\n", t, h);
    pos += snprintf(buf + pos, len - pos, "T:%.1fC H:%.0f%% | ", t, h);
    any = true;
  }

  // Pressure / altitude
  if (baroReady) {
    float p = baro.getPressure();
    float a = baro.getAltitude();
    Serial.printf("[SENSOR] MPL3115 pres=%.0fhPa alt=%.0fm\n", p, a);
    pos += snprintf(buf + pos, len - pos, "P:%.0fhPa A:%.0fm | ", p, a);
    any = true;
  }

  // Accelerometer
  if (lisReady) {
    float x, y, z;
    lis.getAccel(&x, &y, &z);
    Serial.printf("[SENSOR] LIS2HH12 x=%.2f y=%.2f z=%.2fg\n", x, y, z);
    pos += snprintf(buf + pos, len - pos, "Acc:%.2f,%.2f,%.2fg | ", x, y, z);
    any = true;
  }

  // Light
  if (ltrReady) {
    uint16_t ch0 = 0, ch1 = 0;
    unsigned long deadline = millis() + 500;
    while (!ltr.newDataAvailable() && millis() < deadline) delay(10);
    if (ltr.readBothChannels(ch0, ch1)) {
      float lux = calcLux(ch0, ch1);
      Serial.printf("[SENSOR] LTR329  vis=%u ir=%u lux=%.0f\n", ch0, ch1, lux);
      pos += snprintf(buf + pos, len - pos, "L:%.0flux", lux);
    } else {
      Serial.println("[SENSOR] LTR329  read failed");
      pos += snprintf(buf + pos, len - pos, "L:err");
    }
    any = true;
  }

  if (!any) strlcpy(buf, "no sensors", len);
  return any;
}

// ── Button ISR ────────────────────────────────────────────────────────────────
static volatile bool buttonPressed = false;

void IRAM_ATTR onBootButton() { buttonPressed = true; }

// ─────────────────────────────────────────────────────────────────────────────

static unsigned long lastHeartbeatMs = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("\n[BOOT] Starting up...");

  led.begin();
  led.setBrightness(255);
  led.setPixelColor(0, OFF);
  led.show();

  Serial.printf("[BOOT] I2C SDA=GPIO%d SCL=GPIO%d\n", I2C_SDA, I2C_SCL);
  Wire.begin(I2C_SDA, I2C_SCL);
  delay(10);

  initSensors();

  pinMode(BOOT_BUTTON_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(BOOT_BUTTON_PIN), onBootButton, FALLING);
  Serial.printf("[BOOT] Button on GPIO%d\n", BOOT_BUTTON_PIN);

  connectWiFi();
  postNotification("Device is alive");
  lastHeartbeatMs = millis();
  Serial.println("[BOOT] Setup complete");
}

void loop() {
  updateLed();

  if (buttonPressed) {
    buttonPressed = false;
    delay(50);
    if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
      Serial.println("[BTN] Button confirmed — reading sensors");
      char sensorData[256];
      readAllSensors(sensorData, sizeof(sensorData));

      char msg[280];
      snprintf(msg, sizeof(msg), "Sensors | %s", sensorData);
      postNotification(msg, 1);
    } else {
      Serial.println("[BTN] Spurious interrupt — ignored");
    }
  }

  if (millis() - lastHeartbeatMs >= HEARTBEAT_INTERVAL_MS) {
    sendHeartbeat();
    lastHeartbeatMs = millis();
  }

  // Earthquake detection — runs every loop iteration (~100ms)
  if (lisReady) {
    static int           quakeSamples     = 0;
    static unsigned long lastQuakeAlertMs = 0;
    float x, y, z;
    lis.getAccel(&x, &y, &z);
    float mag = sqrtf(x*x + y*y + z*z);
    if (fabsf(mag - 1.0f) > ACCEL_QUAKE_G) {
      if (++quakeSamples >= ACCEL_QUAKE_SAMPLES &&
          millis() - lastQuakeAlertMs >= ALERT_COOLDOWN_MS) {
        unsigned long sec = millis() / 1000;
        char msg[128];
        snprintf(msg, sizeof(msg),
          "Alert | Earthquake detected | acc:%.2fg | uptime %luh %02lum %02lus",
          mag, sec / 3600, (sec % 3600) / 60, sec % 60);
        Serial.printf("[QUAKE] %s\n", msg);
        postNotification(msg, 2);
        lastQuakeAlertMs = millis();
        quakeSamples = 0;
      }
    } else {
      quakeSamples = 0;
    }
  }

  // Sensor read + threshold check every minute
  static unsigned long lastSensorCheckMs = 0;
  if (millis() - lastSensorCheckMs >= SENSOR_CHECK_MS) {
    char sensorBuf[256];
    readAllSensors(sensorBuf, sizeof(sensorBuf));
    Serial.printf("[LOG] %s\n", sensorBuf);
    checkSensorThresholds();
    lastSensorCheckMs = millis();
  }

  static bool wasConnected = true;
  bool connected = ensureWiFi();
  if (connected && !wasConnected) {
    Serial.println("[WiFi] Link restored");
    postNotification("WiFi reconnected", 2);
  }
  wasConnected = connected;

  delay(100);
}
