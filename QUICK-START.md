# ESP-NOW Quick Start

## 🚀 Get Your 3 Pixels Synchronized in 5 Minutes

### What You'll Do

1. Flash 3 pixels with different IDs
2. Flash 1 master controller
3. Watch them animate in perfect sync!

---

## Step-by-Step

### 1️⃣ Flash Pixel #1

```bash
# Edit src/main.cpp, set: #define PIXEL_ID 0
pio run -e pixel -t upload
```

### 2️⃣ Flash Pixel #2

```bash
# Edit src/main.cpp, set: #define PIXEL_ID 1
pio run -e pixel -t upload
```

### 3️⃣ Flash Pixel #3

```bash
# Edit src/main.cpp, set: #define PIXEL_ID 2
pio run -e pixel -t upload
```

### 4️⃣ Flash Master

```bash
pio run -e master -t upload
```

### 5️⃣ Power Everything On

All 3 pixels should now animate together! 🎉

---

## What You'll See

The master sends a new pattern every 5 seconds:

| Time | Pattern | Description |
|------|---------|-------------|
| 0s   | All Up | All hands → 0° |
| 5s   | All Right | All hands → 90° |
| 10s  | All Down | All hands → 180° |
| 15s  | All Left | All hands → 270° |
| 20s  | Staggered | Each pixel different |
| 25s  | *(repeats)* | Back to All Up |

---

## Monitoring

### Watch the Master

```bash
pio device monitor -e master
```

You'll see:
```
Sent pattern: All Up (duration: 3000ms)
Sent pattern: All Right (duration: 2000ms)
...
```

### Watch a Pixel

```bash
pio device monitor -e pixel
```

You'll see:
```
ESP-NOW: Received angles [0°, 0°, 0°] duration=3.00s easing=Elastic
ESP-NOW: Received angles [90°, 90°, 90°] duration=2.00s easing=Ease-in-out
...
```

---

## Architecture

```
┌─────────────┐
│   Master    │  Broadcasts commands every 5s
│  ESP32-C3   │  (No display needed)
└──────┬──────┘
       │ ESP-NOW (WiFi Channel 1)
       │ Broadcast to FF:FF:FF:FF:FF:FF
       │
       ├──────────────┬──────────────┐
       │              │              │
       ▼              ▼              ▼
┌──────────┐   ┌──────────┐   ┌──────────┐
│ Pixel #0 │   │ Pixel #1 │   │ Pixel #2 │
│ GC9A01A  │   │ GC9A01A  │   │ GC9A01A  │
│ Display  │   │ Display  │   │ Display  │
└──────────┘   └──────────┘   └──────────┘
```

---

## Key Features

✅ **Sub-10ms latency** - All pixels receive commands nearly instantly
✅ **Error detection** - Pixels show red "!" if master disconnects
✅ **Compact packets** - Only 76 bytes per command
✅ **Smooth transitions** - Multiple easing functions (elastic, ease-in-out, linear)
✅ **No WiFi needed** - ESP-NOW works without router/AP

---

## Troubleshooting

### Pixels not syncing?

1. **Check pixel IDs**: Each must have unique ID (0, 1, 2)
2. **Check WiFi channel**: All devices on channel 1
3. **Power cycle**: Reset all devices
4. **Check distance**: Keep within 10-20m for testing

### Pixels showing red screen with "!"?

- This is the **error state** - no master signal for 10s
- Check master is powered on and sending commands
- Error will clear automatically when master reconnects

### Build errors?

```bash
pio run -t clean
pio run -e pixel
pio run -e master
```

---

## Next Steps

### Expand to More Pixels

Just flash more devices with IDs 3, 4, 5... up to 23!

### Create Custom Patterns

Edit `src/master.cpp.example` and add your own `TestPattern` structs.

### Add Web Control

Implement HTTP server on master to control patterns from your phone.

### Implement Clock Mode

Send actual time-based digit patterns instead of test patterns.

---

## Files Reference

| File | Purpose |
|------|---------|
| `src/main.cpp` | Pixel firmware (receives commands) |
| `src/master.cpp.example` | Master firmware (sends commands) |
| `lib/ESPNowComm/` | Shared ESP-NOW library |
| `platformio.ini` | Build configuration |
| `ESP-NOW-SETUP.md` | Detailed setup guide |

---

**Ready to test? Flash your devices and watch the magic happen! ✨**

