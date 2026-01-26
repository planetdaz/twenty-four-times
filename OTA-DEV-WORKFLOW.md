# OTA Dev Workflow Guide

## Overview

The new OTA workflow uses your development machine as the HTTP server instead of the ESP32 master. This enables **parallel downloads** to all 24 pixels simultaneously, reducing update time from ~4-6 minutes to **~15 seconds**.

## Quick Start

### 1. Build and Start Dev Server

```bash
npm run ota:dev
```

This will:
- Build the pixel firmware (`pixel_s3` target)
- Start a Node.js HTTP server on port 3000
- Display connection instructions

### 2. Connect to Master's WiFi

When you see the server output, connect your computer to the WiFi AP created by the master:

- **SSID**: `TwentyFourTimes`
- **Password**: `clockupdate`

Your computer should get IP `192.168.4.2` (verify in the terminal output).

### 3. Trigger OTA on Master

On the master touchscreen:
1. Tap **OTA**
2. Tap **Start Server** (master creates WiFi AP)
3. Tap **Send Update** (master sends OTA command to all pixels)

### 4. Watch the Magic

All 24 pixels will:
- Connect to the WiFi AP
- Download firmware simultaneously from your dev machine
- Flash and reboot (~15 seconds total)

The Node.js terminal will show each download in real-time.

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

## Configuration

Edit `src/master.cpp` to customize:

```cpp
// Dev machine OTA server configuration
const char* OTA_DEV_SERVER_IP = "192.168.4.2";
const uint16_t OTA_DEV_SERVER_PORT = 3000;
const bool USE_DEV_OTA_SERVER = true;  // Set false for legacy mode
```

## Legacy Mode (Master as Server)

To revert to the old sequential workflow where master serves firmware:

1. Set `USE_DEV_OTA_SERVER = false` in `src/master.cpp`
2. Build and upload master firmware
3. Use `npm run ota:full` to upload firmware to master's LittleFS
4. Trigger OTA normally (no dev machine needed)

**Note**: Legacy mode updates one pixel at a time (~2 min each).

## Troubleshooting

### Dev machine gets wrong IP

Check the terminal output - it shows all network interfaces. If you get something other than `192.168.4.2`, either:
- Update `OTA_DEV_SERVER_IP` in `master.cpp`
- Or configure a static IP on your WiFi adapter

### Pixels fail to download

Verify:
- Dev machine is connected to `TwentyFourTimes` WiFi
- Node.js server is running (`npm run ota:dev`)
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
| `npm run ota:dev` | Build firmware + start dev server (recommended) |
| `npm run ota:server` | Just start dev server (firmware must exist) |
| `npm run build:pixel` | Build pixel firmware only |
| `npm run ota:full` | Legacy: build + upload to master's LittleFS |

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
