# Flashing Guide

Quick reference for building and flashing firmware to pixel nodes and master controller.

---

## Pixel Nodes

The project supports two ESP32 variants for pixel nodes. Choose the appropriate environment based on your hardware.

### ESP32-C3 (XIAO)

**Hardware:** Seeed Studio XIAO ESP32-C3  
**Performance:** ~30 FPS  
**SPI Mode:** Software (bit-banging)

```bash
# Build and upload
pio run -e pixel_c3 --target upload

# Monitor serial output
pio device monitor -e pixel_c3
```

### ESP32-S3 (Recommended)

**Hardware:** ESP32-S3 Mini / ESP32-S3-Zero  
**Performance:** ~50-60 FPS  
**SPI Mode:** Hardware SPI with DMA

```bash
# Build and upload
pio run -e pixel_s3 --target upload

# Monitor serial output
pio device monitor -e pixel_s3
```

**Note:** The S3 uses hardware SPI on GPIO13/14/15, which provides 2× faster display updates compared to C3.

---

## Master Controller

**Hardware:** ESP32 CYD (Cheap Yellow Display)  
**Display:** 320×240 ST7789 with capacitive touch

```bash
# Build and upload
pio run -e master --target upload

# Monitor serial output
pio device monitor -e master
```

---

## Setting Pixel IDs

Each pixel needs a unique ID (0-23). Currently this is hardcoded in `src/main.cpp`:

```cpp
#define PIXEL_ID 2  // Change this for each device
```

**Workflow for 24 pixels:**

1. Edit `PIXEL_ID` in `src/main.cpp`
2. Flash the device
3. Label the physical device with its ID
4. Repeat for next pixel

**Future:** Pixel IDs will be stored in NVS (non-volatile storage) and set via provisioning mode.

---

## Troubleshooting

### Board Not Detected

**ESP32-C3:**
- Hold BOOT button while plugging in USB
- Release after 2 seconds
- Try upload again

**ESP32-S3:**
- Press and hold BOOT button
- Press and release RESET button
- Release BOOT button
- Try upload again

### Wrong Board Selected

If you see compilation errors about missing pins or functions:

```
error: 'GPIO13' was not declared in this scope
```

Make sure you're using the correct environment:
- `pixel_c3` for XIAO ESP32-C3
- `pixel_s3` for ESP32-S3 Mini

### Serial Monitor Shows Garbage

Check baud rate is set to 115200:

```bash
pio device monitor -b 115200
```

### Display Not Working

1. Check wiring matches your board type (see `HARDWARE.md`)
2. Verify board type in serial output: `Board: XIAO ESP32-C3` or `Board: ESP32-S3 Mini`
3. Check power supply (3.3V for logic, 5V for display backlight)
4. Verify SPI connections with multimeter

---

## Build Flags

### ESP32-S3 Specific

The S3 environment enables:
- **PSRAM:** 2MB external RAM for larger buffers
- **USB CDC:** Native USB serial (no external USB-UART chip needed)

```ini
board_build.arduino.memory_type = qio_opi
build_flags = 
  -DBOARD_HAS_PSRAM
  -DARDUINO_USB_CDC_ON_BOOT=1
```

### Master Controller (CYD)

The master uses TFT_eSPI library with custom pin definitions for the CYD board. See `platformio.ini` for full configuration.

---

## Performance Comparison

| Board | SPI Mode | Clock Speed | FPS | CPU Usage |
|-------|----------|-------------|-----|-----------|
| ESP32-C3 | Software | ~40 MHz | ~30 | High |
| ESP32-S3 | Hardware + DMA | ~80 MHz | ~50-60 | Low |

**Recommendation:** Use ESP32-S3 for production. The dual cores and hardware SPI provide significant headroom for future features.

---

## Quick Commands Reference

```bash
# List all environments
pio run --list-targets

# Build without uploading
pio run -e pixel_s3

# Clean build
pio run -e pixel_s3 --target clean

# Upload pre-built firmware
pio run -e pixel_s3 --target upload

# Build, upload, and monitor in one command
pio run -e pixel_s3 --target upload && pio device monitor -e pixel_s3

# Check for compilation errors only
pio run -e pixel_s3 --target compiledb
```

---

## Production Workflow

For building all 24 pixels:

1. **Prepare workspace:**
   - Label 24 devices with IDs 0-23
   - Organize in order

2. **Flash each pixel:**
   ```bash
   # Edit PIXEL_ID in src/main.cpp
   # Flash device
   pio run -e pixel_s3 --target upload
   # Verify ID in serial monitor
   # Move to next device
   ```

3. **Test each pixel:**
   - Verify display shows animation
   - Check serial output for errors
   - Confirm FPS is ~50-60 (S3) or ~30 (C3)

4. **Flash master controller:**
   ```bash
   pio run -e master --target upload
   ```

5. **System integration:**
   - Connect all pixels to power bus
   - Connect ESP-NOW communication
   - Test synchronized operation

---

## See Also

- **[HARDWARE.md](HARDWARE.md)** - Complete hardware specifications and pinouts
- **[README.md](README.md)** - Project overview and architecture
- **[platformio.ini](platformio.ini)** - Build configuration

