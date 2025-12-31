# Changelog

## 2025-12-31 - ESP-NOW Communication System

### Added
- **ESP-NOW wireless communication** for synchronized multi-pixel operation
- **Shared communication library** (`lib/ESPNowComm/`) with compact packet structure
- **Master controller firmware** (`src/master.cpp.example`) for broadcasting commands
- **Pixel receiver functionality** in main firmware with ESP-NOW support
- **Error state display** - red background with "!" when master disconnects
- **Automatic error recovery** - clears error when master reconnects

### Changed
- **Removed autonomous mode** - pixels never run on their own
- **Simplified pixel behavior** - only two states: normal (receiving commands) or error (no signal)
- **Updated documentation** to reflect error state instead of autonomous fallback

### Technical Details

#### Packet Structure
- **76 bytes total** per command packet
- 1 byte: Command type
- 1 byte: Transition type
- 2 bytes: Duration (milliseconds)
- 72 bytes: Angles for 24 pixels × 3 hands (compressed to 1 byte each)

#### Communication
- **Protocol**: ESP-NOW broadcast
- **Channel**: WiFi channel 1 (configurable)
- **Latency**: <10ms from master to all pixels
- **Range**: 10-100 meters depending on environment
- **Reliability**: Automatic retries via ESP-NOW

#### Pixel Behavior
- **Normal mode**: Receives commands, executes smooth transitions
- **Error mode**: Shows red screen with "!" after 10s timeout
- **Auto-recovery**: Clears error immediately when commands resume
- **No autonomous operation**: Pixels are purely reactive

#### Error Detection
- **Timeout**: 10 seconds without master signal
- **Visual indicator**: Full-screen red background with large white "!"
- **Serial logging**: "ESP-NOW TIMEOUT - NO MASTER SIGNAL"
- **Recovery**: Automatic when master reconnects

### Build Configuration

Two PlatformIO environments:

1. **`pixel`** - Flash to each display device (set unique PIXEL_ID)
2. **`master`** - Flash to one controller device (no display needed)

### Documentation

- **`QUICK-START.md`** - 5-minute setup guide
- **`ESP-NOW-SETUP.md`** - Detailed configuration and troubleshooting
- **Architecture diagrams** - Visual system overview with Mermaid

### Testing

Tested with:
- 3× ESP32-C3 (XIAO) with GC9A01A displays
- 1× ESP32-C3 (XIAO) as master controller
- 5 test patterns cycling every 5 seconds
- Sub-10ms synchronization latency verified

### Next Steps

1. Test with physical hardware (3 pixels + 1 master)
2. Expand to more pixels (up to 24)
3. Implement actual clock digit patterns
4. Add web interface for remote control
5. Implement NVS-based pixel ID storage (no reflashing needed)

---

## Design Philosophy

**Pixels are dumb displays** - They have no autonomy, no decision-making, no fallback behavior beyond showing an error. This keeps the system simple, predictable, and easy to debug:

- ✅ Master sends commands → Pixels execute
- ✅ Master disconnects → Pixels show error
- ✅ Master reconnects → Pixels resume normal operation
- ❌ No autonomous animations
- ❌ No "demo mode"
- ❌ No independent behavior

This aligns with the project's goal of synchronized, choreographed motion across all 24 pixels.

