# NotifyBridge Developer Guide

**Send push notifications from any device to your users' iPhones.**

NotifyBridge lets hardware and IoT developers (ESP32, Arduino, Raspberry Pi, or any HTTP-capable device) send real-time push notifications to end users through a simple REST API.

---

## How It Works

There are two roles in NotifyBridge:

| Role | Who | What they do |
|---|---|---|
| **Developer** (Studio mode) | You, the firmware/hardware builder | Create apps, register devices, send notifications |
| **End User** (Receiver mode) | Your customer | Claims a device, receives push notifications |

The flow is straightforward:

```
You (Developer)                          Your Customer (End User)
─────────────────                        ────────────────────────
1. Create an App in Studio mode
2. Add a Device to the app
3. Get the Device Code (e.g. MS-7F3K-92LM)
4. Program it into your hardware
   or print it as a QR code
5. Ship/give the hardware          →     6. Opens NotifyBridge in Receiver mode
                                         7. Scans QR code or enters Device Code
                                         8. Device is now linked to their phone
9. Send a push from your hardware  →     10. Notification appears on their phone
```

---

## Step 1: Set Up Your Developer Account

1. Download **NotifyBridge** from the App Store.
2. Open the app and choose **Studio** mode.
3. Your account is created automatically. A default app called "NotifyBridge" is ready to use.

You'll find your **API Token** and **User Key** in the app. You need both to send notifications.

---

## Step 2: Create Devices

Each physical unit you build (a sensor, monitor, controller, etc.) needs its own **device** in NotifyBridge.

1. Open your app in Studio mode.
2. Tap on your app name to see its details.
3. Scroll to the **Devices** section and tap **Add Device**.
4. Give it a name (e.g. "Greenhouse Sensor #42").
5. A **Device Code** is generated (format: `XX-XXXX-XXXX`) along with a **QR code**.

Save the Device Code. You'll program it into your hardware or include it in your product packaging so your end user can activate the device.

---

## Step 3: Send Notifications from Your Hardware

Once a user has claimed a device, you can send push notifications with a single HTTP POST request.

### API Endpoint

```
POST https://notifybridge.mindeon.net/v1/messages/send
Content-Type: application/json
```

### Request Body

```json
{
  "api_token": "YOUR_API_TOKEN",
  "user_key": "YOUR_USER_KEY",
  "message": "Temperature exceeded 40°C",
  "title": "Heat Warning",
  "priority": 1,
  "source": "Greenhouse Sensor"
}
```

### Fields

| Field | Required | Description |
|---|---|---|
| `api_token` | Yes | Your app's API token (found in app details) |
| `user_key` | Yes | Your account key (found in Settings) |
| `message` | Yes | The notification body text |
| `title` | No | Notification title (defaults to "Notification") |
| `priority` | No | `-1` Low, `0` Normal (default), `1` High, `2` Emergency |
| `source` | No | Label shown to the user (e.g. your device or app name) |
| `url` | No | A URL the user can open from the notification |

### Example: ESP32 (Arduino)

```cpp
#include <WiFi.h>
#include <HTTPClient.h>

void sendNotification(String message) {
    HTTPClient http;
    http.begin("https://notifybridge.mindeon.net/v1/messages/send");
    http.addHeader("Content-Type", "application/json");

    String payload = "{";
    payload += "\"api_token\":\"YOUR_API_TOKEN\",";
    payload += "\"user_key\":\"YOUR_USER_KEY\",";
    payload += "\"message\":\"" + message + "\",";
    payload += "\"title\":\"My Device\",";
    payload += "\"priority\":1";
    payload += "}";

    int httpCode = http.POST(payload);
    http.end();
}
```

### Example: cURL

```bash
curl -X POST https://notifybridge.mindeon.net/v1/messages/send \
  -H "Content-Type: application/json" \
  -d '{
    "api_token": "YOUR_API_TOKEN",
    "user_key": "YOUR_USER_KEY",
    "message": "Hello from my device",
    "title": "Device Alert",
    "priority": 0
  }'
```

### Example: Python

```python
import requests

requests.post("https://notifybridge.mindeon.net/v1/messages/send", json={
    "api_token": "YOUR_API_TOKEN",
    "user_key": "YOUR_USER_KEY",
    "message": "Motion detected in Zone 3",
    "title": "Security Alert",
    "priority": 2
})
```

---

## Step 4: Your End User Activates the Device

This is what your customer does after receiving the hardware:

1. They download **NotifyBridge** from the App Store.
2. They open the app and choose **Receiver** mode.
3. They tap **+** to add a device.
4. They **scan the QR code** on the hardware or **type the Device Code** manually.
5. Done. They now receive every notification you send.

The end user does not need an account, a subscription, or any technical knowledge. They just scan and go.

---

## Multiple Devices, One Account

You can create as many devices as you need under a single app:

- **1 app** = 1 product line or project
- **Many devices per app** = one for each unit you ship

All devices under the same app share the same API Token. The Device Code is what links a specific physical unit to a specific end user's phone.

---

## Priority Levels

Use priority to control how the notification appears on the user's phone:

| Priority | Value | Behavior |
|---|---|---|
| Low | `-1` | Standard notification, lower visual emphasis |
| Normal | `0` | Standard notification (default) |
| High | `1` | Highlighted notification |
| Emergency | `2` | Critical alert sound, bypasses Do Not Disturb |

---

## Push Credits

Each notification you send costs one push credit. New accounts come with free credits to get started. You can purchase additional credits directly in the app under **Settings > Push Credits**.

---

## Quick Reference

| What | Where to find it |
|---|---|
| API Token | Studio > tap your app > API Token section |
| User Key | Settings > Account |
| Device Code | Studio > tap your app > Devices > tap a device |
| Send endpoint | `POST https://notifybridge.mindeon.net/v1/messages/send` |
