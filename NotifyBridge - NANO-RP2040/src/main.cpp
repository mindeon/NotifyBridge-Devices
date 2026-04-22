#include <Arduino.h>
#include <WiFiNINA.h>
#include <ArduinoHttpClient.h>
#include <Arduino_LSM6DSOX.h>
#include <PDM.h>

// ── Configuration ─────────────────────────────────────────────────────────────
#define WIFI_SSID             "YOUR_WIFI_SSID"
#define WIFI_PASSWORD         "YOUR_WIFI_PASSWORD"

#define NB_API_TOKEN          "YOUR_API_TOKEN"
#define NB_USER_KEY           "YOUR_USER_KEY"
#define NB_DEVICE_CODE        "YOUR_DEVICE_CODE"

#define HEARTBEAT_INTERVAL_MS  (1UL * 60 * 60 * 1000)
#define WIFI_RETRY_INTERVAL    30000UL
#define WIFI_CONNECT_TIMEOUT   30000UL
#define POST_MAX_RETRIES       5

#define NB_HOST "notifybridge.mindeon.net"
#define NB_PORT 443
#define NB_PATH "/v1/messages/send"

// ── Single-event alert thresholds ─────────────────────────────────────────────
#define JOLT_DELTA_G          0.4f    // sudden single change → "Vibration detected"
#define NOISE_SPIKE_FACTOR    20.0f    // Nx above ambient → "Sudden noise"
#define NOISE_MIN_RMS         150.0f  // mic noise floor
#define ALERT_COOLDOWN_MS     10000UL

// ── Earthquake detection ───────────────────────────────────────────────────────
// Accel: count micro-jolts in a rolling window — earthquakes cause sustained
// low-frequency oscillation (1–5 Hz), not just a single spike.
#define QUAKE_JOLT_DELTA_G    0.15f   // smaller delta used only for quake counter
#define QUAKE_MIN_JOLTS       5       // micro-jolts in window to flag sustained vibration
#define QUAKE_WINDOW_MS       5000UL  // rolling window for jolt counting
// Combined: both sustained vibration AND a noise spike must happen within this
// window to trigger the earthquake alert.
#define QUAKE_COINCIDENCE_MS  15000UL
#define QUAKE_COOLDOWN_MS     120000UL // 2 min between quake alerts

// ── Misc ──────────────────────────────────────────────────────────────────────
#define DEBUG_PRINT_INTERVAL  2000UL
// ─────────────────────────────────────────────────────────────────────────────

// ── LED ───────────────────────────────────────────────────────────────────────
#define STATUS_LED LED_BUILTIN

void writeLed(bool on) { digitalWrite(STATUS_LED, on ? HIGH : LOW); }

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
      if (now - lastToggleMs >= 100) { ledOn = !ledOn; writeLed(ledOn); lastToggleMs = now; }
      break;
    case LED_DISCONNECTED:
      if (now - lastToggleMs >= 500) { ledOn = !ledOn; writeLed(ledOn); lastToggleMs = now; }
      break;
    case LED_SENDING:
      if (blinkCount < 6) {
        if (now - lastToggleMs >= 50) { ledOn = !ledOn; writeLed(ledOn); lastToggleMs = now; blinkCount++; }
      } else { blinkCount = 0; setLedMode(LED_IDLE); }
      break;
    case LED_IDLE:
      if (!ledOn && now - lastToggleMs >= 3000)      { ledOn = true;  writeLed(true);  lastToggleMs = now; }
      else if (ledOn && now - lastToggleMs >= 50)    { ledOn = false; writeLed(false); lastToggleMs = now; }
      break;
  }
}

// ── LSM6DSOX ──────────────────────────────────────────────────────────────────
float readChipTemp() {
  int temp;
  if (IMU.readTemperature(temp)) return (float)temp;
  return 0.0f;
}

float readAccelMagnitude() {
  float x, y, z;
  if (IMU.readAcceleration(x, y, z)) return sqrt(x * x + y * y + z * z);
  return 0.0f;
}

// ── PDM microphone — non-blocking ─────────────────────────────────────────────
static short        pdmBuffer[256];
static volatile int pdmSamplesReady = 0;

void onPDMdata() {
  int bytes = PDM.available();
  PDM.read(pdmBuffer, bytes);
  pdmSamplesReady = bytes / 2;
}

float pollNoiseRMS() {
  if (pdmSamplesReady == 0) return -1.0f;
  int n = pdmSamplesReady;
  pdmSamplesReady = 0;
  float sum = 0;
  for (int i = 0; i < n; i++) sum += (float)pdmBuffer[i] * (float)pdmBuffer[i];
  return sqrt(sum / n);
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
    updateLed(); delay(100); Serial.print(".");
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
    updateLed(); delay(100); Serial.print(".");
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
  if (!ensureWiFi()) { Serial.println("No WiFi — notification dropped"); return false; }

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
void sendHeartbeat(float accelBase, float noiseBase) {
  unsigned long sec = millis() / 1000;
  float temp = readChipTemp();
  char msg[220];
  snprintf(msg, sizeof(msg),
    "Heartbeat | uptime %luh %02lum %02lus | RSSI %d dBm | temp %.0fC | accel_base %.2fG | noise_base %.0f",
    sec / 3600, (sec % 3600) / 60, sec % 60,
    WiFi.RSSI(), temp, accelBase, noiseBase);
  postNotification(msg, -1);
}

// ─────────────────────────────────────────────────────────────────────────────

// Rolling jolt timestamps for earthquake sustained-vibration detection
static unsigned long joltTs[16];
static int           joltHead  = 0;
static int           joltTotal = 0; // saturates at 16

static unsigned long lastHeartbeatMs  = 0;
static unsigned long lastJoltAlertMs  = 0;
static unsigned long lastNoiseAlertMs = 0;
static unsigned long lastQuakeAlertMs = 0;
static unsigned long lastDebugPrintMs = 0;
static unsigned long lastNoiseSpikeMs = 0;  // last time noise spike was detected
static unsigned long lastVibMs        = 0;  // last time sustained vibration was detected

static float accelEMA = -1.0f;
static float noiseEMA = -1.0f;

// Count jolt timestamps that fall within the rolling quake window
int countRecentJolts(unsigned long now) {
  int count = 0;
  int n = min(joltTotal, 16);
  for (int i = 0; i < n; i++) {
    if (now - joltTs[i] <= QUAKE_WINDOW_MS) count++;
  }
  return count;
}

void setup() {
  Serial.begin(115200);
  delay(3000);
  Serial.println("NotifyBridge booting...");

  pinMode(STATUS_LED, OUTPUT);
  writeLed(false);

  if (!IMU.begin())            Serial.println("IMU init failed");
  PDM.onReceive(onPDMdata);
  if (!PDM.begin(1, 16000))   Serial.println("PDM init failed");

  connectWiFi();
  postNotification("Device is alive");
  lastHeartbeatMs = millis();
}

void loop() {
  updateLed();
  unsigned long now = millis();

  // ── Accelerometer ─────────────────────────────────────────────────────────────
  float mag = readAccelMagnitude();
  if (mag > 0) {
    if (accelEMA < 0) accelEMA = mag;
    float delta = abs(mag - accelEMA);
    accelEMA = 0.8f * accelEMA + 0.2f * mag;

    // Single jolt alert
    if (delta > JOLT_DELTA_G && now - lastJoltAlertMs > ALERT_COOLDOWN_MS) {
      char msg[80];
      snprintf(msg, sizeof(msg), "Vibration detected! delta: %.2fG (base: %.2fG)", delta, accelEMA);
      Serial.println(msg);
      postNotification(msg, 2);
      lastJoltAlertMs = millis();
    }

    // Record micro-jolts for quake sustained-vibration counter
    if (delta > QUAKE_JOLT_DELTA_G) {
      joltTs[joltHead] = now;
      joltHead = (joltHead + 1) % 16;
      if (joltTotal < 16) joltTotal++;
    }
  }

  // ── Noise ─────────────────────────────────────────────────────────────────────
  float rms = pollNoiseRMS();
  bool noiseSpikeNow = false;
  if (rms >= 0) {
    if (noiseEMA < 0) noiseEMA = rms;
    float spike = (noiseEMA > 1.0f) ? rms / noiseEMA : 0;
    noiseEMA = 0.95f * noiseEMA + 0.05f * rms;

    if (rms > NOISE_MIN_RMS && spike > NOISE_SPIKE_FACTOR) {
      noiseSpikeNow = true;
      lastNoiseSpikeMs = now;
      if (now - lastNoiseAlertMs > ALERT_COOLDOWN_MS) {
        char msg[80];
        snprintf(msg, sizeof(msg), "Sudden noise! level: %.0f (%.1fx above ambient)", rms, spike);
        Serial.println(msg);
        postNotification(msg, 2);
        lastNoiseAlertMs = millis();
      }
    }
  }

  // ── Earthquake detection ──────────────────────────────────────────────────────
  // Condition: sustained vibration (5+ micro-jolts in 5 s) AND a noise spike
  // both occurred within QUAKE_COINCIDENCE_MS of each other.
  int recentJolts = countRecentJolts(now);
  bool sustainedVib = (recentJolts >= QUAKE_MIN_JOLTS);
  if (sustainedVib) lastVibMs = now;

  bool noiseRecent = (now - lastNoiseSpikeMs <= QUAKE_COINCIDENCE_MS);
  bool vibRecent   = (now - lastVibMs        <= QUAKE_COINCIDENCE_MS);

  if (noiseRecent && vibRecent && now - lastQuakeAlertMs > QUAKE_COOLDOWN_MS) {
    char msg[120];
    snprintf(msg, sizeof(msg),
      "EARTHQUAKE detected! jolts: %d in 5s | noise: %.0f | accel_base: %.2fG",
      recentJolts, (rms >= 0 ? rms : 0), accelEMA);
    Serial.println(msg);
    postNotification(msg, 2);
    lastQuakeAlertMs = millis();
    // Reset quake state so it doesn't re-fire immediately
    lastNoiseSpikeMs = 0;
    lastVibMs        = 0;
    joltTotal        = 0;
  }

  // ── Debug print ───────────────────────────────────────────────────────────────
  if (now - lastDebugPrintMs >= DEBUG_PRINT_INTERVAL) {
    lastDebugPrintMs = now;
    char dbg[120];
    snprintf(dbg, sizeof(dbg),
      "accel: %.2fG base: %.2f | noise: %.0f base: %.0f | jolts/5s: %d | vib: %s noise: %s",
      mag, accelEMA,
      (rms >= 0 ? rms : 0), (noiseEMA >= 0 ? noiseEMA : 0),
      recentJolts,
      vibRecent  ? "Y" : "n",
      noiseRecent ? "Y" : "n");
    Serial.println(dbg);
  }

  // ── Heartbeat ─────────────────────────────────────────────────────────────────
  if (now - lastHeartbeatMs >= HEARTBEAT_INTERVAL_MS) {
    sendHeartbeat(accelEMA, noiseEMA);
    lastHeartbeatMs = millis();
  }

  // ── WiFi watchdog ─────────────────────────────────────────────────────────────
  static bool wasConnected = true;
  bool connected = ensureWiFi();
  if (connected && !wasConnected) postNotification("WiFi reconnected", 2);
  wasConnected = connected;

  delay(50);
}
