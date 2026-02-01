#ifndef ORBIT_TIME_ANIMATION_H
#define ORBIT_TIME_ANIMATION_H

#include <Arduino.h>
#include <ESPNowComm.h>
#include <TFT_eSPI.h>

// Orbit Time Animation - Continuous orbital rotation with periodic time display
// All pixels have hands rotating at different speeds like planetary orbits
// Periodically transitions to show current time digits

// External references (provided by master.cpp)
extern TFT_eSPI tft;
extern unsigned long lastCommandTime;
extern unsigned long lastPingTime;
void sendPing();  // External function to ping pixels
uint8_t getCurrentMinute();  // Get current minute from real-time clock
String getCurrentTimeString();  // Get formatted time string (e.g., "12:35 PM")

// Digit pattern support (from master.cpp)
extern DigitPattern digitPatterns[];
extern const uint8_t digit1PixelIds[6];  // Left digit pixels
extern const uint8_t digit2PixelIds[6];  // Right digit pixels

// Color definitions (from master.cpp)
#define COLOR_BG      TFT_BLACK
#define COLOR_TEXT    TFT_WHITE
#define COLOR_ACCENT  TFT_GREEN

// ===== CONFIGURATION =====

// Orbit speeds (degrees per second for continuous rotation)
const float ORBIT_SPEED_SLOW = 30.0f;    // Hand 1 - like hour hand (12 sec per revolution)
const float ORBIT_SPEED_MEDIUM = 60.0f;  // Hand 2 - like minute hand (6 sec per revolution)
const float ORBIT_SPEED_FAST = 120.0f;   // Hand 3 - like second hand (3 sec per revolution)

// Timing constants (use unique names to avoid conflicts with fluid_time.h)
const unsigned long ORBIT_UPDATE_INTERVAL = 2000;  // Update orbit commands every 2 seconds
const unsigned long ORBIT_TIME_DISPLAY_INTERVAL = 60000; // Show time every 60 seconds
const unsigned long ORBIT_TIME_HOLD_DURATION = 6000;     // Hold time display for 6 seconds
const unsigned long ORBIT_TIME_TRANSITION_DURATION = 2000; // Transition to/from time display (2 sec)

// ===== STATE TRACKING =====

// Phase tracking
enum OrbitPhase {
  ORBIT_ORBITING,            // Continuous orbital rotation
  ORBIT_TRANSITIONING_TO_TIME, // Transitioning from orbit to time display
  ORBIT_HOLDING_TIME,        // Holding time display
  ORBIT_RETURNING_TO_ORBIT   // Transitioning from time back to orbit
};

OrbitPhase orbitPhase = ORBIT_ORBITING;
unsigned long lastOrbitUpdate = 0;
unsigned long lastOrbitTimeDisplay = 0;  // Unique name to avoid conflict
unsigned long orbitPhaseStartTime = 0;
uint8_t orbitCurrentMinute = 0;  // Unique name to avoid conflict
uint8_t orbitColorIndex = 0;

// Track current orbit angles for each pixel (for smooth transitions)
struct PixelOrbitState {
  float angle1, angle2, angle3;  // Current angles
  float speed1, speed2, speed3;  // Rotation speeds (deg/sec)
};

PixelOrbitState pixelOrbits[24];  // One per pixel

// ===== HELPER FUNCTIONS =====

// Initialize orbit states with random starting angles and slight speed variations
void initializeOrbits() {
  for (int i = 0; i < 24; i++) {
    pixelOrbits[i].angle1 = random(360);
    pixelOrbits[i].angle2 = random(360);
    pixelOrbits[i].angle3 = random(360);

    // Add slight speed variation (±20%) for visual interest
    float variation1 = 0.8f + (random(41) / 100.0f);  // 0.8 to 1.2
    float variation2 = 0.8f + (random(41) / 100.0f);
    float variation3 = 0.8f + (random(41) / 100.0f);

    pixelOrbits[i].speed1 = ORBIT_SPEED_SLOW * variation1;
    pixelOrbits[i].speed2 = ORBIT_SPEED_MEDIUM * variation2;
    pixelOrbits[i].speed3 = ORBIT_SPEED_FAST * variation3;
  }
}

// Update orbit angles based on elapsed time
void updateOrbitAngles(float elapsedSeconds) {
  for (int i = 0; i < 24; i++) {
    pixelOrbits[i].angle1 += pixelOrbits[i].speed1 * elapsedSeconds;
    pixelOrbits[i].angle2 += pixelOrbits[i].speed2 * elapsedSeconds;
    pixelOrbits[i].angle3 += pixelOrbits[i].speed3 * elapsedSeconds;

    // Wrap angles to 0-360 range
    while (pixelOrbits[i].angle1 >= 360.0f) pixelOrbits[i].angle1 -= 360.0f;
    while (pixelOrbits[i].angle2 >= 360.0f) pixelOrbits[i].angle2 -= 360.0f;
    while (pixelOrbits[i].angle3 >= 360.0f) pixelOrbits[i].angle3 -= 360.0f;
  }
}

// Send orbit commands to all pixels
void sendOrbitCommands() {
  ESPNowPacket packet;
  packet.angleCmd.command = CMD_SET_ANGLES;
  packet.angleCmd.clearTargetMask();

  // Target all pixels
  for (int i = 0; i < 24; i++) {
    packet.angleCmd.setTargetPixel(i);
  }

  // Set angles for each pixel
  for (int i = 0; i < 24; i++) {
    packet.angleCmd.setPixelAngles(
      i,
      pixelOrbits[i].angle1,
      pixelOrbits[i].angle2,
      pixelOrbits[i].angle3,
      DIR_SHORTEST,
      DIR_SHORTEST,
      DIR_SHORTEST
    );

    packet.angleCmd.setPixelStyle(i, orbitColorIndex, 255);  // Full brightness
  }

  // Set transition - use LINEAR for smooth continuous motion
  packet.angleCmd.transition = TRANSITION_LINEAR;
  packet.angleCmd.duration = floatToDuration(ORBIT_UPDATE_INTERVAL / 1000.0f);

  ESPNowComm::sendPacket(&packet, sizeof(AngleCommandPacket));
  lastCommandTime = millis();
}

// Send time digit display command
void sendOrbitTimeDisplay() {
  // Use consolidated digit display function
  sendTwoDigitTime(
    orbitCurrentMinute,
    orbitColorIndex,
    TRANSITION_EASE_IN_OUT,
    ORBIT_TIME_TRANSITION_DURATION / 1000.0f
  );
}

// Update display
void updateOrbitTimeDisplay() {
  tft.fillScreen(COLOR_BG);
  tft.setTextColor(COLOR_ACCENT, COLOR_BG);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("ORBIT TIME");

  // Display current time in top-right
  tft.setTextSize(1);
  tft.setTextColor(TFT_CYAN, COLOR_BG);
  tft.setTextDatum(TR_DATUM);
  tft.drawString(getCurrentTimeString(), 310, 10);
  tft.setTextDatum(TL_DATUM);

  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setTextSize(1);

  // Show current phase
  tft.setCursor(10, 35);
  tft.print("Phase: ");
  switch (orbitPhase) {
    case ORBIT_ORBITING:
      tft.println("Orbiting");
      break;
    case ORBIT_TRANSITIONING_TO_TIME:
      tft.println("-> Time");
      break;
    case ORBIT_HOLDING_TIME:
      tft.setTextColor(TFT_CYAN, COLOR_BG);
      tft.print("Showing: ");
      tft.print(orbitCurrentMinute / 10);
      tft.println(orbitCurrentMinute % 10);
      tft.setTextColor(COLOR_TEXT, COLOR_BG);
      break;
    case ORBIT_RETURNING_TO_ORBIT:
      tft.println("-> Orbit");
      break;
  }

  // Show orbit speeds
  tft.setCursor(10, 50);
  tft.print("Hand 1: ");
  tft.print(ORBIT_SPEED_SLOW, 0);
  tft.println(" deg/s");

  tft.setCursor(10, 65);
  tft.print("Hand 2: ");
  tft.print(ORBIT_SPEED_MEDIUM, 0);
  tft.println(" deg/s");

  tft.setCursor(10, 80);
  tft.print("Hand 3: ");
  tft.print(ORBIT_SPEED_FAST, 0);
  tft.println(" deg/s");

  // Show color
  tft.setCursor(10, 100);
  tft.print("Color: ");
  tft.println(orbitColorIndex);

  // Back button
  tft.fillRoundRect(10, 210, 60, 25, 4, TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.setCursor(20, 217);
  tft.println("Back");
}

// Handle touch input
void handleOrbitTimeTouch(uint16_t x, uint16_t y) {
  // Back button (10, 210, 60, 25)
  if (x >= 10 && x <= 70 && y >= 210 && y <= 235) {
    // Return to animations menu
    extern ControlMode currentMode;
    extern void drawAnimationsScreen();
    currentMode = (ControlMode)1;  // MODE_ANIMATIONS
    drawAnimationsScreen();
  }
}

// Main loop handler
void handleOrbitTimeLoop(unsigned long currentTime) {
  static bool initialized = false;

  // Initialize orbits on first run
  if (!initialized) {
    initializeOrbits();
    updateOrbitTimeDisplay();
    initialized = true;
    lastOrbitUpdate = currentTime;
    lastOrbitTimeDisplay = currentTime;
    orbitPhaseStartTime = currentTime;
  }

  // Send periodic pings to keep pixels alive (every 3 seconds)
  if (currentTime - lastPingTime >= 3000) {
    sendPing();
    lastPingTime = currentTime;
  }

  // Handle state machine
  switch (orbitPhase) {
    case ORBIT_ORBITING: {
      // Check if it's time to show the time
      if (currentTime - lastOrbitTimeDisplay >= ORBIT_TIME_DISPLAY_INTERVAL) {
        // Transition to time display
        orbitCurrentMinute = getCurrentMinute();
        orbitPhase = ORBIT_TRANSITIONING_TO_TIME;
        orbitPhaseStartTime = currentTime;
        sendOrbitTimeDisplay();
        updateOrbitTimeDisplay();

        Serial.print("Orbit showing time: ");
        Serial.print(orbitCurrentMinute / 10);
        Serial.println(orbitCurrentMinute % 10);
      } else {
        // Continue orbiting - update every 2 seconds
        if (currentTime - lastOrbitUpdate >= ORBIT_UPDATE_INTERVAL) {
          float elapsedSeconds = (currentTime - lastOrbitUpdate) / 1000.0f;
          updateOrbitAngles(elapsedSeconds);
          sendOrbitCommands();
          lastOrbitUpdate = currentTime;
        }
      }
      break;
    }

    case ORBIT_TRANSITIONING_TO_TIME: {
      // Wait for transition to complete
      if (currentTime - orbitPhaseStartTime >= ORBIT_TIME_TRANSITION_DURATION) {
        orbitPhase = ORBIT_HOLDING_TIME;
        orbitPhaseStartTime = currentTime;
        updateOrbitTimeDisplay();
      }
      break;
    }

    case ORBIT_HOLDING_TIME: {
      // Hold time display
      if (currentTime - orbitPhaseStartTime >= ORBIT_TIME_HOLD_DURATION) {
        orbitPhase = ORBIT_RETURNING_TO_ORBIT;
        orbitPhaseStartTime = currentTime;
        lastOrbitTimeDisplay = currentTime;  // Reset time display timer
        lastOrbitUpdate = currentTime;  // Reset orbit update timer
        sendOrbitCommands();  // Resume orbiting
        updateOrbitTimeDisplay();

        Serial.println("Returning to orbit");
      }
      break;
    }

    case ORBIT_RETURNING_TO_ORBIT: {
      // Wait for transition to complete
      if (currentTime - orbitPhaseStartTime >= ORBIT_TIME_TRANSITION_DURATION) {
        orbitPhase = ORBIT_ORBITING;
        updateOrbitTimeDisplay();
      }
      break;
    }
  }
}

#endif // ORBIT_TIME_ANIMATION_H
