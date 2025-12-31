# ESP-NOW Setup Guide

This guide explains how to set up ESP-NOW communication between your master controller and pixel displays.

## Overview

The system uses ESP-NOW for wireless communication:
- **Master Controller**: Broadcasts synchronized commands to all pixels
- **Pixel Displays**: Receive commands and animate in unison

## Hardware Setup

You'll need:
- 1x ESP32-C3 (XIAO or similar) for the **master controller**
- 3x ESP32-C3 with GC9A01A displays for the **pixels**

## Flashing Instructions

### Step 1: Flash the Pixel Firmware

For each of your 3 pixel devices:

1. **Set the Pixel ID** in `src/main.cpp`:
   ```cpp
   #define PIXEL_ID 0  // Change to 0, 1, 2 for each device
   ```

2. **Build and upload**:
   ```bash
   pio run -e pixel -t upload
   ```

3. **Monitor the serial output** to verify ESP-NOW initialization:
   ```bash
   pio device monitor -e pixel
   ```
   
   You should see:
   ```
   ========== ESP-NOW INIT ==========
   Pixel ID: 0
   Pixel MAC Address: XX:XX:XX:XX:XX:XX
   WiFi Channel: 1
   ESP-NOW initialized successfully!
   ==================================
   ```

4. **Repeat for all 3 pixels**, changing `PIXEL_ID` to 0, 1, and 2.

### Step 2: Flash the Master Controller

1. **Build and upload** the master firmware:
   ```bash
   pio run -e master -t upload
   ```

2. **Monitor the serial output**:
   ```bash
   pio device monitor -e master
   ```
   
   You should see:
   ```
   ========== MASTER CONTROLLER ==========
   Twenty-Four Times - ESP-NOW Master
   Master MAC Address: XX:XX:XX:XX:XX:XX
   WiFi Channel: 1
   ESP-NOW sender initialized!
   Broadcasting test patterns every 5 seconds...
   ```

## Testing

Once all devices are powered on:

1. **Watch the pixels**: All 3 should animate in perfect synchronization
2. **Check the master serial output**: You'll see which pattern is being sent
3. **Check pixel serial output**: Each pixel will log received commands

### Test Patterns

The master cycles through these patterns every 5 seconds:

1. **All Up**: All hands point to 0° (up)
2. **All Right**: All hands point to 90° (right)
3. **All Down**: All hands point to 180° (down)
4. **All Left**: All hands point to 270° (left)
5. **Staggered**: Each pixel shows different angles

## Troubleshooting

### Pixels not responding to master

1. **Check WiFi channel**: All devices must be on channel 1 (default)
2. **Check serial output**: Look for "ESP-NOW TIMEOUT" messages
3. **Power cycle**: Reset all devices and try again
4. **Distance**: Keep devices within 10-20 meters for testing

### Pixels showing red screen with "!"

- This is the **error state** - pixel hasn't received commands for 10 seconds
- **This is expected behavior** - pixels never run autonomously
- Check that the master is powered on and sending commands
- Once master reconnects, error screen will clear automatically

### Build errors

If you get compilation errors:

1. **Clean the build**:
   ```bash
   pio run -t clean
   ```

2. **Rebuild**:
   ```bash
   pio run -e pixel
   pio run -e master
   ```

## Next Steps

Once you've verified ESP-NOW communication works:

1. **Expand to more pixels**: Change `PIXEL_ID` and flash more devices
2. **Create custom patterns**: Edit `src/master.cpp.example` to add new animations
3. **Add web interface**: Implement HTTP server on master for remote control
4. **Implement clock mode**: Send actual time-based digit patterns

## Architecture Notes

### Packet Structure

Each command packet contains:
- Command type (1 byte)
- Transition type (1 byte)
- Duration in milliseconds (2 bytes)
- Angles for all 24 pixels × 3 hands (72 bytes)
- **Total: 76 bytes** (well under ESP-NOW's 250-byte limit)

### Pixel Behavior

- **Normal mode**: Waits for master commands, executes transitions
- **Error mode**: Shows red screen with "!" if no commands received for 10 seconds
- **No autonomous operation**: Pixels never run on their own - they only respond to master

### Performance

- **Latency**: <10ms from master broadcast to pixel reception
- **Reliability**: ESP-NOW provides automatic retries and acknowledgments
- **Range**: 10-100 meters depending on environment
- **FPS**: Pixels maintain 30 FPS during transitions

## Configuration

### WiFi Channel

Default is channel 1. To change:

1. Edit `lib/ESPNowComm/ESPNowComm.h`:
   ```cpp
   #define ESPNOW_CHANNEL 6  // Change to desired channel
   ```

2. Rebuild and reflash **all devices** (master and pixels)

### Packet Timeout

Default is 10 seconds. To change:

1. Edit `src/main.cpp`:
   ```cpp
   const unsigned long PACKET_TIMEOUT = 5000;  // 5 seconds
   ```

2. Rebuild and reflash **pixel firmware only**

## Serial Commands

Currently, the system doesn't support serial commands, but you can add them to:
- Change pixel ID without reflashing
- Trigger specific patterns manually
- Adjust transition parameters in real-time

This would be a good next step for development!

