# Master Controller on CYD Board

## Overview

The master controller firmware has been updated to run on a **CYD (Cheap Yellow Display)** board instead of an ESP32-C3. This provides a visual interface for monitoring the ESP-NOW broadcast system.

## Hardware: CYD Capacitive Touch Board

**Board:** JC2432W328C (Guition ESP32-2432S028)
- **MCU:** ESP32 (dual-core, 240 MHz)
- **Display:** 2.8" ILI9341 TFT (320x240 pixels)
- **Touch:** CST816S capacitive touch (I2C)
- **Backlight:** GPIO 27

### Pin Configuration

| Function | GPIO |
|----------|------|
| TFT MISO | 12   |
| TFT MOSI | 13   |
| TFT SCLK | 14   |
| TFT CS   | 15   |
| TFT DC   | 2    |
| TFT RST  | -1 (not used) |
| TFT BL   | 27   |
| Touch SDA | 33  |
| Touch SCL | 32  |
| Touch INT | 21  |
| Touch RST | 25  |

## Display Features

The master controller now shows:

1. **Title:** "Twenty-Four Times"
2. **Current Pattern:** Name of the active pattern being broadcast
3. **Duration:** Transition duration in milliseconds
4. **Transition Type:** Linear, Ease In-Out, or Elastic
5. **Status:** Broadcasting confirmation
6. **Countdown:** Time until next pattern
7. **MAC Address:** Device MAC for debugging

### Error Display

If ESP-NOW fails to send a packet, the screen turns red with "SEND FAILED!" message.

## Build Configuration

### PlatformIO Environment: `master`

```ini
[env:master]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
build_src_filter = -<*> +<master.cpp>
board_build.partitions = huge_app.csv
```

### TFT_eSPI Build Flags

The display is configured via build flags (no User_Setup.h needed):

- **Driver:** ILI9341
- **Resolution:** 240x320 (landscape: 320x240)
- **SPI Frequency:** 40 MHz
- **Fonts:** GLCD, Font2, Font4, Font6, Font7, Font8, GFXFF, Smooth Font

### Dependencies

- `bodmer/TFT_eSPI@^2.5.43`
- ESPNowComm (local library)

## Flashing Instructions

### 1. Build the Master Firmware

```bash
platformio run -e master
```

### 2. Upload to CYD Board

```bash
platformio run -e master -t upload
```

### 3. Monitor Serial Output

```bash
platformio device monitor -e master
```

## Operation

1. **Power on** the CYD board
2. **Startup screen** appears: "Twenty-Four Times - Initializing..."
3. **ESP-NOW initialization** - if successful, shows "ESP-NOW Ready!"
4. **Pattern broadcast** begins immediately
5. **Display updates** every 5 seconds with new pattern info

## Test Patterns

The master cycles through 5 test patterns:

1. **All Up** - All hands at 0° (Elastic, 3000ms)
2. **All Right** - All hands at 90° (Ease In-Out, 2000ms)
3. **All Down** - All hands at 180° (Linear, 2500ms)
4. **All Left** - All hands at 270° (Elastic, 3500ms)
5. **Staggered** - Each pixel different angles (Ease In-Out, 4000ms)

## Troubleshooting

### Display not working

- Check TFT_BACKLIGHT is HIGH (GPIO 27)
- Verify SPI pins match your CYD variant
- Try different rotation: `tft.setRotation(1)` or `tft.setRotation(3)`

### ESP-NOW fails

- Ensure WiFi channel is set to 1
- Check that pixel devices are powered on
- Verify MAC address in serial output

### Build errors

```bash
# Clean and rebuild
platformio run -e master -t clean
platformio run -e master
```

## Next Steps

- Add touch interface for manual pattern selection
- Implement WiFi time sync for clock display
- Add brightness control via touch
- Show pixel connection status (which pixels are responding)

