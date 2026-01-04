# ESP32 Pixel Display Discovery & Provisioning Protocol

## Overview
This document outlines the approach for discovering, identifying, and provisioning a network of ESP32-based pixel display slaves using a master controller and ESP-NOW wireless communication. The goal is to reliably discover all devices, assign unique IDs, and visually map each device to its physical location.

---

## 1. Discovery Phase

### Purpose
- Identify all active slave devices on the network -- a variable in the master app contains the number of slaves expected (24)
- Collect their unique MAC addresses

### Steps
1. **Master broadcasts a discovery command** to all slaves.
2. **Slaves respond one time only** with their MAC address after a random delay (e.g., 0–4 seconds) to avoid packet collisions. 
3. **Master listens** for responses for a fixed window (e.g., 5 seconds), collecting all received MAC addresses into a buffer.
4. **Master de-dupes** the buffer to create a distinct list of discovered MAC addresses.
5. **If not all expected devices are found:**
    - Master sends another discovery command, including an exclusion list of already discovered MACs.
    - Only undiscovered slaves respond, further reducing collision potential.
    - Repeat until all devices are found or a timeout occurs.

---

## 2. ID Assignment Phase

### Purpose
- Assign a unique integer ID (0–N) to each discovered slave
- Map each slave to its physical location via user interaction

### Steps
1. **Master enters assignment mode** and displays the list of discovered MAC addresses.
2. **User cycles through MAC addresses** using Prev/Next controls on the master UI.
3. **Master sends a highlight command** to the currently selected slave (by MAC), causing it to visually indicate selection (e.g., change color, flash, animate).
4. **User confirms the physical device** and presses Assign to link the current ID to the selected MAC.
5. **Master sends an assignment packet** to the selected slave, containing its assigned ID.
6. **Slave stores the ID in NVM** (e.g., Preferences or EEPROM).
7. **Repeat** for all devices until all are mapped and assigned.

---

## 3. Packet Structures

- **Discovery Command:** `{cmd: DISCOVERY, exclude: [list of MACs]}`
- **Discovery Response:** `{mac: [6 bytes]}`
- **Highlight Command:** `{cmd: HIGHLIGHT, mac: [6 bytes], color: [3 bytes]}`
- **Assignment Command:** `{cmd: ASSIGN, mac: [6 bytes], id: [1 byte]}`

---

## 4. Reliability & Scalability
- Use random delays and repeated responses to minimize packet collisions.
- Exclusion lists in discovery commands reduce unnecessary responses in subsequent rounds.
- Visual mapping ensures accurate physical-to-logical assignment.
- NVM storage allows persistent ID assignment across reboots.

---

## 5. User Interface
- Visual feedback on slave during discovery. 
  - Transmitting loop -- red bg, blank screen
  - Discovered (indicated by having mac addrses in ignore loop) -- green bg, blank screen
- Show current ID being assigned (e.g., "Assigning ID 7")
- Display MAC address of selected slave
- Controls: Prev, Next, Assign, Skip, Cancel
- Visual feedback on slave during assignment. Three possible states:
    - Idle (green bg with white question mark)
    - Selected (blue border with black bg with yellow question mark)
    - Assigned (green checkbox on black bg)

---

## 6. Example Workflow
1. Master initiates discovery.
2. Slaves respond with MAC addresses.
3. Master collects and de-dupes MACs.
4. Master enters assignment mode.
5. User cycles through MACs, visually identifies each slave, and assigns IDs.
6. Slaves store their assigned IDs in NVM.

---

## 7. Future Extensions
- Support for re-provisioning or re-mapping devices
