# Touch Interface for Master Controller

## Overview

The master controller now has a touch-based menu system that allows you to select different control modes for the pixel displays.

## Main Menu

When the master boots up, it displays a menu with 4 buttons:

```
┌─────────────────────────────────────┐
│      Twenty-Four Times              │
│                                     │
│  ┌──────────┐  ┌──────────┐       │
│  │Simulation│  │ Patterns │       │
│  │  Random  │  │Cycle tests│       │
│  └──────────┘  └──────────┘       │
│  ┌──────────┐  ┌──────────┐       │
│  │ Identify │  │  Manual  │       │
│  │Show IDs  │  │Coming soon│       │
│  └──────────┘  └──────────┘       │
└─────────────────────────────────────┘
```

## Control Modes

### 1. Simulation Mode (Top Left)

**What it does:**
- Sends random patterns to all pixels every 5 seconds
- Mimics the behavior of the HTML simulation
- Random angles (0°, 90°, 180°, 270°)
- Random transitions (Linear, Ease, Elastic, Bounce, Back variants)
- Random colors from the 16-color palette
- Random opacity (0, 50, or 255)
- Random durations (0.5-9.0 seconds, weighted toward longer)

**Display shows:**
- "SIMULATION MODE"
- Current transition type
- Current duration
- "Random angles, colors, opacity"
- "Touch screen to return to menu"

**Timing:**
- New random pattern every 5 seconds
- Timer starts at the END of the previous animation duration
- This means if you send a 3-second animation, the next one starts 5 seconds after that completes

**Use case:** 
- Testing all pixels with varied patterns
- Demonstrating the full range of capabilities
- Ambient/artistic display mode

### 2. Test Patterns Mode (Top Right)

**What it does:**
- Cycles through 5 predefined test patterns
- Each pattern lasts 5 seconds
- Patterns:
  1. All Up (0°) - White on Black, Elastic, 3s
  2. All Right (90°) - Black on White, Ease In-Out, 2s
  3. All Down (180°) - Dark Brown on Cream, Linear, 2.5s
  4. All Left (270°) - Wheat on Dark Slate, Elastic, 3.5s
  5. Staggered - Different angles per pixel, Ease In-Out, 4s

**Display shows:**
- Pattern name
- Duration
- Transition type
- Color palette name
- Opacity
- "Next in 5 sec"
- "Touch screen to return to menu"

**Use case:**
- Systematic testing of all pixels
- Verifying synchronization
- Demonstrating specific patterns

### 3. Identify Mode (Bottom Left)

**What it does:**
- Sends CMD_IDENTIFY to all pixels (ID 255)
- Each pixel displays its ID number in large text on blue background
- Useful for physical installation and debugging

**Display shows:**
- "IDENTIFY MODE"
- "All Pixels"
- "Duration: 5 seconds"

**Timing:**
- Runs once when activated
- Stays on screen until you touch to return to menu

**Use case:**
- Physical installation - identify which pixel is which
- Debugging - verify pixel IDs
- Setup and configuration

### 4. Manual Mode (Bottom Right) - Coming Soon

**Status:** Disabled (grayed out)

**Future features:**
- Manual angle control per pixel
- Color picker
- Opacity slider
- Transition type selector
- Individual pixel targeting

## Touch Interaction

### In Menu Mode
- Touch any of the 4 buttons to enter that mode
- Buttons have visual feedback (different colors)

### In Any Other Mode
- Touch anywhere on the screen to return to the main menu
- Allows quick mode switching

## Technical Details

### Touch Hardware
- **CYD Board:** Uses resistive touch (XPT2046) or capacitive touch (CST816S)
- **Touch pins:** SDA=33, SCL=32, INT=21, RST=25
- **Library:** TFT_eSPI built-in touch support

### Debouncing
- 200ms debounce time to prevent accidental double-taps
- Tracks last touch time to filter rapid touches

### Display
- **Resolution:** 320x240 pixels (landscape)
- **Colors:** 16-bit RGB565
- **Fonts:** Multiple sizes (1-3) for hierarchy

## Usage Tips

1. **Start in Simulation Mode** to see the full range of capabilities
2. **Use Test Patterns** to verify all pixels are synchronized
3. **Use Identify Mode** during installation to label physical pixels
4. **Touch anywhere** to quickly return to menu and switch modes

## Future Enhancements

- [ ] Manual mode with sliders and pickers
- [ ] Save/load custom patterns
- [ ] Brightness control
- [ ] WiFi time sync for clock mode
- [ ] Pixel status indicators (which pixels are responding)
- [ ] Pattern editor
- [ ] Scheduling (time-based pattern changes)

