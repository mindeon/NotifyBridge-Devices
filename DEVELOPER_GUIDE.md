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

Each physical unit you build (a sensor, monitor, controller, etc.) needs its own **device** in NotifyBridge. A device is a virtual entity that connects your hardware to your end users.

1. Open your app in Studio mode.
2. Tap on your app name to see its details.
3. Scroll to the **Devices** section and tap **Add Device**.
4. Give it a name (e.g. "Greenhouse Sensor #42").
5. A **Device Code** is generated (format: `MS-XXXX-XXXX`) along with a **QR code**.

Save the Device Code. You'll program it into your hardware or include it in your product packaging so your end user can claim the device on their phone.

---

## Step 3: Send Notifications from Your Hardware

Once a user has claimed a device, you can send push notifications with a single HTTP POST request.

> **Tip:** In Studio mode, tap any device to see a ready-to-use cURL command with your credentials pre-filled. Just copy and paste!

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
  "device_code": "YOUR_DEVICE_CODE",
  "message": "Temperature exceeded 40°C",
  "priority": 1
}
```

### Fields

| Field | Required | Description |
|---|---|---|
| `api_token` | Yes | Your app's API token (found in app details) |
| `user_key` | Yes | Your account key (found in Settings) |
| `device_code` | Yes* | The device's code (e.g. "MS-7F3K-92LM"). You can also use `relay_id` instead. |
| `relay_id` | Yes* | Alternative to `device_code`. The internal device ID (e.g. "relay_abc123"). |
| `message` | Yes | The notification body text |
| `priority` | No | `-1` Low, `0` Normal (default), `1` High, `2` Emergency |
| `device_name` | No | Target a specific subscriber device by name (useful for multi-device households) |

*Either `device_code` or `relay_id` is required.

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
    payload += "\"device_code\":\"YOUR_DEVICE_CODE\",";
    payload += "\"message\":\"" + message + "\",";
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
    "device_code": "YOUR_DEVICE_CODE",
    "message": "Hello from my device",
    "priority": 1
  }'
```

### Example: Python

```python
import requests

requests.post("https://notifybridge.mindeon.net/v1/messages/send", json={
    "api_token": "YOUR_API_TOKEN",
    "user_key": "YOUR_USER_KEY",
    "device_code": "YOUR_DEVICE_CODE",
    "message": "Motion detected in Zone 3",
    "priority": 1
})
```

---

## Push Limits

NotifyBridge includes a monthly push notification limit to prevent abuse and ensure fair usage.

- **Monthly Limit**: 3,000 pushes per month (per account)
- **Billing**: When a device is claimed by a subscriber, push usage is counted against the **subscriber's** limit, not the developer's
- **Credits**: Developers can purchase additional push credits through the app if needed

---

## Step 4: Understanding the Claiming Process

The claiming process connects your hardware to an end user's iPhone. Here's how it works behind the scenes:

### Developer Side (You)

When you create a device in Studio mode:

1. **Device Created**: The NotifyBridge backend generates:
   - A unique **Device Code** (e.g., `MS-7F3K-92LM`)
   - A unique **Relay ID** (internal identifier)
   - Status: `UNCLAIMED`
   
2. **Your User Key Stored**: The device is associated with **your** User Key (the developer/owner)

3. **API Token Linked**: The device is linked to your app's API Token

At this point, the device exists but isn't connected to any subscriber's phone. Think of it as a "virtual mailbox" waiting to be claimed.

### What You Do Next

You have two options:

**Option A: Embed the Device Code in your hardware**
```cpp
// In your ESP32/Arduino firmware
const char* DEVICE_CODE = "MS-7F3K-92LM";  // Hardcode this
```

**Option B: Display it for manual entry**
- Print the QR code on packaging
- Show it on your device's screen
- Include it in setup documentation

### Subscriber Side (Your Customer)

When your customer receives the hardware:

1. They download **NotifyBridge** from the App Store
2. They open the app and choose **Receiver** mode
3. On first launch, they're assigned a **new User Key** automatically
4. They tap **+** to claim a device
5. They scan the QR code or enter the Device Code manually

### What Happens During Claiming

When they submit the Device Code, here's what happens on the backend:

1. **Backend validates** the Device Code exists and is unclaimed
2. **Backend updates the device**:
   - Status changes: `UNCLAIMED` → `CLAIMED`
   - Stores the subscriber's User Key in `claimed_by_user_key`
   - Stores the subscriber's Device ID in `claimed_by_device_id`
3. **Backend returns** the full device details to the subscriber's app
4. **Subscriber's app** adds the device to their "Devices" list

### Critical Concept: Two User Keys

This is the key to understanding NotifyBridge's architecture:

| User Key | Owner | Purpose |
|----------|-------|---------|
| **Developer User Key** | You | Authenticates API requests to send notifications |
| **Subscriber User Key** | Your customer | Routes notifications to their specific device |

When you send a notification, you include:
- **Your** User Key (proves you own the app)
- **Your** API Token (proves you own the device)
- The **Device Code** (specifies which device)

The backend uses the Device Code to look up which subscriber claimed it, then routes the notification to **their** phone using **their** User Key.

### After Claiming: Sending Notifications

Once claimed, your hardware can send notifications:

```bash
curl -X POST https://notifybridge.mindeon.net/v1/messages/send \
  -H "Content-Type: application/json" \
  -d '{
    "api_token": "YOUR_API_TOKEN",       # Proves you own the device
    "user_key": "YOUR_USER_KEY",         # Proves you're the developer
    "device_code": "MS-7F3K-92LM",       # Specifies the device
    "message": "Temperature alert!"
  }'
```

The backend:
1. Validates your API Token and User Key
2. Looks up which subscriber claimed `MS-7F3K-92LM`
3. Routes the notification to that subscriber's phone via APNs

### Multiple Customers, Same Hardware Design

This architecture lets you manufacture many identical units:

```
Your Factory:
┌─────────────────────────────────────┐
│ Unit #1: Device Code AB-1234-CDEF   │  →  Customer A's iPhone
│ Unit #2: Device Code AB-5678-GHIJ   │  →  Customer B's iPhone  
│ Unit #3: Device Code AB-9012-KLMN   │  →  Customer C's iPhone
└─────────────────────────────────────┘
         All use same:
         - API Token
         - Your User Key
         - Same firmware code
```

Each unit has a **unique Device Code**, but they all authenticate with **your** credentials. The Device Code determines **which customer** gets the notification.

### One Device, Multiple Subscribers (Groups)

**Important:** A device can send notifications to **multiple people** at once using the **group** feature.

When a subscriber claims a device, they get a **User Key**. They can share this key with others (family members, roommates, team members) who also want to receive notifications from the same hardware.

**Example: Smart Doorbell in a Household**

```
Your Product: Smart Doorbell
Device Code: DB-1234-ABCD
    ↓
Customer A claims it
    ↓
Customer A's User Key: USER-KEY-ABC
    ↓
Customer A shares their User Key with Customer B, C, D
    ↓
All 4 people receive doorbell notifications!
```

**How Subscribers Join a Group:**

1. **Customer A** (first person) claims the device normally
2. Customer A opens Devices tab → Menu (⋮) → **"Invite to Group"**
3. A QR code appears with Customer A's User Key
4. **Customer B** opens NotifyBridge → Receiver mode
5. Customer B taps **+** → **"Join a Group"**
6. Customer B scans the QR code (or manually enters the key)
7. Customer B's phone is now linked to the same User Key as Customer A
8. Both receive notifications when the doorbell (or any other device claimed by that User Key) sends a push

**Important Notes:**

- All group members share the **same User Key**
- All group members see **all devices** claimed under that User Key
- Any group member can claim new devices, and everyone in the group will see them
- This is perfect for:
  - **Households**: Everyone gets home security alerts
  - **Teams**: All members receive equipment monitoring alerts  
  - **Families**: Shared device notifications (elderly care monitors, etc.)

**As a Developer:**

You don't need to do anything special to support groups. Your hardware sends to one Device Code, and NotifyBridge automatically delivers to all devices in that User Key's group.

```cpp
// Your firmware doesn't change - just send to the Device Code
sendNotification("MS-7F3K-92LM", "Motion detected!");
// NotifyBridge routes it to ALL devices in the group automatically
```

**Behind the Scenes:**

```
Hardware sends:                       Backend:                    Subscribers:
─────────────                         ────────                    ────────────
POST /v1/messages/send                Lookup: Who claimed         Customer A's iPhone 🔔
{                                     MS-7F3K-92LM?               Customer B's iPhone 🔔
  "device_code": "MS-7F3K-92LM"      Answer: USER-KEY-ABC        Customer C's iPhone 🔔
}                                         ↓                       Customer D's iPhone 🔔
                                     Find ALL devices                (all in same group)
                                     registered with
                                     USER-KEY-ABC
                                          ↓
                                     Send to device_id:
                                     - iPhone-001 (Customer A)
                                     - iPhone-002 (Customer B)
                                     - iPhone-003 (Customer C)
                                     - iPhone-004 (Customer D)
```

### The Customer Experience

From your customer's perspective, claiming is just:

1. Open NotifyBridge app
2. Tap **Receiver** mode
3. Scan QR code
4. Done! Notifications arrive

They never see API tokens, User Keys, or any technical details. They just scan and go.

---

## Step 5: Architecture Diagram

Here's a visual representation of the complete flow:

```
BEFORE CLAIMING
────────────────

Developer (You)                    NotifyBridge Backend                End User (Customer)
─────────────                      ────────────────────                ──────────────────
User Key: DEV-123                                                      User Key: SUB-789
API Token: TOKEN-ABC                                                   (not yet connected)
                                                                       
Creates Device                                                          Hardware arrives
    ↓                                                                      ↓
Device Code: MS-7F3K              [Devices Table]                      Opens app
    ↓                             relay_id: R1                         Chooses "Receiver"
Hardcode in                       device_code: MS-7F3K                     ↓
ESP32 firmware                    status: UNCLAIMED                   Scans QR code
    ↓                             user_key: DEV-123                      ↓
Ships to customer    ────────────>api_token: TOKEN-ABC                Taps "Claim"
                                  claimed_by: NULL                         ↓
                                                                       (sends MS-7F3K)


AFTER CLAIMING
──────────────

Developer (You)                    NotifyBridge Backend                End User (Customer)
─────────────                      ────────────────────                ──────────────────
                                  [Devices Table]
                                  relay_id: R1                        Device appears in
                                  device_code: MS-7F3K                "Devices" tab
                                  status: CLAIMED          ←──────────    ✓
                                  user_key: DEV-123                  
Hardware sends:                   api_token: TOKEN-ABC                Ready to receive
POST /v1/messages/send            claimed_by: SUB-789 ───────────────>notifications
{                                 device_id: iPhone-456
  "api_token": "TOKEN-ABC",                   ↓
  "user_key": "DEV-123",                 Lookup: Who claimed
  "device_code": "MS-7F3K",               MS-7F3K?
  "message": "Alert!"                    Answer: SUB-789
}                                            ↓
    ↓                                   Route to phone
    └──────────────────────────────────>iPhone-456 via APNs
                                             ↓
                                        🔔 Push notification
                                           arrives on iPhone
```

### Key Points

1. **Developer credentials** (`api_token` + `user_key`) prove ownership and authorize sending
2. **Device Code** identifies the specific relay
3. **Backend** maintains the mapping: `Device Code` → `Subscriber's User Key` → `Subscriber's Device`
4. **One relay, one subscriber** at a time (but can be unclaimed and re-claimed)
5. **No direct connection** between your hardware and the subscriber's phone — everything routes through NotifyBridge

---

## Step 6: Testing the Flow

Before shipping to customers, test the complete flow:

### Test in Studio Mode

1. Create a relay in Studio mode
2. Note the Device Code
3. In the same app, tap **Claim Relay** (for testing only)
4. Enter the Device Code
5. The relay status changes to "Claimed"

### Test Sending

Use the pre-filled cURL command from the relay detail view:

1. Tap the relay in Studio mode
2. Scroll to "Usage Example"
3. Tap "Copy Example"
4. Paste into Terminal and run
5. You should receive a notification (if on a real device)

**Note:** Push notifications don't work in the Simulator — you need a physical iPhone.

### Test as End User

1. Switch to Receiver mode (Settings → Change Role)
2. The claimed relay should appear in your Devices list
3. Tap it to see details
4. Send another notification — it should arrive

### Test Unclaiming

1. In Receiver mode, tap the relay
2. Scroll down and tap **Unclaim Device**
3. Status changes back to "Unclaimed"
4. Try sending a notification — it should fail (relay not claimed)

---

## Step 7: Multiple Relays, One Account

You can create as many relays as you need under a single app:

- **1 app** = 1 product line or project
- **Many relays per app** = one for each unit you ship

All relays under the same app share the same API Token. The Device Code is what links a specific physical unit to a specific end user's phone.

---

## Priority Levels

Use priority to control how the notification appears on the user's phone:

| Priority | Value | Behavior |
|---|---|---|
| Low | `-1` | Silent delivery, no sound or banner interruption |
| Normal | `0` | Standard notification (default) |
| High | `1` | Standard sound, cuts through Focus filters |
| Emergency | `2` | Critical alert sound, bypasses Do Not Disturb and silent mode |

---

## Push Credits

Each notification you send costs one push credit. New accounts come with free credits to get started. You can purchase additional credits directly in the app under **Settings > Push Credits**.

---

## Advanced Features

### Groups: Multiple Subscribers for One Relay

One of NotifyBridge's most powerful features is **Groups** — the ability for multiple people to receive notifications from the same relay.

#### Use Cases

- **Smart Home Devices**: All household members get alerts
- **Business Equipment**: Entire team receives monitoring notifications
- **Senior Care Devices**: Family members all receive health alerts
- **Security Systems**: Multiple people get intrusion alerts
- **Shared Spaces**: Roommates all receive door sensor notifications

#### How It Works

When someone claims a relay, they get a **User Key**. This key can be shared with others who also want to receive notifications from that relay (and any other relays claimed under that key).

**Step-by-Step Example:**

1. **Alice** buys your smart doorbell and claims it
   - Alice gets User Key: `a1b2c3d4e5f6...`
   - Doorbell relay is claimed under this key

2. **Alice shares with Bob** (her spouse)
   - Alice: Devices → Menu → "Invite to Group" → Shows QR code
   - Bob: Opens NotifyBridge → + → "Join a Group" → Scans QR
   - Bob's device is now linked to Alice's User Key

3. **Both receive notifications**
   - Your doorbell sends a notification
   - Backend sees the relay is claimed by User Key `a1b2c3d4e5f6...`
   - Backend finds ALL devices registered with that key
   - Push sent to Alice's iPhone AND Bob's iPhone

#### Important Group Characteristics

| Aspect | Behavior |
|--------|----------|
| **Shared User Key** | All group members use the same User Key |
| **Shared Relay List** | All members see all relays claimed by the group |
| **Who Can Claim** | Any group member can claim new relays |
| **Notification Delivery** | One notification sent = delivered to ALL group members |
| **Group Size** | No technical limit (but rate limits apply per relay) |
| **Leave Group** | Member can generate a new User Key and re-register |

#### As a Developer: No Changes Required

The best part? **Your firmware doesn't change.** You just send to the Device Code:

```cpp
// Same code works for 1 person or 100 people in a group
sendNotification("MS-7F3K-92LM", "Alert!");
```

NotifyBridge handles the routing automatically. One API call delivers to everyone in the group.

#### Group vs Multiple Relays

Don't confuse **Groups** with **Multiple Relays**:

| Scenario | What It Is | Use Case |
|----------|-----------|----------|
| **One Relay, Multiple Subscribers (Group)** | Multiple people receive notifications from the same device | Family members all get doorbell alerts |
| **Multiple Relays, One Subscriber** | One person receives notifications from multiple devices | One person monitors 5 different sensors |
| **Multiple Relays, Multiple Subscribers** | Different groups claim different devices | Customer A has doorbell, Customer B has different doorbell |

#### Inviting to a Group (Subscriber Side)

Your end users can invite others from within the app:

1. Open **Devices** tab in Receiver mode
2. Tap the **Menu (⋮)** button
3. Select **"Invite to Group"**
4. Share the QR code or send the key via text/email

The invited person:

1. Opens **NotifyBridge** app
2. Chooses **Receiver** mode
3. Taps **+** button
4. Selects **"Join a Group"**
5. Scans QR code or pastes the key
6. Done! They're in the group

#### Technical Details: How Groups Work

```
Backend Database:
─────────────────

relay_table:
  relay_id: R1
  device_code: MS-7F3K-92LM
  claimed_by_user_key: GROUP-KEY-123  ← Shared among group
  
device_registrations:
  device_id: iPhone-A    user_key: GROUP-KEY-123  fcm_token: ...
  device_id: iPhone-B    user_key: GROUP-KEY-123  fcm_token: ...
  device_id: iPhone-C    user_key: GROUP-KEY-123  fcm_token: ...

When notification arrives for MS-7F3K-92LM:
  1. Lookup relay → claimed_by_user_key = GROUP-KEY-123
  2. Find all devices with user_key = GROUP-KEY-123
  3. Send push to fcm_token for iPhone-A, iPhone-B, iPhone-C
```

#### Security Considerations

- **User Keys are sensitive**: They grant access to all relays in the group
- **Share only with trusted people**: Anyone with the key can see all devices
- **No revocation**: If someone joins, they stay in until they leave voluntarily
- **Consider this in your product design**: If you're building a product for shared use, groups are great. If it's personal, inform users to keep keys private.

### QR Code Display

Every relay comes with a built-in QR code that encodes the Device Code. You can:

1. Display the QR code in the relay detail view by tapping **Show QR Code**
2. Share the QR code image with your customer
3. Print the QR code on your hardware or packaging

The QR code makes claiming much easier for your end users — they just scan it instead of typing the code manually.

### Unclaim and Re-Claim

If an end user needs to transfer a relay to a different phone, they can:

1. Open the relay in the Devices tab
2. Tap **Unclaim Device**
3. The relay returns to "Unclaimed" status
4. Another user can now claim it with the same Device Code

As a developer, you can also unclaim relays from Studio mode, but only if they haven't been claimed by a subscriber.

### Delete Relays

If you no longer need a relay (e.g., a hardware unit was scrapped), you can delete it from Studio mode:

1. Open your app in Studio mode
2. Tap the relay you want to remove
3. Scroll down and tap **Delete Device**

**Note:** You can only delete relays that are unclaimed. If a subscriber has claimed the relay, they must unclaim it first.

---

## Quick Reference

| What | Where to find it |
|---|---|
| API Token | Studio > tap your app > API Token section |
| User Key | Settings > Account |
| Device Code | Studio > tap your app > Relays > tap a relay > Copy Device Code |
| Send endpoint | `POST https://notifybridge.mindeon.net/v1/messages/send` |
| Invite to Group | Receiver mode > Devices > Menu (⋮) > "Invite to Group" |
| Join a Group | Receiver mode > + button > "Join a Group" |
---

## Common Scenarios

### Scenario 1: Single Customer, Single Device
**Example:** One person has a temperature sensor

- Developer creates 1 relay
- Customer claims it  
- Developer's hardware sends notifications
- Customer receives them

**No special setup needed.**

### Scenario 2: Single Customer, Multiple Devices
**Example:** One person has 5 different sensors

- Developer creates 5 relays (one per sensor)
- Customer claims all 5 relays (same User Key for all)
- Each sensor sends to its own Device Code
- Customer receives notifications from all sensors in one app

**Customer just scans QR codes for each device.**

### Scenario 3: Multiple Customers, Same Device Type
**Example:** You sell 100 identical smart doorbells

- Developer creates 100 relays (one per doorbell)
- Each doorbell has unique Device Code hardcoded
- Customer A claims Doorbell #1
- Customer B claims Doorbell #2
- Customer C claims Doorbell #3
- All use same firmware, same API Token

**Each customer only sees their own doorbell.**

### Scenario 4: Multiple People, Same Device (Group)
**Example:** Family wants everyone to get doorbell alerts

- Developer creates 1 relay
- Mom claims the doorbell
- Mom shares her User Key with Dad, Kids
- Dad/Kids join the group
- Doorbell sends 1 notification
- Everyone in the family receives it

**Perfect for shared devices.**

### Scenario 5: Multiple People, Multiple Devices (Group)
**Example:** Business team monitors 3 servers

- Developer creates 3 relays (Server A, B, C)
- Team Lead claims all 3 servers (same User Key)
- Team Lead shares User Key with team
- Team members join the group
- All 3 servers send to their Device Codes
- All team members receive alerts from all 3 servers

**Ideal for team/workplace scenarios.**

---

## Troubleshooting

### "Relay not claimed" error when sending
- The relay must be claimed before it can receive notifications
- Have a subscriber claim it first
- Or claim it yourself in Studio mode for testing

### Notification doesn't arrive
- Check you're testing on a **real iPhone** (not Simulator)
- Verify the Device Code is correct
- Check push credits haven't run out
- Make sure notification permissions are enabled on the iPhone

### Wrong person receives notifications
- Double-check the Device Code in your firmware
- Each relay has a unique code
- Make sure you didn't mix up codes between units

### Group member isn't receiving notifications
- Verify they successfully joined the group (check User Key matches)
- Ensure their device is registered (re-open the app to trigger registration)
- Check their notification permissions

### Can't delete a relay
- Relays must be unclaimed before deletion
- If a subscriber has claimed it, they must unclaim it first
- You can unclaim from Studio mode if it's your own claim

