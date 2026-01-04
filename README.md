# **Twenty-Four Times**

## Overview

**Twenty-Four Times** is a modular kinetic–digital art piece composed of **24 circular display nodes**, arranged in an **8 × 3 matrix**.
Each node renders animated geometry—most commonly three rotating “hands”—that collectively form large-scale numeric digits and choreographed transitions.

While rooted in timekeeping, the system is designed as a **general distributed animation platform**, capable of evolving into non-clock visual modes over time.

The name *Twenty-Four Times* intentionally reflects both:

* the number of clocks in the matrix, which is also the number of hours in a day
* the multiplication of time, motion, and state across space

### Video Demonstrations

* [On-screen simulation and initial build with custom 3D printed enclosures](https://www.youtube.com/watch?v=c2BB3x_dGME)
* [Testing master controller with ESP-NOW communication](https://www.youtube.com/watch?v=1ejrj0Ynra4)

![Prototype build showing pixel modules with round LCD displays and 3D printed enclosures](images/twenty-four-times-proto-work%20(4).jpg)

---

## 1. System Architecture

### Distributed Rendering (One MCU per Pixel)

Each pixel is a **fully self-contained module**:

* display
* microcontroller
* local rendering
* identical firmware

A single master controller coordinates all pixels via a **simple broadcast protocol**.

| Role                  | Description                                                              |
| --------------------- | ------------------------------------------------------------------------ |
| **Master Controller** | Generates global animation targets and transition instructions           |
| **Pixel Nodes (×24)** | Render motion locally based on received targets and their assigned index |
| **Communication**     | One-way wireless broadcast (ESP-NOW protocol)                            |

### Why this architecture

* Guaranteed smooth animation per display
* No shared SPI bandwidth
* Simple wireless setup
* Identical firmware on all nodes

---

## 2. Hardware Specification

### Display

* **1.28" Round IPS LCD** (GC9A01 controller)
* **240 × 240 RGB565** resolution
* SPI interface (write-only)

### Microcontroller (Per Pixel)

* **ESP32-S3** (recommended) or **ESP32-C3** (tested)
* Dual-core 240 MHz (S3) or single-core 160 MHz (C3)
* Compact form factor with castellated edges
* Hardware SPI support (S3) for optimal performance

> **Note:** Both C3 and S3 are supported. S3 is recommended for production due to dual cores, PSRAM, and hardware SPI (50-60 FPS vs 30 FPS on C3).

📄 **See [HARDWARE.md](HARDWARE.md) for complete specifications, pinouts, wiring diagrams, and assembly notes.**

---

## 3. Power & Wiring

### 3.1 Per-Pixel Power

| Component           | Peak Draw            |
| ------------------- | -------------------- |
| Display             | ~150 mA (full white) |
| ESP32-S3            | ~100 mA              |
| **Total per pixel** | ~250 mA max          |

**System total:** ~6 A @ 5 V worst case

---

### 3.2 Power Distribution

* Central regulated **5 V supply**
* Power injected directly into each pixel
* Displays are **not powered via USB**
* Local decoupling per module

---

### 3.3 Pixel Interconnect

Each pixel module exposes **two conductors**:

| Line      | Purpose   |
| --------- | --------- |
| **VCC**   | 5 V power |
| **GND**   | Ground    |

* 24 identical 2-pin connections (power only)
* Dupont-compatible
* Bus topology inside main enclosure

---

## 5. Communication Protocol

### Device Provisioning & Discovery

For details on how devices are discovered, identified, and assigned unique IDs in the network, see the [DISCOVERY_AND_PROVISIONING.md](DISCOVERY_AND_PROVISIONING.md) document. It outlines the full protocol and user workflow for reliable setup and mapping of ESP32 pixel modules.

### Protocol Overview

Communication between the master and pixel nodes uses the **ESP-NOW wireless protocol**. The master broadcasts packets containing animation and rendering instructions to all nodes. Each node receives only the data relevant to its assigned index.

#### Packet Structure (AngleCommandPacket)

Each broadcast packet contains:

- `command`: Command type (e.g., CMD_SET_ANGLES)
- `transition`: Animation/easing type (e.g., linear, elastic, bounce)
- `duration`: Animation duration (0–60 seconds, 0.25s steps)
- `angles[MAX_PIXELS][3]`: Target angles for all hands on all pixels
- `directions[MAX_PIXELS][3]`: Rotation direction for each hand (CW/CCW/shortest)
- `colorIndices[MAX_PIXELS]`: Color palette index for each pixel
- `opacities[MAX_PIXELS]`: Opacity (0–255) for each pixel
- `reserved[24]`: Reserved for future use

#### Example Workflow

1. Master prepares an AngleCommandPacket with target angles, directions, colors, and opacities for all 24 pixels.
2. Master broadcasts the packet via ESP-NOW.
3. Each pixel node receives the packet, extracts its own instructions (by index), and animates accordingly.

#### Other Packet Types
- **Ping**: Used to keep pixel nodes awake and responsive.
- **Identify**: Used for provisioning and mapping, allowing a pixel to visually indicate itself.
- **Assignment**: Used during provisioning to assign a unique ID to each pixel node based on its MAC address.

---

## 6. Graphics & Rendering

### Rendering Model

* Full local framebuffer per display
* Sprite-based drawing (TFT_eSPI)
* Anti-aliased hand geometry
* Local easing, blending, and timing

Measured performance:

* ~30 FPS per display
* No visible tearing or flicker

### Why local rendering matters

* No per-pixel image streaming
* No shared memory
* Visual coherence achieved through **shared intent**, not shared pixels

---

## 7. Mechanical Design

### Pixel Modules

* Fully enclosed, self-contained units
* Designed in **Fusion 360**
* 3D printed and physically validated
* Internal wiring only
* Short internal jumpers between MCU and display
* Single 3-wire external lead per module

### Assembly

* Modules mount into a larger frame
* Consistent spacing becomes part of the visual language
* No exposed electronics on the front face

The repository includes:

* printable enclosure files
* assembly-ready geometry
* revisions used in physical prototypes

---

## 8. Web-Based Visual Simulator

The repository includes an **HTML + JavaScript simulator** (`twenty-four-times-simulation.html`) used to:

* prototype layouts
* test animation ideas
* design transition behavior
* validate legibility and motion

This simulator:

* predates hardware completion
* evolves alongside firmware
* acts as a reference for animation logic

See the [simulation demo video](https://www.youtube.com/watch?v=c2BB3x_dGME) for examples of the visual output.

---

## 9. Visual Modes (Planned)

Initial focus:

* **Clock Mode**
  Coordinated digits formed by three-hand geometry, with transitions

Planned future exploration:

* Scatter / reform transitions
* Phase-shifted motion
* Flow and cascade effects
* Eye and attention-based visuals
* Non-time generative modes

These modes will evolve once the clock mode is complete and stable.

---

## 10. Development Status & Milestones

* ✅ Pixel enclosure designed, printed, and validated
* ✅ Multiple physical pixel modules built
* ✅ Display + MCU integration working
* ✅ ~30 FPS buffered animation achieved
* 🔲 Persistent pixel ID provisioning
* 🔲 Final master → node protocol implementation
* 🔲 Full 8 × 3 mechanical assembly
* 🔲 Clock mode with transitions
* 🔲 Expansion into additional visual modes

---

## 11. Inspiration & Acknowledgment

**Twenty-Four Times** is inspired in part by the work of **Humans Since 1982**, particularly their *ClockClock* series, which explores time through synchronized arrays of analog clock faces driven by mechanical motion.

Where *ClockClock* uses physical clock hands and stepper motors to form typographic digits, **Twenty-Four Times** extends the core idea back to a digital domain (ironically):

* Each clock face is rendered on a circular LCD
* Each unit supports **three independently controlled hands** which allows for forming digits without gaps, and would not be easy with physical hands
* Motion is software-defined rather than mechanically constrained
* Transitions and non-time-based visual modes are possible
* The system operates as a distributed, programmable animation field

This project is not intended as a replica, but as a **technical and conceptual evolution** of the same underlying question:

> *What happens when time becomes a material for choreography rather than measurement?*

### Related Projects

* **Humans Since 1982 — ClockClock**
  The original art installation using physical stepper motors and clock hands
  [https://www.humanssince1982.com/clockclock](https://www.humanssince1982.com/products/clockclock-24-white)

* **Erich Styger — MetaClockClock**
  An impressive DIY implementation using 120 stepper motors with LED rings, controlled by NXP LPC845 microcontrollers over RS-485. Multiple versions documented with detailed build logs, CNC enclosures, and open source firmware.
  [https://mcuoneclipse.com/2025/08/03/new-metaclockclock-combining-art-and-technology-in-clocks/](https://mcuoneclipse.com/2025/08/03/new-metaclockclock-combining-art-and-technology-in-clocks/)

---

## 12. License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

---

