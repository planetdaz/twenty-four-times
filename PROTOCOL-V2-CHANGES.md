# ESP-NOW Protocol V2 Changes

## Overview

The ESP-NOW protocol has been significantly enhanced to provide full control from the master controller. Pixels are now purely reactive - they only respond to master commands and never run autonomously.

## Key Changes

### 1. Fixed Angle Encoding

**Problem:** Angles like 90°, 180°, 270° were being decoded as 89°, 179°, 269°

**Solution:** 
- Changed angle encoding from `(degrees / 360.0f) * 255.0f` to `(degrees / 360.0f) * 256.0f + 0.5f`
- Changed decoding from `(angle / 255.0f) * 360.0f` to `(angle / 256.0f) * 360.0f`
- Added rounding to ensure exact values for common angles

### 2. Duration Encoding

**Old:** `uint16_t duration_ms` (2 bytes, 0-65535 milliseconds)

**New:** `uint8_t duration` (1 byte, 0-60 seconds)
- Encoding: `(seconds / 60.0f) * 255.0f + 0.5f`
- Decoding: `(duration / 255.0f) * 60.0f`
- Saves 1 byte per packet
- More intuitive range for animations

### 3. Rotation Direction Control

**New Field:** `RotationDirection directions[MAX_PIXELS][HANDS_PER_PIXEL]` (72 bytes)

**Options:**
- `DIR_SHORTEST` (0) - Choose shortest path (default)
- `DIR_CW` (1) - Clockwise
- `DIR_CCW` (2) - Counter-clockwise

**Benefit:** Master can synchronize rotation directions across all pixels

### 4. Color Palette System

**New Fields:**
- `uint8_t colorIndices[MAX_PIXELS]` (24 bytes) - Color palette index per pixel
- Shared color palette with 16 entries (defined in ESPNowComm.h)

**Palette Entries:**
0. White on Black
1. Black on White
2. Dark Brown on Cream
3. Cream on Dark Brown
4. Wheat on Dark Slate
5. Dark Slate on Wheat
6. Cornsilk on Saddle Brown
7. Light Gray on Navy
8. Light Yellow on Red-Orange
9. Dark Magenta on Gold
10. White on Deep Sky Blue
11. Ivory on Deep Pink
12. Midnight Blue on Lime Green
13. Lemon Chiffon on Blue Violet
14. Midnight Blue on Dark Orange
15. Dark Red on Turquoise

**Benefit:** 
- Master and pixels share same color names
- Only 1 byte per pixel instead of 4 bytes (2x RGB565 colors)
- Easy to display color names on master UI

### 5. Opacity Control

**New Field:** `uint8_t opacities[MAX_PIXELS]` (24 bytes)

**Range:** 0-255 (0 = transparent, 255 = opaque)

**Benefit:** Master can control opacity per pixel for fade effects

### 6. All Transition Types Supported

**Old:** 4 transition types (Linear, Ease In-Out, Elastic, Instant)

**New:** 8 transition types
- TRANSITION_LINEAR (0)
- TRANSITION_EASE_IN_OUT (1)
- TRANSITION_ELASTIC (2)
- TRANSITION_BOUNCE (3)
- TRANSITION_BACK_IN (4)
- TRANSITION_BACK_OUT (5)
- TRANSITION_BACK_IN_OUT (6)
- TRANSITION_INSTANT (7)

**Benefit:** Full access to all pixel easing functions

### 7. Identify Command

**New Command:** `CMD_IDENTIFY` (0x05)

**Packet Structure:**
```cpp
struct IdentifyPacket {
  CommandType command;  // CMD_IDENTIFY
  uint8_t pixelId;      // Which pixel to identify (0-23, or 255 for all)
};
```

**Behavior:** 
- Pixel shows its ID in large text on blue background
- Remains in identify mode until next command received
- Useful for physical identification during installation

### 8. Demo Mode Removed from Pixels

**Old:** Pixels had autonomous demo mode with random transitions

**New:** Pixels are purely reactive
- No autonomous operation
- No random number generation on pixels
- All randomness handled by master
- Cleaner, more predictable behavior

### 9. Helper Functions Moved to Library

**Moved from pixel to ESPNowComm.h:**
- `getRandomAngle()` - Returns 0°, 90°, 180°, or 270°
- `getRandomColorIndex()` - Returns random palette index
- `getRandomTransition()` - Returns random transition type
- `getRandomDuration()` - Returns weighted random duration (0.5-9.0s)
- `getRandomOpacity()` - Returns 0, 50, or 255
- `getTransitionName()` - Returns human-readable transition name

**Benefit:** 
- Master can use these for random pattern generation
- Pixels don't need random logic
- Consistent behavior across system

## Packet Size

**Old:** 76 bytes
- 1 byte command
- 1 byte transition
- 2 bytes duration
- 72 bytes angles

**New:** 219 bytes
- 1 byte command
- 1 byte transition
- 1 byte duration
- 72 bytes angles
- 72 bytes directions
- 24 bytes color indices
- 24 bytes opacities
- 24 bytes reserved

**Still well under ESP-NOW's 250 byte limit!**

## Migration Guide

### For Pixel Firmware

1. Update `onPacketReceived()` to handle new packet structure
2. Extract directions: `cmd.getPixelDirections(PIXEL_ID, dir1, dir2, dir3)`
3. Extract color: `colorIndex = cmd.colorIndices[PIXEL_ID]`
4. Extract opacity: `opacity = cmd.opacities[PIXEL_ID]`
5. Convert duration: `durationToFloat(cmd.duration)`
6. Remove all demo mode code
7. Add identify mode display

### For Master Firmware

1. Update pattern structure to include `duration_sec`, `colorIndex`, `opacity`
2. Use `floatToDuration()` to encode duration
3. Use `setPixelAngles()` with direction parameters
4. Use `setPixelStyle()` to set color and opacity
5. Use helper functions from ESPNowComm.h for random generation
6. Display color names from `COLOR_PALETTE[]`

## Testing

Test all new features:
- [ ] Angles decode correctly (90° = 90°, not 89°)
- [ ] Duration encoding/decoding accurate
- [ ] Direction control works (CW, CCW, shortest)
- [ ] All 16 colors display correctly
- [ ] Opacity transitions smoothly
- [ ] All 8 transition types work
- [ ] Identify command shows pixel ID
- [ ] Pixels never run autonomously

