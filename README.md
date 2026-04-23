# NotifyBridge Devices

Example device firmware for integrating microcontrollers with the [Mindeon NotifyBridge API](https://www.mindeon.net/api).

This repository contains PlatformIO-based Arduino projects for popular microcontroller platforms, demonstrating how to connect embedded devices to the NotifyBridge notification service.

## Supported Devices

| Device | Platform | Board | Notable Features |
|--------|----------|-------|-----------------|
| ESP32 | Espressif ESP32 | esp32dev | Button input, hourly heartbeat |
| Arduino MKR WiFi 1010 | Atmel SAM (ARM Cortex-M0+) | mkrwifi1010 | HTTPS via WiFiNINA, button input |
| Arduino Nano RP2040 Connect | RP2040 | arduino_nano_connect | IMU, microphone, earthquake detection |
| Raspberry Pi Pico W | RP2040 (WiFi) | rpipicow | Lightweight, button input |
| Raspberry Pi Pico 2W | RP2350 (WiFi) | rpipico2w | On-chip temperature sensor |
| LoPy4 + PySense | Espressif ESP32 | lopy4 | Temperature, humidity, pressure, light, accelerometer, earthquake detection |
| LoPy4 + PyTrack | Espressif ESP32 | lopy4 | GPS tracking, speed alerts, accelerometer, earthquake detection |

## Prerequisites

- [PlatformIO](https://platformio.org/) (CLI or VSCode extension)
- A [Mindeon NotifyBridge](https://www.mindeon.net/api) account with an API token and user key

## Getting Started

1. Clone the repository:
   ```bash
   git clone https://github.com/mindeon/NotifyBridge-Devices.git
   cd NotifyBridge-Devices
   ```

2. Open the project folder for your target device in PlatformIO:
   - `NotifyBridge - ESP32/`
   - `NotifyBridge - MKR1010/`
   - `NotifyBridge - NANO-RP2040/`
   - `NotifyBridge - RPi_PicoW/`
   - `NotifyBridge - RPi_Pico2W/`
   - `NotifyBridge - LOPY-PYSENSSE/`
   - `NotifyBridge - LOPY-PYTRACK/`

3. In `src/main.cpp`, replace the placeholder values in the configuration block at the top of the file:
   ```cpp
   #define WIFI_SSID      "YOUR_WIFI_SSID"
   #define WIFI_PASSWORD  "YOUR_WIFI_PASSWORD"

   #define NB_API_TOKEN   "YOUR_API_TOKEN"
   #define NB_USER_KEY    "YOUR_USER_KEY"
   #define NB_DEVICE_CODE "YOUR_DEVICE_CODE"
   ```

4. Build and upload:
   ```bash
   pio run --target upload
   ```

## Project Structure

```
NotifyBridge-Devices/
├── NotifyBridge - ESP32/           # ESP32 firmware
│   ├── platformio.ini
│   └── src/main.cpp
├── NotifyBridge - MKR1010/         # Arduino MKR WiFi 1010 firmware
│   ├── platformio.ini
│   └── src/main.cpp
├── NotifyBridge - NANO-RP2040/     # Arduino Nano RP2040 Connect firmware
│   ├── platformio.ini
│   └── src/main.cpp
├── NotifyBridge - RPi_PicoW/       # Raspberry Pi Pico W firmware
│   ├── platformio.ini
│   └── src/main.cpp
├── NotifyBridge - RPi_Pico2W/      # Raspberry Pi Pico 2W firmware
│   ├── platformio.ini
│   └── src/main.cpp
├── NotifyBridge - LOPY-PYSENSSE/   # LoPy4 + PySense firmware
│   ├── platformio.ini
│   └── src/main.cpp
└── NotifyBridge - LOPY-PYTRACK/    # LoPy4 + PyTrack firmware
    ├── platformio.ini
    └── src/main.cpp
```

## Device Details

### ESP32

- **Button** on GPIO 13 — sends a priority-1 notification on press
- **Heartbeat** every hour — includes uptime, RSSI, and IP address
- **WiFi reconnection** alert (priority 2) when connectivity is restored
- Uses the native ESP32 `HTTPClient` library for HTTPS POST

### Arduino MKR WiFi 1010

- **Button** on pin D7 — sends a priority-1 notification on press
- **Heartbeat** every hour — includes uptime, RSSI, and IP address
- **WiFi reconnection** alert (priority 2) when connectivity is restored
- Uses `WiFiNINA` + `ArduinoHttpClient` with SSL/TLS for HTTPS

### Arduino Nano RP2040 Connect

The most feature-rich device in this collection. It uses the on-board LSM6DSOX IMU and PDM microphone to detect environmental events.

#### Sensors

| Sensor | Library | Usage |
|--------|---------|-------|
| LSM6DSOX (IMU) | Arduino_LSM6DSOX | Acceleration magnitude, micro-jolt counting, chip temperature |
| PDM Microphone | PDM.h | 16 kHz audio, RMS noise measurement |

#### Detection Modes

| Event | Trigger | Priority | Cooldown |
|-------|---------|----------|----------|
| Vibration jolt | Accel delta > 0.4 G above baseline | 2 | 10 s |
| Sudden noise | RMS > 150 AND spike > 20× ambient | 2 | 10 s |
| Earthquake | 5+ micro-jolts in 5 s AND audio spike within 15 s | 2 | 2 min |
| Heartbeat | Every hour | −1 | — |

#### Earthquake Detection Algorithm

The earthquake detector fuses two independent sensor streams:

1. **Vibration stream** — counts micro-jolts (acceleration delta > 0.15 G) inside a 5-second rolling window using a circular timestamp buffer.
2. **Audio stream** — detects sudden noise spikes (RMS > 150 AND > 20× the running ambient level).
3. **Coincidence gate** — both events must occur within 15 seconds of each other to trigger a confirmed earthquake alert.

Baselines are updated continuously with exponential moving averages (EMA): 80 % weight for acceleration, 95 % for audio noise, allowing the detector to adapt to the ambient environment while remaining sensitive to sudden changes.

### Raspberry Pi Pico W

- **Button** on GPIO 15 — sends a priority-1 notification on press
- **Heartbeat** every hour — includes uptime, RSSI, and IP address
- **WiFi reconnection** alert (priority 2) when connectivity is restored
- LED controlled through the CYW43 WiFi chip (not a GPIO pin)
- Uses `WiFiClientSecure` (insecure mode) + `HTTPClient` for HTTPS

### Raspberry Pi Pico 2W

Identical to the Pico W with one addition:

- **On-chip temperature sensor** (RP2350 ADC channel 4) — chip temperature included in every heartbeat message
- LED mapped to GPIO 64 (CYW43 chip on Pico 2W variant)

### LoPy4 + PySense

Runs on a [Pycom LoPy4](https://pycom.io/) module mounted on the PySense v1.1 expansion board. Uses the ESP32 Arduino framework via PlatformIO.

#### Sensors

| Sensor | Library | Usage |
|--------|---------|-------|
| Si7021 (temp/humidity) | Adafruit Si7021 | Temperature (°C) and relative humidity (%) |
| MPL3115A2 (pressure) | Adafruit MPL3115A2 | Barometric pressure (hPa) and altitude (m) |
| LTR329 (light) | Adafruit LTR329 and LTR303 | Ambient light (lux) |
| LIS2HH12 (accel) | rhio-LIS2HH12 | Acceleration (g), earthquake detection |

#### Alert Thresholds

| Event | Trigger | Priority | Cooldown |
|-------|---------|----------|----------|
| High temperature | T > 35 °C | 2 | 5 min |
| Low temperature | T < 5 °C | 2 | 5 min |
| High humidity | H > 80 % | 2 | 5 min |
| Low humidity | H < 20 % | 2 | 5 min |
| Low pressure | P < 970 hPa | 2 | 5 min |
| High light | L > 10 000 lux | 2 | 5 min |
| Earthquake | 3+ consecutive accel deviation > 0.2 g from 1 g | 2 | 5 min |
| Heartbeat | Every 30 minutes | −1 | — |

- **Button** on GPIO 37 (PySense P14) — sends a priority-1 notification with a full sensor snapshot on press
- **WS2812B RGB LED** on GPIO 0 (LoPy4 onboard)
- Uses the native ESP32 `HTTPClient` library for HTTPS POST

---

### LoPy4 + PyTrack

Runs on a [Pycom LoPy4](https://pycom.io/) module mounted on the PyTrack v2.0 expansion board. Uses the ESP32 Arduino framework via PlatformIO.

#### Sensors

| Sensor | Library | Usage |
|--------|---------|-------|
| L76GNSS (GPS) | TinyGPSPlus | Position (lat/lon), speed, altitude, satellite count |
| LIS2HH12 (accel) | rhio-LIS2HH12 | Acceleration (g), earthquake detection |

The L76GNSS communicates over I2C (address `0x10`) rather than UART. GPS time is used to sync the ESP32 RTC via `settimeofday()` on first valid fix; heartbeat timestamps switch from uptime to wall-clock UTC once synced.

#### Alert Thresholds

| Event | Trigger | Priority | Cooldown |
|-------|---------|----------|----------|
| High speed | GPS speed > 120 km/h | 2 | 5 min |
| Earthquake | 3+ consecutive accel deviation > 0.2 g from 1 g | 2 | 5 min |
| Heartbeat | Every 30 minutes — includes RSSI, sensor snapshot, and position drift | −1 | — |

- **Button** on GPIO 37 (PyTrack P14) — sends a priority-1 notification with timestamp and sensor snapshot on press
- **WS2812B RGB LED** on GPIO 0 — IDLE state flashes **green** when GPS has a fix, **blue** while waiting for fix
- **Position drift tracking** — the heartbeat reports maximum drift (metres) and sample count since the last heartbeat
- Uses the native ESP32 `HTTPClient` library for HTTPS POST

---

## Shared Behaviour

All devices share the same core architecture:

### LED State Machine

| State | Pattern | Meaning |
|-------|---------|---------|
| CONNECTING | Fast 100 ms toggle | Joining WiFi |
| IDLE | 50 ms flash every 3 s | Connected, waiting |
| SENDING | 3 rapid 50 ms blinks | Sending notification |
| DISCONNECTED | Slow 500 ms toggle | WiFi lost |

### WiFi Management

- Blocking connect at boot (30-second timeout)
- Non-blocking automatic reconnect in the main loop with a 30-second retry cooldown

### Notification Delivery

HTTP POST to `https://notifybridge.mindeon.net/v1/messages/send` with JSON payload:

```json
{
  "api_token": "YOUR_API_TOKEN",
  "user_key": "YOUR_USER_KEY",
  "device_code": "YOUR_DEVICE_CODE",
  "message": "Notification text",
  "priority": 1
}
```

Failed requests are retried with exponential backoff: 2 s initial delay, doubling up to 60 s, for a maximum of 5 attempts.

### Notification Priorities

| Priority | Meaning |
|----------|---------|
| −1 | Background / heartbeat |
| 1 | User-triggered (button press) |
| 2 | System alert (WiFi restored, sensor event) |

## API Reference

Full NotifyBridge API documentation is available at [https://www.mindeon.net/api](https://www.mindeon.net/api).

## License

See [LICENSE](LICENSE) for details.
