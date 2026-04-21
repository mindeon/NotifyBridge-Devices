# NotifyBridge Devices

Example device firmware for integrating microcontrollers with the [Mindeon NotifyBridge API](https://www.mindeon.net/api).

This repository contains PlatformIO-based Arduino projects for popular microcontroller platforms, demonstrating how to connect embedded devices to the NotifyBridge notification service.

## Supported Devices

| Device | Platform | Board |
|--------|----------|-------|
| ESP32 | Espressif ESP32 | esp32dev |
| Raspberry Pi Pico W | RP2040 (WiFi) | rpipicow |

## Prerequisites

- [PlatformIO](https://platformio.org/) (CLI or VSCode extension)
- A [Mindeon NotifyBridge](https://www.mindeon.net/api) API key

## Getting Started

1. Clone the repository:
   ```bash
   git clone https://github.com/your-username/NotifyBridge-Devices.git
   cd NotifyBridge-Devices
   ```

2. Open the project folder for your target device in PlatformIO:
   - `NotifyBridge - ESP32/`
   - `NotifyBridge - RPi_PicoW/`

3. Configure your Wi-Fi credentials and NotifyBridge API key in the source code.

4. Build and upload:
   ```bash
   pio run --target upload
   ```

## Project Structure

```
NotifyBridge-Devices/
├── NotifyBridge - ESP32/       # ESP32 firmware
│   ├── platformio.ini
│   └── src/main.cpp
└── NotifyBridge - RPi_PicoW/  # Raspberry Pi Pico W firmware
    ├── platformio.ini
    └── src/main.cpp
```

## API Reference

Full NotifyBridge API documentation is available at [https://www.mindeon.net/api](https://www.mindeon.net/api).

## License

See [LICENSE](LICENSE) for details.
