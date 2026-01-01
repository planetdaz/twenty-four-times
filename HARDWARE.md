# Hardware Documentation

## Pixel Node Hardware

Each of the 24 pixel nodes consists of:
- 1× Round LCD display (GC9A01, 240×240)
- 1× ESP32 microcontroller (C3 or S3)
- Custom 3D-printed enclosure
- 3-wire connection (5V, GND, COMMS)

---

## Display Module

**Product:** YELUFT 1.28" Round IPS LCD  
**Link:** [Amazon B0F21J42WD](https://www.amazon.com/YELUFT-Interface-Self-Luminous-Raspberry-Pre-Soldered/dp/B0F21J42WD)

| Specification | Value |
|---------------|-------|
| Controller | GC9A01 |
| Resolution | 240 × 240 pixels |
| Size | 1.28" diagonal |
| Interface | SPI (4-wire + CS + DC + RST) |
| Color Depth | RGB565 (16-bit) |
| Voltage | 3.3V logic, 5V power |
| Current Draw | ~150mA @ full white |

**Physical Pins:**
- VCC (5V)
- GND
- DIN (MOSI/SDA)
- CLK (SCK/SCL)
- CS (Chip Select)
- DC (Data/Command)
- RST (Reset)
- BL (Backlight, usually tied to VCC)

---

## Microcontroller Options

The project supports two ESP32 variants. Both work, but S3 offers better performance.

### Option 1: Seeed XIAO ESP32-C3 (Tested)

**Product:** Seeed Studio XIAO ESP32C3  
**Link:** [Seeed Studio](https://www.seeedstudio.com/Seeed-XIAO-ESP32C3-p-5431.html)

| Specification | Value |
|---------------|-------|
| MCU | ESP32-C3 (RISC-V) |
| Cores | 1 |
| Clock Speed | 160 MHz |
| SRAM | 400 KB |
| Flash | 4 MB |
| PSRAM | None |
| WiFi | 802.11 b/g/n (2.4GHz) |
| Bluetooth | BLE 5.0 |
| Form Factor | 21mm × 17.8mm |
| USB | USB-C (Serial/JTAG) |
| Current Draw | ~100mA typical |

**Physical Pinout:**

```
                    ╔═══════════╗
                    ║  USB-C    ║
                    ╚═══════════╝
    LEFT SIDE                      RIGHT SIDE
    Pin 1  ──  5V                  GND  ── Pin 14
    Pin 2  ── GND                  3V3  ── Pin 13
    Pin 3  ──  D2 (GPIO4)  ← RST   D10 (GPIO10) ── Pin 11  ← TFT_SDA
    Pin 4  ──  D3 (GPIO5)  ← CS    D9  (GPIO9)  ── Pin 10
    Pin 5  ──  D4 (GPIO6)  ← DC    D8  (GPIO8)  ── Pin 9   ← TFT_SCL
    Pin 6  ──  D5 (GPIO7)          D7  (GPIO20) ── Pin 8
    Pin 7  ──  D6 (GPIO21)         3V3 ── Pin 7 (bottom)

    TFT Wiring Summary:
    Pin 3  = D2  (GPIO4)  → TFT_RST
    Pin 4  = D3  (GPIO5)  → TFT_CS
    Pin 5  = D4  (GPIO6)  → TFT_DC
    Pin 9  = D8  (GPIO8)  → TFT_SCL (⚠️ strapping pin)
    Pin 11 = D10 (GPIO10) → TFT_SDA
```

**Pin Mapping (C3):**

| Physical Pin | Label | GPIO | Function | TFT Connection |
|--------------|-------|------|----------|----------------|
| 3 | D2 | GPIO4 | Digital I/O | **TFT_RST** |
| 4 | D3 | GPIO5 | Digital I/O | **TFT_CS** |
| 5 | D4 | GPIO6 | Digital I/O | **TFT_DC** |
| 9 | D8 | GPIO8 | Digital I/O | **TFT_SCL** (⚠️ strapping pin) |
| 11 | D10 | GPIO10 | Digital I/O | **TFT_SDA** |

**Notes:**
- GPIO8 is a strapping pin (boot mode selection) - safe for SPI CLK output
- Uses software SPI (bit-banging) since pins don't match hardware SPI
- Achieves ~30 FPS with full-screen updates
- Castellated edges allow SMD soldering to carrier board

---

### Option 2: ESP32-S3-Zero (Recommended)

**Product:** Waveshare ESP32-S3-Zero
**Link:** [Amazon B0D2CY4Y5H](https://www.amazon.com/dp/B0D2CY4Y5H)

| Specification | Value |
|---------------|-------|
| MCU | ESP32-S3FH4R2 (Xtensa LX7) |
| Cores | 2 |
| Clock Speed | 240 MHz |
| SRAM | 512 KB |
| Flash | 4 MB |
| PSRAM | 2 MB (Octal SPI) |
| WiFi | 802.11 b/g/n (2.4GHz) |
| Bluetooth | BLE 5.0 |
| USB | Native USB OTG |
| Current Draw | ~100mA typical |
| Form Factor | Castellated edges for SMD mounting |

**Physical Pinout:**

```
                    ╔═══════════╗
                    ║  USB-C    ║
                    ╚═══════════╝
    LEFT HEADER (Pins 1-9)         RIGHT HEADER (Pins 18-10)
    Pin 1  ── 5V                   TX (UART0)    ── Pin 18
    Pin 2  ── GND                  RX (UART0)    ── Pin 17
    Pin 3  ── 3V3(OUT)             GP13          ── Pin 16
    Pin 4  ── GP1                  GP12 (FSPI)   ── Pin 15  ← TFT_SCL
    Pin 5  ── GP2                  GP11 (FSPI)   ── Pin 14  ← TFT_SDA
    Pin 6  ── GP3                  GP10 (FSPI)   ── Pin 13  ← TFT_CS
    Pin 7  ── GP4  ← TFT_RST       GP9           ── Pin 12
    Pin 8  ── GP5                  GP8           ── Pin 11
    Pin 9  ── GP6  ← TFT_DC        GP7           ── Pin 10

    BOTTOM SMD PADS (not used - no SMD soldering required):
    GP16 (Pin 9)
    GP15 (Pin 8)
    GP14 (Pin 7)

    Note: GP21 is used for onboard WS2812 RGB LED (not exposed on header)
```

**Pin Mapping (S3 - Hardware SPI):**

| Physical Pin | Label | GPIO | Function | TFT Connection | Notes |
|--------------|-------|------|----------|----------------|-------|
| Left 7 | GP4 | GPIO4 | Digital I/O | **TFT_RST** | Reset |
| Right 13 | GP10 | GPIO10 | FSPI_CS | **TFT_CS** | Hardware SPI CS (default) |
| Left 9 | GP6 | GPIO6 | Digital I/O | **TFT_DC** | Data/Command |
| Right 15 | GP12 | GPIO12 | FSPI_CLK | **TFT_SCL** | Hardware SPI CLK @ 80MHz (default) |
| Right 14 | GP11 | GPIO11 | FSPI_MOSI | **TFT_SDA** | Hardware SPI MOSI (default) |

**Notes:**
- Uses hardware SPI2 (FSPI) @ 80MHz - no SMD soldering required!
- Performance: ~36 FPS (44% faster than software SPI)
- All pins accessible via headers
- Dual cores allow background tasks
- 2MB PSRAM for larger buffers or future features

---

## Wiring Connections

### ESP32-C3 to GC9A01 Display

```
XIAO ESP32-C3                    GC9A01 Display
─────────────                    ──────────────
Pin 12 (3V3)              ────>  VCC
Pin 13 (GND)              ────>  GND
Pin 11 (D10/GPIO10)       ────>  DIN (MOSI)
Pin 9  (D8/GPIO8)         ────>  CLK (SCK)
Pin 4  (D3/GPIO5)         ────>  CS
Pin 5  (D4/GPIO6)         ────>  DC
Pin 3  (D2/GPIO4)         ────>  RST
                                 BL ──> VCC (or PWM for dimming)
```

### ESP32-S3-Zero to GC9A01 Display

```
ESP32-S3-Zero                    GC9A01 Display
─────────────                    ──────────────
Left Pin 3   (3V3 OUT)    ────>  VCC
Left Pin 2   (GND)        ────>  GND
Right Pin 14 (GP11)       ────>  DIN (MOSI)  ← FSPI_MOSI (default)
Right Pin 15 (GP12)       ────>  CLK (SCK)   ← FSPI_CLK @ 80MHz (default)
Right Pin 13 (GP10)       ────>  CS          ← FSPI_CS (default)
Left Pin 9   (GP6)        ────>  DC
Left Pin 7   (GP4)        ────>  RST
                                 BL ──> VCC (or PWM for dimming)
```

---

## Firmware Pin Configuration

The firmware automatically detects the board type and uses appropriate pins:

**ESP32-C3:** Software SPI on GPIO4,5,6,8,10
**ESP32-S3:** Hardware SPI2 (FSPI) on GPIO4,6,10,11,12 @ 80MHz (default FSPI pins)

Both boards use header pins only - no SMD soldering required.

See `src/main.cpp` for implementation details.

---

## Power Requirements

### Per-Pixel Power Budget

| Component | Typical | Peak |
|-----------|---------|------|
| GC9A01 Display | 80mA | 150mA (full white) |
| ESP32-C3/S3 | 80mA | 100mA |
| **Total per pixel** | **160mA** | **250mA** |

### System Total (24 Pixels)

| Scenario | Current @ 5V | Power |
|----------|--------------|-------|
| Typical (mixed content) | 3.8A | 19W |
| Peak (all white) | 6.0A | 30W |

**Recommended PSU:** 5V 8A (40W) with margin

---

## Assembly Notes

1. **Castellated Edges:** Both MCU boards have half-circle plated holes along edges, allowing direct SMD soldering to a carrier PCB for compact, permanent installation
2. **Strapping Pins:** GPIO8 on C3 is a boot mode pin - ensure it's not pulled LOW during power-on/reset
3. **Decoupling:** Add 10µF + 100nF capacitors near each MCU's power pins
4. **Wire Gauge:** Use 22-24 AWG for power distribution, 26-28 AWG for SPI signals
5. **Cable Length:** Keep SPI wires <15cm for reliable 40MHz+ operation

---

## Performance Comparison

| Metric | ESP32-C3 | ESP32-S3 |
|--------|----------|----------|
| Frame Rate | ~30 FPS | ~50-60 FPS |
| SPI Mode | Software | Hardware + DMA |
| SPI Clock | ~40 MHz | ~80 MHz |
| CPU Overhead | High | Low |
| Future Headroom | Limited | Excellent |

**Recommendation:** Use ESP32-S3 for production builds. C3 works but has less headroom for future features.

