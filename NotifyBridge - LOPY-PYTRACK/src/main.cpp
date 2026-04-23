#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <rhio-LIS2HH12.h>
#include <TinyGPSPlus.h>
#include <sys/time.h>

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
#define SPEED_HIGH_KMH      120.0f  // GPS speed alert
#define ACCEL_QUAKE_G         0.20f // deviation from 1g to count as shaking
#define ACCEL_QUAKE_SAMPLES       3 // consecutive samples needed (~300 ms)
#define ALERT_COOLDOWN_MS  (5UL * 60 * 1000)
#define SENSOR_CHECK_MS     (1UL * 60 * 1000)

// LoPy4: onboard WS2812B on GPIO0
// PyTrack v2.0 button: P14 = GPIO37 (input-only, external pull-up on board)
// PyTrack v2.0 I2C:    SDA=P22=GPIO25, SCL=P21=GPIO26
// PyTrack v2.0 GPS:    L76GNSS on I2C bus at address 0x10
#define RGB_PIN          0
#define BOOT_BUTTON_PIN  37
#define I2C_SDA          25
#define I2C_SCL          26
#define GPS_I2C_ADDR     0x10
// ─────────────────────────────────────────────────────────────────────────────

// ── Hardware instances ────────────────────────────────────────────────────────
static Adafruit_NeoPixel led(1, RGB_PIN, NEO_GRB + NEO_KHZ800);
static LIS2HH12          lis;
static TinyGPSPlus       gps;

static bool lisReady = false;

// ── GPS time sync ─────────────────────────────────────────────────────────────
static bool timeSynced = false;

// Pure integer Unix epoch calculation — verified for 2026-04-23
static time_t gpsToEpoch(uint16_t year, uint8_t month, uint8_t day,
                          uint8_t hour, uint8_t minute, uint8_t second) {
  static const uint16_t dpm[] = {0,31,59,90,120,151,181,212,243,273,304,334};
  bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;

  // days from 1970-01-01 to Jan 1 of year
  int32_t y   = year - 1970;
  int32_t ly  = (year - 1) / 4 - 492      // leap years ÷4  (1969/4=492)
              - ((year - 1) / 100 - 19)   // minus centuries (1969/100=19)
              + ((year - 1) / 400 - 4);   // plus 400-cycles (1969/400=4)
  int32_t days = y * 365 + ly;

  days += dpm[month - 1];
  if (month > 2 && leap) days++;
  days += day - 1;

  return (time_t)((int64_t)days * 86400
                + hour * 3600 + minute * 60 + second);
}

static void formatTimestamp(char* buf, size_t len) {
  time_t now = time(nullptr);
  if (now > 1700000000L) { // RTC is set (> Nov 2023)
    struct tm* t = gmtime(&now);
    strftime(buf, len, "%Y-%m-%d %H:%M:%S UTC", t);
  } else {
    unsigned long sec = millis() / 1000;
    snprintf(buf, len, "uptime %luh %02lum %02lus", sec/3600, (sec%3600)/60, sec%60);
  }
}

static void syncTimeFromGPS() {
  if (!gps.time.isValid() || !gps.date.isValid()) return;
  if (gps.date.year() < 2020) return;

  time_t epoch = gpsToEpoch(gps.date.year(), gps.date.month(), gps.date.day(),
                             gps.time.hour(), gps.time.minute(), gps.time.second());
  struct timeval tv = { epoch, 0 };
  settimeofday(&tv, nullptr);

  if (!timeSynced) {
    timeSynced = true;
    char ts[32]; formatTimestamp(ts, sizeof(ts));
    Serial.printf("[GPS] Clock synced: %s\n", ts);
  }
}

// ── Position drift tracking ───────────────────────────────────────────────────
static double driftRefLat = 0, driftRefLon = 0;
static bool   driftRefSet  = false;
static double driftMaxM    = 0;
static int    driftSamples = 0;

static void updateDrift() {
  if (!gps.location.isValid()) return;
  double lat = gps.location.lat();
  double lon = gps.location.lng();
  if (!driftRefSet) {
    driftRefLat = lat; driftRefLon = lon;
    driftRefSet = true;
    return;
  }
  double d = TinyGPSPlus::distanceBetween(driftRefLat, driftRefLon, lat, lon);
  if (d > driftMaxM) driftMaxM = d;
  driftSamples++;
}

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
    case LED_IDLE: {
      uint32_t idleColor = gps.location.isValid() ? GREEN : BLUE;
      if (!ledOn && now - lastToggleMs >= 3000) {
        ledOn = true;
        led.setPixelColor(0, idleColor);
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
  char ts[32];
  formatTimestamp(ts, sizeof(ts));

  char sensorData[256];
  readAllSensors(sensorData, sizeof(sensorData));

  char driftBuf[48] = "";
  if (driftRefSet && driftSamples > 0)
    snprintf(driftBuf, sizeof(driftBuf), " | drift:%.1fm(n=%d)", driftMaxM, driftSamples);

  char msg[420];
  snprintf(msg, sizeof(msg), "%s | RSSI %d dBm | %s%s",
           ts, WiFi.RSSI(), sensorData, driftBuf);
  Serial.printf("[HB] %s\n", msg);
  postNotification(msg, -1);

  driftMaxM = 0;
  driftSamples = 0;
}

// ── GPS feed ──────────────────────────────────────────────────────────────────
// L76GNSS sends NMEA bytes over I2C; 0x0A padding is sent when idle
static void feedGPS(unsigned long ms = 100) {
  unsigned long deadline = millis() + ms;
  while (millis() < deadline) {
    Wire.requestFrom((uint8_t)GPS_I2C_ADDR, (uint8_t)32);
    while (Wire.available()) {
      char c = Wire.read();
      if (c != 0x0A) gps.encode(c); // skip padding bytes
    }
    delay(5);
  }
}

// ── I2C scan ──────────────────────────────────────────────────────────────────
static void scanI2C() {
  Serial.println("[I2C] Scanning bus...");
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("[I2C]   0x%02X\n", addr);
      found++;
    }
  }
  if (!found) Serial.println("[I2C]   no devices found");
}

// ── LIS2HH12 init / recovery ──────────────────────────────────────────────────
static void initLIS() {
  lisReady = false;
  Wire.end();
  delay(10);
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(50000);
  delay(10);

  static const uint8_t LIS_ADDRS[] = {0x1E, 0x1C};
  uint8_t lisAddr = 0;
  for (uint8_t addr : LIS_ADDRS) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) { lisAddr = addr; break; }
  }
  if (lisAddr) {
    lis.setI2C(lisAddr);
    lis.begin();
    lis.setBasicConfig();
    lis.setFrequency(48);
    lis.setAxis(7);
    lis.setFS(0);
    lisReady = true;
    Serial.printf("[SENSOR] LIS2HH12 (accel)      OK at 0x%02X\n", lisAddr);
  } else {
    Serial.println("[SENSOR] LIS2HH12 (accel)      NOT FOUND — will retry");
  }
}

// ── Sensors ───────────────────────────────────────────────────────────────────
void initSensors() {
  Serial.println("[SENSOR] Initializing sensors...");
  scanI2C();
  initLIS();

  // L76GNSS — GPS via I2C
  Wire.beginTransmission(GPS_I2C_ADDR);
  if (Wire.endTransmission() == 0)
    Serial.printf("[SENSOR] L76GNSS  (GPS)         OK at I2C 0x%02X\n", GPS_I2C_ADDR);
  else
    Serial.printf("[SENSOR] L76GNSS  (GPS)         NOT FOUND at I2C 0x%02X\n", GPS_I2C_ADDR);

  Serial.printf("[SENSOR] Ready: accel=%d gps=I2C\n", lisReady);
}

// Builds a sensor snapshot string into buf. Returns false if no sensors available.
bool readAllSensors(char* buf, size_t len) {
  size_t pos = 0;
  bool any = false;

  // Discard one read to flush any stale register state left by WiFi RF noise
  if (lisReady) { float _x, _y, _z; lis.getAccel(&_x, &_y, &_z); delay(10); }

  // Accelerometer
  if (lisReady) {
    float x, y, z;
    lis.getAccel(&x, &y, &z);
    if (fabsf(x) > 4.0f || fabsf(y) > 4.0f || fabsf(z) > 4.0f) {
      Serial.printf("[SENSOR] LIS2HH12 bad read (%.2f,%.2f,%.2fg) — recovering I2C\n", x, y, z);
      initLIS();
      if (lisReady) lis.getAccel(&x, &y, &z);
    }
    if (lisReady) {
      Serial.printf("[SENSOR] LIS2HH12 x=%.2f y=%.2f z=%.2fg\n", x, y, z);
      pos += snprintf(buf + pos, len - pos, "Acc:%.2f,%.2f,%.2fg | ", x, y, z);
      any = true;
    }
  }

  // GPS
  feedGPS(200);
  if (gps.location.isValid()) {
    Serial.printf("[SENSOR] L76GNSS  lat=%.6f lon=%.6f spd=%.1fkm/h alt=%.0fm sats=%d\n",
                  gps.location.lat(), gps.location.lng(),
                  gps.speed.kmph(), gps.altitude.meters(),
                  gps.satellites.value());
    pos += snprintf(buf + pos, len - pos,
                    "GPS:%.6f,%.6f spd:%.1fkm/h alt:%.0fm sats:%d",
                    gps.location.lat(), gps.location.lng(),
                    gps.speed.kmph(), gps.altitude.meters(),
                    gps.satellites.value());
  } else {
    Serial.println("[SENSOR] L76GNSS  no fix");
    pos += snprintf(buf + pos, len - pos, "GPS:no fix (sats:%d)", gps.satellites.value());
  }
  any = true;

  if (!any) strlcpy(buf, "no sensors", len);
  return any;
}

// ── Sensor threshold checks ───────────────────────────────────────────────────
void checkSensorThresholds() {
  unsigned long now = millis();

  static unsigned long lastAlertSpeedMs = 0UL - ALERT_COOLDOWN_MS;

  feedGPS(50);
  if (gps.location.isValid() && gps.speed.isValid()) {
    float spd = gps.speed.kmph();
    if (spd > SPEED_HIGH_KMH && now - lastAlertSpeedMs >= ALERT_COOLDOWN_MS) {
      char ts[32]; formatTimestamp(ts, sizeof(ts));
      char msg[120];
      snprintf(msg, sizeof(msg), "Alert | Speed:%.1fkm/h > %.0fkm/h | %s",
               spd, (double)SPEED_HIGH_KMH, ts);
      Serial.printf("[ALERT] %s\n", msg);
      postNotification(msg, 2);
      lastAlertSpeedMs = millis();
    }
  }
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
  Wire.setClock(50000); // 50 kHz — more tolerant of WiFi RF noise
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
  // Feed GPS continuously so TinyGPS++ parses incoming NMEA sentences
  feedGPS(10);
  syncTimeFromGPS();
  if (gps.location.isUpdated()) updateDrift();

  updateLed();

  if (buttonPressed) {
    buttonPressed = false;
    delay(50);
    if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
      Serial.println("[BTN] Button confirmed — reading sensors");
      char ts[32]; formatTimestamp(ts, sizeof(ts));
      char sensorData[256];
      readAllSensors(sensorData, sizeof(sensorData));

      char msg[300];
      snprintf(msg, sizeof(msg), "%s | %s", ts, sensorData);
      postNotification(msg, 1);
    } else {
      Serial.println("[BTN] Spurious interrupt — ignored");
    }
  }

  if (millis() - lastHeartbeatMs >= HEARTBEAT_INTERVAL_MS) {
    sendHeartbeat();
    lastHeartbeatMs = millis();
  }

  // Earthquake detection — runs every loop iteration (~100 ms)
  if (lisReady) {
    static int           quakeSamples     = 0;
    static unsigned long lastQuakeAlertMs = 0UL - ALERT_COOLDOWN_MS;
    float x, y, z;
    lis.getAccel(&x, &y, &z);
    if (fabsf(x) > 4.0f || fabsf(y) > 4.0f || fabsf(z) > 4.0f) {
      initLIS();
      quakeSamples = 0;
    } else {
      float mag = sqrtf(x*x + y*y + z*z);
      if (fabsf(mag - 1.0f) > ACCEL_QUAKE_G) {
        if (++quakeSamples >= ACCEL_QUAKE_SAMPLES &&
            millis() - lastQuakeAlertMs >= ALERT_COOLDOWN_MS) {
          char ts[32]; formatTimestamp(ts, sizeof(ts));
          char msg[128];
          snprintf(msg, sizeof(msg),
            "Alert | Earthquake detected | acc:%.2fg | %s", mag, ts);
          Serial.printf("[QUAKE] %s\n", msg);
          postNotification(msg, 2);
          lastQuakeAlertMs = millis();
          quakeSamples = 0;
        }
      } else {
        quakeSamples = 0;
      }
    }
  }

  // Sensor read + threshold check every minute
  static unsigned long lastSensorCheckMs = 0;
  if (millis() - lastSensorCheckMs >= SENSOR_CHECK_MS) {
    char sensorBuf[256];
    readAllSensors(sensorBuf, sizeof(sensorBuf));
    Serial.printf("[LOG] %s\n", sensorBuf);
    char dbgTs[32]; formatTimestamp(dbgTs, sizeof(dbgTs));
    Serial.printf("[GPS] raw=%04d-%02d-%02d %02d:%02d:%02d  synced=%d  fmt=\"%s\"\n",
                  gps.date.year(), gps.date.month(), gps.date.day(),
                  gps.time.hour(), gps.time.minute(), gps.time.second(),
                  timeSynced, dbgTs);
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
