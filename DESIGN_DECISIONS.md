# Design Decisions & Evolution Strategy

This document captures key architectural decisions and planned evolution paths for the **Twenty-Four Times** project that complement the core system description in the README.

---

## Communication Architecture Evolution

### Current State: Wired Serial Bus (README Spec)
The README describes a 3-wire bus topology with one-way serial communication. This remains the documented baseline architecture.

### Planned Implementation: ESP-NOW Wireless
For practical deployment and development flexibility, the system will use **ESP-NOW** as the primary communication protocol:

**Rationale:**
- **Development velocity**: Eliminates physical rewiring during iteration
- **Installation flexibility**: No complex bus wiring across 24 modules
- **Reliable broadcast**: Sub-10ms latency, 250-byte packets sufficient for command structure
- **Power efficiency**: Lower power than WiFi, suitable for always-on installation
- **Fallback compatibility**: Can coexist with or migrate to wired serial if needed

**Architecture:**
- Master controller broadcasts packets to all 24 pixels simultaneously
- Each pixel receives commands via ESP-NOW callback on dedicated FreeRTOS task
- Rendering loop checks packet queue non-blocking (~1µs overhead, zero FPS impact)
- Channel synchronization ensures master and pixels communicate reliably

**Trade-offs accepted:**
- Wireless introduces potential for interference (mitigated by ESP-NOW's reliability)
- Requires WiFi channel coordination between master and pixels
- Adds ~20KB firmware overhead vs pure wired implementation

---

## Master Controller Design

### Dual-Mode Operation: WiFi + ESP-NOW Bridge

The master controller will operate in **WIFI_AP_STA** mode to enable both external control and pixel communication:

**WiFi Interface (External Control):**
- HTTP server for PC/phone control via web interface
- RESTful API for programmatic control and automation
- WebSocket support for real-time monitoring and debugging
- Accessible from development machines and mobile devices

**ESP-NOW Interface (Pixel Control):**
- Broadcast commands to all 24 pixels
- Same WiFi channel as WiFi connection (required for coexistence)
- Low-latency command distribution (<10ms to all pixels)

**Control Flow:**
```
PC/Phone (WiFi) → Master ESP32 (HTTP → ESP-NOW) → 24 Pixels
```

**Benefits:**
- Wireless control during development and installation
- Web-based interface accessible from any device on network
- No USB tether required for operation
- Pixels remain on efficient ESP-NOW-only mode (lower power)

---

## Firmware Update Strategy

### Over-The-Air (OTA) Updates for Fleet Management

Managing 24 identical devices requires efficient update mechanisms that scale beyond USB serial flashing.

### Phase 1: ArduinoOTA (Development)
**Current approach for iteration:**
- Each pixel connects to WiFi and advertises OTA capability
- Devices identified by hostname (pixel-01 through pixel-24)
- Batch update script sequences through all devices
- ~12 minutes to update full fleet (30 seconds per device)

**Use case:** Frequent firmware updates during active development

### Phase 2: HTTP OTA (Production)
**Planned for installation deployment:**
- Master hosts firmware binary on local HTTP server
- Master sends "update available" command via ESP-NOW
- All 24 pixels download and flash simultaneously
- ~30 seconds total update time for entire fleet

**Use case:** Rapid deployment of updates to installed system

**Benefits:**
- Eliminates physical access requirement for updates
- Scales to full fleet in seconds vs minutes
- Can be triggered remotely via master's web interface
- Supports staged rollouts and rollback strategies

---

## Performance Optimization Path

### Current Performance: ESP32-C3 @ 30 FPS
Proof-of-concept validated on ESP32-C3 (160MHz, single-core, 400KB SRAM):
- 30 FPS sustained with 240×240 RGB565 framebuffer (115KB)
- Bottleneck: SPI transfer to display (~30ms per frame)
- Canvas operations: ~3ms per frame
- Transition math: <1ms per frame

### Target Hardware: ESP32-S3
Migration to ESP32-S3 provides significant headroom:
- **Dual-core 240MHz** vs single-core 160MHz (1.5× clock, 2× cores)
- **8MB PSRAM** vs 400KB SRAM (20× memory, enables advanced buffering)
- **Expected performance**: 40-60 FPS with current code, 60-120 FPS with optimization

**Optimization opportunities enabled by S3:**
- Canvas allocation in PSRAM (frees internal SRAM for packet queues)
- DMA SPI transfers (offload display updates to hardware)
- Dual-core rendering pipeline (Core 0: render, Core 1: transfer)
- Double/triple buffering for tear-free updates

**Decision:** Optimize for smooth motion (30-60 FPS) rather than maximum FPS, preserving power budget and thermal headroom.

---

## Pixel Identity & Provisioning

### NVS-Based Persistent Identity
Each pixel stores its index (0-23) in non-volatile storage, persisting across firmware updates.

**Planned provisioning flow:**
1. Flash identical firmware to all 24 devices
2. Enter provisioning mode (button press, serial command, or web interface)
3. Master assigns IDs sequentially or via spatial mapping
4. Each pixel stores ID in NVS and reboots
5. Normal operation uses stored ID to filter relevant commands

**Benefits:**
- Single firmware binary for all devices
- IDs survive OTA updates
- Flexible reassignment without reflashing
- Supports physical rearrangement without code changes

**Deferred decision:** Exact provisioning UI/UX pending hardware assembly and installation workflow validation.

---

## Graphics & Animation Philosophy

### Local Autonomy with Shared Intent
Each pixel is fully autonomous in rendering, receiving only high-level targets from master:

**Master sends:**
- Target angles for three hands
- Transition duration and easing type
- Opacity and color parameters

**Pixel decides:**
- Exact interpolation curve
- Frame timing and pacing
- Direction of rotation (CW/CCW)
- Local rendering optimizations

**Rationale:**
- Reduces bandwidth (targets vs frames: 80 bytes vs 115KB per update)
- Enables smooth motion even with packet loss
- Allows per-pixel variation and organic motion
- Scales to complex choreography without protocol changes

### Color & Opacity System
Transitions include synchronized color and opacity changes:
- Background and foreground colors transition smoothly
- Opacity blending creates fade effects
- Palette system supports curated color combinations
- All pixels transition colors in sync for visual coherence

---

## Future Evolution Paths

### Planned Capabilities (Post-Clock Mode)

**Enhanced Communication:**
- Per-pixel addressing for asymmetric animations
- Pixel-to-master feedback (health monitoring, acknowledgments)
- Multi-master coordination for larger installations

**Advanced Rendering:**
- Texture mapping and image display modes
- Particle systems and generative graphics
- Camera input and reactive visuals
- Audio-reactive motion

**Installation Features:**
- Ambient light sensing for brightness adaptation
- Motion detection for interactive modes
- Network time sync for precise clock accuracy
- Remote monitoring and diagnostics

**Architectural Flexibility:**
- System designed to support non-clock visual modes
- Protocol extensible to new command types
- Firmware architecture supports mode switching
- Web interface can evolve into full control dashboard

---

## Technology Stack Decisions

### Firmware
- **Framework:** Arduino (ESP-IDF compatibility layer)
- **Build system:** PlatformIO (reproducible builds, library management)
- **Graphics:** Adafruit GFX (proven, portable, sufficient for current needs)
- **Networking:** ESP-NOW (primary), WiFi (master only), ArduinoOTA (updates)

### Development Tools
- **IDE:** VS Code + PlatformIO extension
- **Version control:** Git + GitHub
- **3D design:** Fusion 360
- **Simulation:** Custom HTML/JavaScript canvas-based simulator

### Rationale
- **Arduino framework:** Rapid development, extensive library ecosystem, easy onboarding
- **PlatformIO:** Superior dependency management vs Arduino IDE, CI/CD ready
- **Adafruit GFX:** Adequate performance, well-documented, hardware-agnostic
- **ESP-NOW:** Best balance of performance, power, and simplicity for this use case

---

## Open Questions & Future Decisions

Items intentionally deferred until more context is available:

- **Power supply topology:** Centralized vs distributed regulation
- **Enclosure mounting:** Magnetic, clip, or screw-based attachment
- **Master controller form factor:** Dedicated enclosure vs integrated into frame
- **Web interface design:** Control-focused vs monitoring-focused vs both
- **Installation location:** Indoor vs outdoor (affects enclosure and power requirements)
- **Expansion beyond 24 pixels:** Protocol and power scaling considerations

These decisions will be made based on physical assembly experience and installation requirements.

---

*This document reflects the current state of architectural thinking and will evolve as the project progresses.*

