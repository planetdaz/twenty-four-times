# Quick Wiring Guide

Visual reference for connecting GC9A01 displays to both ESP32-C3 and ESP32-S3 boards.

---

## ESP32-C3 (XIAO) Wiring

**Board:** Seeed Studio XIAO ESP32-C3

```
        XIAO ESP32-C3                    GC9A01 Display
        ═════════════                    ══════════════

        Pin 12 (3V3)  ────────────────>  VCC
        Pin 13 (GND)  ────────────────>  GND
        Pin 11 (D10)  ────────────────>  DIN (MOSI)
        Pin 9  (D8)   ────────────────>  CLK (SCK)
        Pin 4  (D3)   ────────────────>  CS
        Pin 5  (D4)   ────────────────>  DC
        Pin 3  (D2)   ────────────────>  RST
                                         BL ──> VCC
```

**Pin Locations on XIAO:**
```
                ╔═══════════╗
                ║  USB-C    ║
                ╚═══════════╝
LEFT (Pins 1-7)                RIGHT (Pins 14-8)
Pin 1  ── D0                   5V        ── Pin 14
Pin 2  ── D1                   GND       ── Pin 13 ──> GND
Pin 3  ── D2 ──> RST           3V3       ── Pin 12 ──> VCC
Pin 4  ── D3 ──> CS            D10       ── Pin 11 ──> DIN
Pin 5  ── D4 ──> DC            D9        ── Pin 10
Pin 6  ── D5                   D8        ── Pin 9  ──> CLK
Pin 7  ── D6                   D7        ── Pin 8
```

---

## ESP32-S3-Zero Wiring

**Board:** Waveshare ESP32-S3-Zero

```
        ESP32-S3-Zero                    GC9A01 Display
        ═════════════                    ══════════════

        Left Pin 3   (3V3 OUT) ───────>  VCC
        Left Pin 2   (GND)     ───────>  GND
        Right Pin 14 (GP11)    ───────>  DIN (MOSI)  ← FSPI_MOSI (default)
        Right Pin 15 (GP12)    ───────>  CLK (SCK)   ← FSPI_CLK @ 80MHz (default)
        Right Pin 13 (GP10)    ───────>  CS          ← FSPI_CS (default)
        Left Pin 9   (GP6)     ───────>  DC
        Left Pin 7   (GP4)     ───────>  RST
                                         BL ──> VCC
```

**Pin Locations on ESP32-S3-Zero:**
```
                ╔═══════════╗
                ║  USB-C    ║
                ╚═══════════╝
LEFT HEADER (1-9)              RIGHT HEADER (18-10)
Pin 1  ── 5V                   TX        ── Pin 18
Pin 2  ── GND ──> GND          RX        ── Pin 17
Pin 3  ── 3V3 ──> VCC          GP13      ── Pin 16
Pin 4  ── GP1                  GP12 ⭐   ── Pin 15 ──> CLK (FSPI_CLK)
Pin 5  ── GP2                  GP11 ⭐   ── Pin 14 ──> DIN (FSPI_MOSI)
Pin 6  ── GP3                  GP10 ⭐   ── Pin 13 ──> CS (FSPI_CS)
Pin 7  ── GP4 ──> RST          GP9       ── Pin 12
Pin 8  ── GP5                  GP8       ── Pin 11
Pin 9  ── GP6 ──> DC           GP7       ── Pin 10

BOTTOM SMD PADS (not used):
GP16 (Pin 9)
GP15 (Pin 8)
GP14 (Pin 7)
```

⭐ = Hardware SPI2 (FSPI) default pins @ 80MHz

---

## Side-by-Side Comparison

| Connection | XIAO ESP32-C3 | ESP32-S3-Zero | Notes |
|------------|---------------|---------------|-------|
| **VCC** | Pin 12 (3V3) | Left Pin 3 (3V3 OUT) | 3.3V power |
| **GND** | Pin 13 (GND) | Left Pin 2 (GND) | Ground |
| **DIN** | Pin 11 (D10/GPIO10) | Right Pin 14 (GP11) | MOSI/SDA (FSPI_MOSI default on S3) |
| **CLK** | Pin 9 (D8/GPIO8) | Right Pin 15 (GP12) | SCK/SCL (FSPI_CLK @ 80MHz default on S3) |
| **CS** | Pin 4 (D3/GPIO5) | Right Pin 13 (GP10) | Chip Select (FSPI_CS default on S3) |
| **DC** | Pin 5 (D4/GPIO6) | Left Pin 9 (GP6) | Data/Command |
| **RST** | Pin 3 (D2/GPIO4) | Left Pin 7 (GP4) | Reset |
| **BL** | VCC (always on) | VCC (always on) | Backlight |

---

## Key Differences

### ESP32-C3 (XIAO)
- ✅ Compact XIAO form factor
- ✅ Software SPI (bit-banging)
- ⚠️ GPIO8 is strapping pin (safe for SPI CLK)
- 📊 Performance: ~30 FPS

### ESP32-S3-Zero
- ✅ Hardware SPI2 (FSPI) @ 80MHz - header pins only, no SMD soldering!
- ✅ Dual cores (240 MHz each)
- ✅ 2MB PSRAM for future features
- ✅ Castellated edges for SMD mounting
- 📊 Performance: ~36 FPS (44% faster than C3!)

---

## Wiring Tips

1. **Keep wires short** - SPI signals degrade with length. Keep under 15cm for best results.

2. **Use solid core wire** - 22-24 AWG solid core is easier to work with for breadboard prototyping.

3. **Power first** - Connect VCC and GND before signal wires.

4. **Check polarity** - Double-check VCC/GND before powering on.

5. **Test continuity** - Use multimeter to verify connections before power-on.

6. **Decoupling caps** - Add 10µF + 100nF capacitors near MCU power pins for stability.

7. **Backlight** - BL pin can be connected to VCC (always on) or to a GPIO with PWM for brightness control.

---

## Troubleshooting

### Display stays white/blank
- Check VCC and GND connections
- Verify RST is connected (display needs reset pulse)
- Check CS and DC connections

### Display shows garbage/noise
- SPI wires too long or poor connections
- Check CLK and DIN connections
- Verify correct board selected in platformio.ini

### Display works but slow/choppy
- ESP32-C3: Normal, ~30 FPS is expected
- ESP32-S3: Check you're using `pixel_s3` environment (hardware SPI)

### No serial output
- ESP32-C3: Press BOOT button while connecting USB
- ESP32-S3: Press BOOT, then RESET, then release BOOT

---

## Testing Your Wiring

After wiring, flash the test firmware:

```bash
# For ESP32-C3
pio run -e pixel_c3 --target upload

# For ESP32-S3
pio run -e pixel_s3 --target upload
```

You should see:
1. Serial output showing board type
2. Rotating clock hands on display
3. Smooth animation (~30 FPS on C3, ~50-60 FPS on S3)

---

## Production Assembly

For final assembly:
1. Test each board on breadboard first
2. Solder connections or use JST connectors
3. Use heat shrink tubing for strain relief
4. Label each pixel with its ID (0-23)
5. Test before mounting in enclosure

---

See **[HARDWARE.md](HARDWARE.md)** for complete specifications and **[FLASHING.md](FLASHING.md)** for build instructions.

