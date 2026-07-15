![PlatformIO](https://img.shields.io/badge/PlatformIO-orange)
![ESP32](https://img.shields.io/badge/ESP32-blue)
![Arduino](https://img.shields.io/badge/Arduino-00979D)
![Raspberry Pi](https://img.shields.io/badge/Raspberry%20Pi-C51A4A)
![License](https://img.shields.io/github/license/mindeon/NotifyBridge-Devices?cacheSeconds=60)

# NotifyBridge Devices
Turn any microcontroller into a push notification device.

Official PlatformIO examples for integrating **ESP32, Arduino, Raspberry Pi, LoPy4 and other embedded devices** with **NotifyBridge**.

NotifyBridge lets your firmware send **instant push notifications** to Android and iPhone using a simple REST API.

No Firebase SDK.
No mobile development.
No notification backend.

Just an HTTPS request.

## What is NotifyBridge?

NotifyBridge is a cloud notification service built specifically for embedded systems and IoT projects.

Instead of building your own notification infrastructure, your device sends a simple HTTPS request and NotifyBridge delivers the notification directly to your users' phones.

Perfect for:

- 🌡 Temperature monitoring
- 🚨 Motion detection
- 🔋 Battery monitoring
- 💧 Water tank monitoring
- 🌱 Greenhouse automation
- 📍 GPS tracking
- 🛰 Remote sensors
- 🏭 Industrial equipment
- ⚡ Any device capable of making an HTTPS request

## How it works

```
Your Device
(ESP32 / Arduino / Raspberry Pi)
              │
              │ HTTPS POST
              ▼
      NotifyBridge Cloud
              │
              │ Push Notification
              ▼
      Android & iPhone
```

## Useful Links

🌐 Website https://www.mindeon.net

📱 Mobile Apps https://www.mindeon.net/app

📖 REST API https://www.mindeon.net/api

📚 Documentation https://www.mindeon.net/guide

## Supported Devices

Each device now lives in its own repository, so you only need to clone the firmware for the board you're actually using.

| Device | Platform | Board | Notable Features | Repository |
|--------|----------|-------|-----------------|-------------|
| ESP32 | Espressif ESP32 | esp32dev | Button input, hourly heartbeat | [NotifyBridge-ESP32](https://github.com/mindeon/NotifyBridge-ESP32) |
| Arduino MKR WiFi 1010 | Atmel SAM (ARM Cortex-M0+) | mkrwifi1010 | HTTPS via WiFiNINA, button input | [NotifyBridge-MKR1010](https://github.com/mindeon/NotifyBridge-MKR1010) |
| Arduino Nano RP2040 Connect | RP2040 | arduino_nano_connect | IMU, microphone, earthquake detection | [NotifyBridge-NANO-RP2040](https://github.com/mindeon/NotifyBridge-NANO-RP2040) |
| Raspberry Pi Pico W | RP2040 (WiFi) | rpipicow | Lightweight, button input | [NotifyBridge-RPi-PicoW](https://github.com/mindeon/NotifyBridge-RPi-PicoW) |
| Raspberry Pi Pico 2W | RP2350 (WiFi) | rpipico2w | On-chip temperature sensor | [NotifyBridge-RPi-Pico2W](https://github.com/mindeon/NotifyBridge-RPi-Pico2W) |
| LoPy4 + PySense | Espressif ESP32 | lopy4 | Temperature, humidity, pressure, light, accelerometer, earthquake detection | [NotifyBridge-LoPy-PySense](https://github.com/mindeon/NotifyBridge-LoPy-PySense) |
| LoPy4 + PyTrack | Espressif ESP32 | lopy4 | GPS tracking, speed alerts, accelerometer, earthquake detection | [NotifyBridge-LoPy-PyTrack](https://github.com/mindeon/NotifyBridge-LoPy-PyTrack) |

## Prerequisites

- [PlatformIO](https://platformio.org/) (CLI or VSCode extension)
- A [Mindeon NotifyBridge](https://www.mindeon.net/api) account with an API token and user key

## Quick Start

Clone the repository for your board (see the table above), then follow that repository's README to configure credentials and build/upload:

```bash
git clone https://github.com/mindeon/NotifyBridge-ESP32.git
cd NotifyBridge-ESP32
pio run --target upload
```

Every device repo uses the same credential format:

```cpp
#define WIFI_SSID      "YOUR_WIFI_SSID"
#define WIFI_PASSWORD  "YOUR_WIFI_PASSWORD"

#define NB_API_TOKEN   "YOUR_API_TOKEN"
#define NB_USER_KEY    "YOUR_USER_KEY"
#define NB_DEVICE_CODE "YOUR_DEVICE_CODE"
```

> This repository previously bundled all seven device projects together. That
> combined history and source is preserved and browsable at the
> [`v1-monorepo`](https://github.com/mindeon/NotifyBridge-Devices/tree/v1-monorepo)
> tag for reference, but active development now happens in the per-device
> repositories linked above.

## Device Details

Per-device pinouts, sensors, alert thresholds, and firmware notes now live in
each device's own README — see the Repository links in the table above.

All devices share the same core architecture (LED state machine, WiFi
reconnect handling, retry/backoff, and the notification payload format) —
documented identically in each device repo's README.

## API Reference

Full NotifyBridge API documentation is available at [https://www.mindeon.net/api](https://www.mindeon.net/api).

## License

See [LICENSE](LICENSE) for details.
