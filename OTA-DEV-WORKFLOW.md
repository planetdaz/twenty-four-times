# OTA Dev Workflow Guide

## Overview

The new OTA workflow uses your development machine as the HTTP server instead of the ESP32 master. This enables **parallel downloads** to all 24 pixels simultaneously, reducing update time from ~4-6 minutes to **~15 seconds**.

## Quick Start

### 1. Trigger OTA on Master FIRST

On the master touchscreen:
1. Tap **OTA**
2. Tap **Start Server**
   - Master creates WiFi AP with SSID `TwentyFourTimes`
   - Screen shows connection instructions

### 2. Connect Dev Machine to Master's WiFi

Connect your computer to the WiFi AP:
- **SSID**: `TwentyFourTimes`
- **Password**: `clockupdate`

**Note**: You can keep ethernet connected - Windows will use both simultaneously.

### 3. Start Dev Server

```bash
npm run ota:server
```

This starts a Node.js HTTP server on port 3000. Verify in the terminal that:
- Your WiFi IP is `192.168.4.2` (or note the actual IP)
- The server is listening

### 4. Trigger OTA Update

On the master touchscreen:
- Tap **Send Update**

### 5. Watch the Progress

**Master screen** shows: "UPDATING..." with instructions to check progress elsewhere

**Monitor progress on:**
- **Dev server terminal**: Shows each pixel downloading in real-time
- **Pixel screens**: Show download/flash progress individually

All 24 pixels will:
- Connect to the WiFi AP simultaneously
- Download firmware in parallel from your dev machine
- Flash and reboot (~15-20 seconds total)

When complete, master screen shows "COMPLETE!" - tap "Done" to return to menu.

## Architecture

```
Dev Machine (192.168.4.2:3000)
         ↑
         | HTTP downloads (parallel)
         |
    WiFi AP (192.168.4.1)
         ↑
         | ESP-NOW commands
         |
   Master ESP32 ←--ESP-NOW-→ Pixels (24x)
```

## Building Firmware

If you need to build new pixel firmware first:

```bash
npm run build:pixel
```

Then run the OTA server and follow steps 1-5 above.

## Configuration

Edit `src/master.cpp` to customize the dev server IP/port:

```cpp
// Dev machine OTA server configuration
const char* OTA_DEV_SERVER_IP = "192.168.4.2";
const uint16_t OTA_DEV_SERVER_PORT = 3000;
```

**Note**: The default IP `192.168.4.2` is the typical first DHCP assignment on the master's AP. If your machine gets a different IP, update this constant and rebuild the master firmware.

## Troubleshooting

### Dev machine gets wrong IP

Check the terminal output - it shows all network interfaces. If you get something other than `192.168.4.2`, either:
- Update `OTA_DEV_SERVER_IP` in `master.cpp`
- Or configure a static IP on your WiFi adapter

### Pixels fail to download

Verify:
- Dev machine is connected to `TwentyFourTimes` WiFi
- Node.js server is running (`npm run ota:server`)
- Firmware file exists at `.pio/build/pixel_s3/firmware.bin`
- Firewall isn't blocking port 3000

### Slow downloads

If downloads are still slow:
- Check WiFi signal strength
- Ensure no other devices are using the AP
- Try restarting the master's WiFi AP

## Scripts Reference

| Script | Description |
|--------|-------------|
| `npm run ota:server` | Start dev OTA server (requires firmware.bin to exist) |
| `npm run build:pixel` | Build pixel firmware |
| `npm run build:master` | Build master firmware |
| `npm run upload:pixel` | Upload pixel firmware via USB |
| `npm run upload:master` | Upload master firmware via USB |

## Technical Details

### Why This Works Better

**Old (Sequential)**:
- Master's ESP32 HTTP server: 1 connection at a time
- 24 pixels × ~2 min each = 48 minutes worst-case
- Master's limited HTTP server performance

**New (Parallel)**:
- Node.js HTTP server: handles 24+ concurrent connections
- All pixels download simultaneously
- ~15 seconds total
- Better error visibility and logging

### Network Flow

1. Master creates WiFi AP on channel 1 (same as ESP-NOW)
2. Dev machine joins AP, gets DHCP address
3. Master broadcasts ESP-NOW commands with dev machine's URL
4. All pixels connect to AP and start HTTP downloads
5. Master polls pixels with `CMD_GET_VERSION` to detect reboots
6. When pixels respond with new version, OTA is confirmed

### Firmware Size

The Node.js server reads the actual firmware size from the built `.bin` file. The master sets a placeholder size (`1000000`) when using dev mode - pixels will get the real size from HTTP `Content-Length` headers.
