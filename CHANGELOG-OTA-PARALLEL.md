# OTA Parallel Update - Changelog

## Summary

Refactored OTA system from **sequential (one-at-a-time)** to **parallel (all-at-once)** downloads using the dev machine as HTTP server instead of the ESP32 master.

**Result**: Update time reduced from ~4-6 minutes to ~15 seconds.

## Key Changes

### Architecture
- **Before**: Master's ESP32 HTTP server → Sequential downloads (1 pixel at a time)
- **After**: Dev machine Node.js server → Parallel downloads (all 24 pixels simultaneously)

### Workflow Changes

**Old Workflow (Sequential)**:
1. Build firmware
2. Upload firmware to master's LittleFS via `npm run ota:full`
3. Master serves firmware via HTTP
4. Master sends OTA commands one pixel at a time
5. Each pixel waits for previous to complete (~2 min each)
6. Total time: 4-6 minutes

**New Workflow (Parallel)**:
1. Master starts WiFi AP (tap "Start Server")
2. Dev machine connects to AP
3. Dev machine runs `npm run ota:server`
4. Master broadcasts OTA command to ALL pixels at once
5. All pixels download simultaneously from dev machine
6. Total time: ~15 seconds

### Code Changes

#### Removed
- `WebServer.h` and `LittleFS.h` includes
- `otaServer` (WebServer instance)
- `otaServerRunning` flag
- `handleFirmwareRequest()` function
- `USE_DEV_OTA_SERVER` conditional flag
- `otaQueueIndex` (current pixel tracking)
- `otaCurrentPixelStartTime` (per-pixel timing)
- `otaLastActivityTime` (sequential activity tracking)
- `OTA_PIXEL_TIMEOUT` per-pixel timeout
- Firmware file validation in `startOTADiscovery()`
- Sequential queue advancement logic
- LittleFS firmware loading

#### Added
- `otaStartTime` (when parallel broadcast started)
- `otaPixelStartTimes[MAX_PIXELS]` (per-pixel timeout tracking)
- `OTA_TOTAL_TIMEOUT` (120s for all pixels)
- Parallel broadcast logic (send to all pixels in one loop)
- Concurrent pixel monitoring (track all pixels simultaneously)
- Enhanced screen display showing parallel progress
- Better on-screen instructions for correct workflow
- `scripts/ota-server.js` - Node.js HTTP server
- `npm run ota:server` script
- `npm run ota:dev` script (build + server)

#### Modified
- `initOTAServer()`: Removed HTTP server setup, simplified to WiFi AP only
- `stopOTAServer()`: Removed HTTP server cleanup
- `sendOTAStartToPixel()`: Always uses dev server URL
- `drawOTAScreen()`:
  - OTA_READY: Shows correct workflow instructions
  - OTA_IN_PROGRESS: Shows parallel progress counts (not "current pixel")
- `handleOTATouch()`: Updated button coordinates
- Main loop OTA logic: Broadcasts to all pixels, monitors all concurrently
- Version response handler: Checks all pixels in queue, not just current

### Screen Display Changes

**OTA_READY Screen**:
```
WiFi AP Ready!
SSID: TwentyFourTimes
Password: clockupdate

On dev machine:
1. Connect to above WiFi
2. Run: npm run ota:server
3. Tap 'Send Update' below

[Send Update]
[Back]
```

**OTA_IN_PROGRESS Screen**:
```
Updating In Parallel

Completed: 18 / 24
In Progress: 6
Success: 18

All pixels downloading from
dev machine simultaneously...

[Done]
```

### Serial Output Enhancements

When starting OTA server:
```
=== OTA SETUP INSTRUCTIONS ===
1. Connect dev machine to WiFi AP:
   SSID: TwentyFourTimes
   Password: clockupdate
2. Run on dev machine: npm run ota:server
3. Tap 'Send Update' on master screen
===============================
```

When OTA completes:
```
===== OTA COMPLETE =====
Total pixels: 24
Successful: 24
Failed: 0
Total time: 16 seconds
========================
```

### Network Configuration

- WiFi AP max connections increased from 4 to 30 (handles 24 pixels + dev machine + headroom)
- Dev server default: `192.168.4.2:3000`
- ESP-NOW remains active during OTA (AP+STA mode)

### Breaking Changes

- Old workflow (`npm run ota:full`) no longer works
- Master no longer stores firmware in LittleFS
- Must run dev server during OTA updates
- Cannot update pixels without dev machine present

### Migration Guide

1. **Update master firmware**: Build and upload new master firmware
2. **Remove old workflow**: Delete `scripts/prepare-ota.js` (if no longer needed)
3. **Use new workflow**: Follow steps in OTA-DEV-WORKFLOW.md

### Testing Checklist

- [ ] Master creates WiFi AP on channel 1
- [ ] Dev machine gets IP `192.168.4.2` (or configured IP)
- [ ] `npm run ota:server` starts successfully
- [ ] Master discovers all online pixels
- [ ] All pixels receive START command simultaneously
- [ ] Pixels download in parallel (~15s)
- [ ] All pixels reboot and respond with new version
- [ ] Master displays correct progress counts
- [ ] Serial output shows parallel operation
- [ ] Total OTA time < 30 seconds

### Known Issues

None currently.

### Future Improvements

- Auto-detect dev machine IP (avoid hardcoding)
- Add version checking (skip pixels already updated)
- Add retry logic for failed pixels
- Show per-pixel status grid on screen
- Estimate time remaining based on progress
- Support firmware compression
