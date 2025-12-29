# **Twenty-Four Times**

## Overview

**Twenty-Four Times** is a modular kinetic–digital art piece composed of **24 circular display nodes**, arranged in an **8 × 3 matrix**.
Each node renders animated geometry—most commonly three rotating “hands”—that collectively form large-scale numeric digits and choreographed transitions.

While rooted in timekeeping, the system is designed as a **general distributed animation platform**, capable of evolving into non-clock visual modes over time.

The name *Twenty-Four Times* intentionally reflects both:

* the number of clocks
* the multiplication of time, motion, and state across space

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
| **Communication**     | One-way wired serial broadcast (3-wire bus)                              |

### Why this architecture

* Guaranteed smooth animation per display
* No shared SPI bandwidth
* Simple wiring
* Identical firmware on all nodes

---

## 2. Hardware Specification

### 2.1 Display

* Product link:
  [https://www.amazon.com/YELUFT-Interface-Self-Luminous-Raspberry-Pre-Soldered/dp/B0F21J42WD](https://www.amazon.com/YELUFT-Interface-Self-Luminous-Raspberry-Pre-Soldered/dp/B0F21J42WD)

| Property    | Specification       |
| ----------- | ------------------- |
| Type        | 1.28" Round IPS LCD |
| Controller  | GC9A01              |
| Resolution  | 240 × 240           |
| Interface   | SPI (write-only)    |
| Color Depth | RGB565              |

---

### 2.2 Microcontroller (Per Pixel)

* Product link:
  [https://www.amazon.com/dp/B0D2CY4Y5H](https://www.amazon.com/dp/B0D2CY4Y5H)

| Property    | Specification                                              |
| ----------- | ---------------------------------------------------------- |
| MCU         | **ESP32-S3**                                               |
| CPU         | Dual core, 240 MHz                                         |
| Rationale   | Best performance headroom for animation math and buffering |
| Form Factor | Small enough to fit inside each pixel enclosure            |

> ESP32-C3 was evaluated and proven viable, but ESP32-S3 is selected for its dual core and PSRAM, to allow for concurrent tasks and larger framebuffers.

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

Each pixel module exposes **three conductors**:

| Line      | Purpose             |
| --------- | ------------------- |
| **VCC**   | 5 V power           |
| **GND**   | Ground              |
| **COMMS** | One-way serial data |

* 24 identical 3-pin connections
* Dupont-compatible
* Bus topology inside main enclosure

---

## 4. Pixel Identity & Configuration

Pixel identity is **not hard-coded** and **not strap-based**.

* Each node stores its assigned index in **non-volatile flash (NVS)**
* IDs persist across firmware updates
* All nodes run **identical firmware**

### Provisioning concept

* A configuration or provisioning mode assigns IDs
* Assignment may be initiated by:

  * the master controller
  * a temporary configuration MCU
  * or a dedicated setup command

Exact provisioning mechanics are intentionally deferred until hardware assembly is complete.

---

## 5. Communication Protocol

### Design Goals

* Extremely compact
* Deterministic
* No clocks, timestamps, or frame counters
* Scales to non-clock animations

### Core Concept

Each broadcast frame defines:

* **Target angles** for all hands
* **A transition type** shared by all hands
* **A timing profile** selected from a predefined table

#### Conceptual payload

```
[ transition_id, duration_id, angles[72] ]
```

Where:

* 24 pixels × 3 hands = **72 angles**
* Angles are absolute targets
* Each pixel updates only its own three values
* Transition logic is fully local

This keeps bandwidth low while allowing complex, expressive motion.

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

The repository includes an **HTML + JavaScript simulator** used to:

* prototype layouts
* test animation ideas
* design transition behavior
* validate legibility and motion

This simulator:

* predates hardware completion
* evolves alongside firmware
* acts as a reference for animation logic

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

For reference and context, see:

* Humans Since 1982 — ClockClock
  [https://www.humanssince1982.com/clockclock](https://www.humanssince1982.com/clockclock)





