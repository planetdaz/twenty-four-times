# NPM Scripts Reference

Quick reference for all available npm scripts in the Twenty-Four Times project.

## Building Firmware

```bash
# Build pixel firmware (ESP32-S3)
npm run build:pixel

# Build master firmware (resistive touch CYD)
npm run build:master
```

## Uploading Firmware (USB)

```bash
# Upload pixel firmware via USB
npm run upload:pixel

# Upload master firmware via USB
npm run upload:master
```

## OTA (Over-The-Air) Updates

```bash
# Prepare OTA firmware (copy pixel firmware to data/)
npm run ota:prepare

# Upload OTA firmware to master's filesystem
npm run ota:upload

# Build pixel firmware + upload to master (complete workflow)
npm run ota:full
```

### OTA Workflow

1. **Build and prepare OTA:**
   ```bash
   npm run ota:full
   ```

2. **On master touchscreen:**
   - Tap **OTA** button
   - Tap **Start Server**
   - Tap **Send Update**

3. **Watch pixels update:**
   - Each pixel shows progress on its screen
   - Pixels auto-reboot when complete

## Version Management

```bash
# Bump minor version (1.0 -> 1.1)
npm run version:bump minor

# Bump major version (1.0 -> 2.0)
npm run version:bump major
```

After bumping version, rebuild firmware:
```bash
npm run build:pixel
npm run build:master
```

## Complete Update Workflow

When you want to push a new version to all pixels:

```bash
# 1. Bump version
npm run version:bump minor

# 2. Build and upload OTA firmware to master
npm run ota:full

# 3. Use master touchscreen to trigger OTA update
#    (OTA -> Start Server -> Send Update)
```

## Notes

- **OTA updates** only work for pixels (not master)
- Master firmware must be updated via USB
- Pixel firmware is built for `pixel_s3` environment (ESP32-S3)
- Master firmware is built for `master_resistive` environment (resistive touch CYD)
- All scripts assume you have PlatformIO CLI (`pio`) in your PATH

