#ifndef SCATTER_FLOCK_ANIMATION_H
#define SCATTER_FLOCK_ANIMATION_H

#include <Arduino.h>
#include <ESPNowComm.h>
#include <TFT_eSPI.h>

// Scatter Flock Animation - Emergent chaos-to-order behavior
// Hands scatter to random positions, then swarm together into patterns, then scatter again
// Like flocking birds or schooling fish
// Periodically swarms into time digit shapes

// External references (provided by master.cpp)
extern TFT_eSPI tft;
extern unsigned long lastCommandTime;
extern unsigned long lastPingTime;
void sendPing();  // External function to ping pixels
uint8_t getCurrentMinute();  // Get current minute from real-time clock
String getCurrentTimeString();  // Get formatted time string

// Use digit display library for time digits
#include "digit_display.h"

// Color definitions (from master.cpp)
#define COLOR_BG      TFT_BLACK
#define COLOR_TEXT    TFT_WHITE
#define COLOR_ACCENT  TFT_GREEN

// ===== CONFIGURATION =====

// Swarm pattern types
enum SwarmPattern {
  SWARM_UNIFIED,          // All hands point same direction
  SWARM_WAVE,             // Hands form wave pattern across grid
  SWARM_SPIRAL,           // Hands form spiral pattern
  SWARM_RADIAL,           // Mercedes logo - 3 hands evenly spaced
  SWARM_TWO_HAND_SWIRL,   // 2 hands 180° apart, swirl across pixels
  SWARM_TWO_HAND_RANDOM   // 2 hands random degrees apart, pattern across pixels
};

// Twitch pattern types (chaos behavior)
enum TwitchPattern {
  TWITCH_THREE_RANDOM,      // 3 hands, random positions
  TWITCH_TWO_180,           // 2 hands 180° apart, snap to 0/45/90/135
  TWITCH_TWO_RANDOM_SNAP,   // 2 hands random degrees apart, snap to 0/45/90/135
  TWITCH_ONE_HAND           // 1 hand (all 3 same angle), snap to 0/45/90/135
};

// ===== STATE TRACKING =====

enum ScatterPhase {
  SCATTER_CHAOTIC,      // Random scattered positions, occasional twitching
  SCATTER_CONVERGING,   // Swarming toward unified pattern
  SCATTER_UNIFIED,      // Holding unified pattern
  SCATTER_SHOWING_TIME, // Displaying time digits
  SCATTER_HOLDING_TIME  // Holding time display
};

ScatterPhase scatterPhase = SCATTER_CHAOTIC;
unsigned long phaseStartTime = 0;
unsigned long lastTwitchTime = 0;
unsigned long lastTimeDisplayTrigger = 0;

// Timing parameters
const unsigned long CHAOTIC_DURATION_MIN = 3000;   // Min time in chaos (3s)
const unsigned long CHAOTIC_DURATION_MAX = 8000;   // Max time in chaos (8s)
const unsigned long TWITCH_INTERVAL = 500;          // Twitch every 0.5s while chaotic
const unsigned long UNIFIED_DURATION = 2000;        // Hold unified for 2s
const unsigned long TIME_DISPLAY_INTERVAL = 60000;  // Show time every 60s
const unsigned long SCATTER_TIME_HOLD_DURATION = 6000;      // Hold time for 6s
const float CONVERGE_DURATION = 2.0f;               // 2 seconds to converge
const float SCATTER_DURATION = 1.5f;                // 1.5 seconds to scatter

// Current state
SwarmPattern currentSwarmPattern;
TwitchPattern currentTwitchPattern;
unsigned long chaoticDuration;  // Randomized each cycle
uint8_t scatterCurrentMinute = 0;
bool scatterShouldShowTimeNext = true;  // Start with time display
float twoHandSeparation = 0;  // For TWO_HAND_RANDOM patterns
unsigned long lastRotationTime = 0;  // For rotating gears
float currentRotation = 0;  // Current rotation angle for gears
const unsigned long ROTATION_INTERVAL = 50;  // Update rotation every 50ms

// ===== HELPER FUNCTIONS =====

// Get random swarm pattern
SwarmPattern getRandomSwarmPattern() {
  return (SwarmPattern)random(6);  // 0-5
}

// Get random twitch pattern
TwitchPattern getRandomTwitchPattern() {
  return (TwitchPattern)random(4);  // 0-3
}

// Get swarm pattern name for display
const char* getSwarmPatternName(SwarmPattern pattern) {
  switch (pattern) {
    case SWARM_UNIFIED: return "Unified";
    case SWARM_WAVE: return "Wave";
    case SWARM_SPIRAL: return "Spiral";
    case SWARM_RADIAL: return "Radial Gears";
    case SWARM_TWO_HAND_SWIRL: return "2-Hand Swirl";
    case SWARM_TWO_HAND_RANDOM: return "2-Hand Random";
    default: return "Unknown";
  }
}

// Snap angle to nearest 45-degree increment
float snapTo45(float angle) {
  int increment = round(angle / 45.0f);
  return increment * 45.0f;
}

// Calculate angle for swarm pattern based on pixel position
void getSwarmAngles(uint8_t pixelId, SwarmPattern pattern, float& angle1, float& angle2, float& angle3) {
  // Convert pixel ID to grid position (3 rows x 8 columns)
  uint8_t row = pixelId / 8;
  uint8_t col = pixelId % 8;

  // Normalized position (0.0 to 1.0)
  float normCol = col / 7.0f;
  float normRow = row / 2.0f;

  // Center offset
  float centerCol = col - 3.5f;  // -3.5 to +3.5
  float centerRow = row - 1.0f;   // -1.0 to +1.0
  float distFromCenter = sqrt(centerCol * centerCol + centerRow * centerRow);
  float angleToCenter = atan2(centerRow, centerCol) * 180.0f / PI;

  switch (pattern) {
    case SWARM_UNIFIED:
      // All hands point same direction
      angle1 = 45.0f;
      angle2 = 135.0f;
      angle3 = 225.0f;
      break;

    case SWARM_WAVE:
      // Wave pattern across columns
      angle1 = normCol * 360.0f;
      angle2 = (normCol * 360.0f) + 120.0f;
      angle3 = (normCol * 360.0f) + 240.0f;
      break;

    case SWARM_SPIRAL:
      // Spiral pattern based on distance from center
      angle1 = distFromCenter * 60.0f + angleToCenter;
      angle2 = distFromCenter * 60.0f + angleToCenter + 120.0f;
      angle3 = distFromCenter * 60.0f + angleToCenter + 240.0f;
      break;

    case SWARM_RADIAL:
      // Mercedes logo - 3 hands evenly spaced (120° apart)
      // Add current rotation for animated gears effect
      angle1 = angleToCenter + currentRotation;
      angle2 = angleToCenter + currentRotation + 120.0f;
      angle3 = angleToCenter + currentRotation + 240.0f;
      break;

    case SWARM_TWO_HAND_SWIRL:
      // 2 hands 180° apart, swirl pattern across pixels
      // Third hand hidden (same as first)
      angle1 = (col * 45.0f) + (row * 30.0f);  // Swirl offset
      angle2 = angle1 + 180.0f;
      angle3 = angle1;  // Hide third hand
      break;

    case SWARM_TWO_HAND_RANDOM:
      // 2 hands with random separation, pattern across pixels
      // Third hand hidden (same as first)
      angle1 = (col * 40.0f) + (row * 25.0f);  // Pattern offset
      angle2 = angle1 + twoHandSeparation;
      angle3 = angle1;  // Hide third hand
      break;
  }

  // Normalize angles to 0-360
  while (angle1 < 0) angle1 += 360.0f;
  while (angle2 < 0) angle2 += 360.0f;
  while (angle3 < 0) angle3 += 360.0f;
  while (angle1 >= 360.0f) angle1 -= 360.0f;
  while (angle2 >= 360.0f) angle2 -= 360.0f;
  while (angle3 >= 360.0f) angle3 -= 360.0f;
}

// Send scatter command - random positions for all pixels
void sendScatterPattern() {
  ESPNowPacket packet;
  packet.angleCmd.command = CMD_SET_ANGLES;
  packet.angleCmd.clearTargetMask();  // Target all pixels
  packet.angleCmd.transition = TRANSITION_EASE_IN_OUT;
  packet.angleCmd.duration = floatToDuration(SCATTER_DURATION);

  uint8_t colorIndex = getRandomColorIndex();

  // Each pixel gets random angles for chaotic scatter
  for (int i = 0; i < MAX_PIXELS; i++) {
    float angle1 = getRandomAngle();
    float angle2 = getRandomAngle();
    float angle3 = getRandomAngle();

    RotationDirection dir1 = (random(2) == 0) ? DIR_CW : DIR_CCW;
    RotationDirection dir2 = (random(2) == 0) ? DIR_CW : DIR_CCW;
    RotationDirection dir3 = (random(2) == 0) ? DIR_CW : DIR_CCW;

    packet.angleCmd.setPixelAngles(i, angle1, angle2, angle3, dir1, dir2, dir3);
    packet.angleCmd.setPixelStyle(i, colorIndex, 255);
  }

  ESPNowComm::sendPacket(&packet, sizeof(AngleCommandPacket));
  lastCommandTime = millis();

  Serial.println("Scatter: Chaos unleashed");
}

// Send twitch command - small random movements while chaotic
void sendTwitchPattern() {
  ESPNowPacket packet;
  packet.angleCmd.command = CMD_SET_ANGLES;
  packet.angleCmd.clearTargetMask();
  packet.angleCmd.transition = TRANSITION_LINEAR;
  packet.angleCmd.duration = floatToDuration(0.3f);  // Quick twitch

  uint8_t colorIndex = getRandomColorIndex();

  // Different twitch behaviors based on current pattern
  switch (currentTwitchPattern) {
    case TWITCH_THREE_RANDOM: {
      // Original: 3 hands, random positions
      for (int i = 0; i < MAX_PIXELS; i++) {
        float angle1 = getRandomAngle();
        float angle2 = getRandomAngle();
        float angle3 = getRandomAngle();
        RotationDirection dir = DIR_SHORTEST;
        packet.angleCmd.setPixelAngles(i, angle1, angle2, angle3, dir, dir, dir);
        packet.angleCmd.setPixelStyle(i, colorIndex, 255);
      }
      break;
    }

    case TWITCH_TWO_180: {
      // 2 hands 180° apart, snap to 0/45/90/135
      for (int i = 0; i < MAX_PIXELS; i++) {
        float angle1 = snapTo45(getRandomAngle());
        float angle2 = angle1 + 180.0f;
        float angle3 = angle1;  // Hide third hand
        RotationDirection dir = DIR_SHORTEST;
        packet.angleCmd.setPixelAngles(i, angle1, angle2, angle3, dir, dir, dir);
        packet.angleCmd.setPixelStyle(i, colorIndex, 255);
      }
      break;
    }

    case TWITCH_TWO_RANDOM_SNAP: {
      // 2 hands random degrees apart, snap to 0/45/90/135
      float separation = random(4, 10) * 45.0f;  // 180-450 degrees
      for (int i = 0; i < MAX_PIXELS; i++) {
        float angle1 = snapTo45(getRandomAngle());
        float angle2 = angle1 + separation;
        float angle3 = angle1;  // Hide third hand
        RotationDirection dir = DIR_SHORTEST;
        packet.angleCmd.setPixelAngles(i, angle1, angle2, angle3, dir, dir, dir);
        packet.angleCmd.setPixelStyle(i, colorIndex, 255);
      }
      break;
    }

    case TWITCH_ONE_HAND: {
      // 1 hand (all 3 same angle), snap to 0/45/90/135
      for (int i = 0; i < MAX_PIXELS; i++) {
        float angle = snapTo45(getRandomAngle());
        RotationDirection dir = DIR_SHORTEST;
        packet.angleCmd.setPixelAngles(i, angle, angle, angle, dir, dir, dir);
        packet.angleCmd.setPixelStyle(i, colorIndex, 255);
      }
      break;
    }
  }

  ESPNowComm::sendPacket(&packet, sizeof(AngleCommandPacket));
  lastCommandTime = millis();
}

// Send converge/swarm command
void sendConvergePattern(SwarmPattern pattern) {
  // Initialize pattern-specific parameters
  if (pattern == SWARM_TWO_HAND_RANDOM) {
    twoHandSeparation = random(90, 270);  // Random separation between hands
  }
  if (pattern == SWARM_RADIAL) {
    currentRotation = 0;  // Reset rotation
    lastRotationTime = millis();
  }

  ESPNowPacket packet;
  packet.angleCmd.command = CMD_SET_ANGLES;
  packet.angleCmd.clearTargetMask();
  packet.angleCmd.transition = TRANSITION_EASE_IN_OUT;
  packet.angleCmd.duration = floatToDuration(CONVERGE_DURATION);

  uint8_t colorIndex = getRandomColorIndex();

  // Each pixel converges to its swarm position
  for (int i = 0; i < MAX_PIXELS; i++) {
    float angle1, angle2, angle3;
    getSwarmAngles(i, pattern, angle1, angle2, angle3);

    RotationDirection dir = DIR_SHORTEST;  // Smooth convergence

    packet.angleCmd.setPixelAngles(i, angle1, angle2, angle3, dir, dir, dir);
    packet.angleCmd.setPixelStyle(i, colorIndex, 255);
  }

  ESPNowComm::sendPacket(&packet, sizeof(AngleCommandPacket));
  lastCommandTime = millis();

  Serial.print("Swarm: Converging to ");
  Serial.println(getSwarmPatternName(pattern));
}

// Update rotating gears (for RADIAL pattern during UNIFIED phase)
void updateRotatingGears() {
  currentRotation += 2.0f;  // Rotate 2 degrees
  if (currentRotation >= 360.0f) currentRotation -= 360.0f;

  ESPNowPacket packet;
  packet.angleCmd.command = CMD_SET_ANGLES;
  packet.angleCmd.clearTargetMask();
  packet.angleCmd.transition = TRANSITION_LINEAR;
  packet.angleCmd.duration = floatToDuration(0.1f);  // Smooth rotation

  uint8_t colorIndex = 2;  // Keep same color during rotation

  for (int i = 0; i < MAX_PIXELS; i++) {
    float angle1, angle2, angle3;
    getSwarmAngles(i, SWARM_RADIAL, angle1, angle2, angle3);

    RotationDirection dir = DIR_CW;  // All rotate clockwise

    packet.angleCmd.setPixelAngles(i, angle1, angle2, angle3, dir, dir, dir);
    packet.angleCmd.setPixelStyle(i, colorIndex, 255);
  }

  ESPNowComm::sendPacket(&packet, sizeof(AngleCommandPacket));
  lastCommandTime = millis();
}

// Send time digit pattern using digit_display library
void sendTimePattern(uint8_t minute) {
  uint8_t leftDigit = minute / 10;
  uint8_t rightDigit = minute % 10;
  uint8_t colorIndex = 2;  // Cyan for time display

  // Use the consolidated digit display function
  sendTwoDigitDisplay(
    leftDigit,
    rightDigit,
    colorIndex,
    TRANSITION_EASE_IN_OUT,
    CONVERGE_DURATION,
    DIR_SHORTEST,
    DIR_SHORTEST,
    DIR_SHORTEST,
    false  // Use unified directions, not randomized
  );

  Serial.print("Swarm: Time display ");
  Serial.print(leftDigit);
  Serial.println(rightDigit);
}

// Update display
void updateScatterFlockDisplay() {
  tft.fillScreen(COLOR_BG);
  tft.setTextColor(COLOR_ACCENT, COLOR_BG);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("SCATTER FLOCK");

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
  switch (scatterPhase) {
    case SCATTER_CHAOTIC:
      tft.setTextColor(TFT_RED, COLOR_BG);
      tft.println("CHAOS");
      break;
    case SCATTER_CONVERGING:
      tft.setTextColor(TFT_YELLOW, COLOR_BG);
      tft.println("CONVERGING");
      break;
    case SCATTER_UNIFIED:
      tft.setTextColor(TFT_GREEN, COLOR_BG);
      tft.println("UNIFIED");
      break;
    case SCATTER_SHOWING_TIME:
    case SCATTER_HOLDING_TIME:
      tft.setTextColor(TFT_CYAN, COLOR_BG);
      tft.print("TIME: ");
      tft.print(scatterCurrentMinute / 10);
      tft.println(scatterCurrentMinute % 10);
      break;
  }

  tft.setTextColor(COLOR_TEXT, COLOR_BG);

  // Show swarm pattern if unified
  if (scatterPhase == SCATTER_UNIFIED) {
    tft.setCursor(10, 50);
    tft.print("Pattern: ");
    tft.println(getSwarmPatternName(currentSwarmPattern));
  }

  // Show next time display countdown
  if (scatterPhase != SCATTER_SHOWING_TIME && scatterPhase != SCATTER_HOLDING_TIME) {
    unsigned long timeSinceLastDisplay = millis() - lastTimeDisplayTrigger;
    unsigned long timeUntilNext = TIME_DISPLAY_INTERVAL - timeSinceLastDisplay;

    tft.setCursor(10, 70);
    tft.print("Next time: ");
    tft.print(timeUntilNext / 1000);
    tft.println("s");
  }

  // Back button
  tft.fillRoundRect(10, 210, 60, 25, 4, TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.setTextSize(1);
  tft.setTextDatum(MC_DATUM);  // Middle center alignment
  tft.drawString("Back", 40, 222);  // Center of button (10+30, 210+12)
  tft.setTextDatum(TL_DATUM);  // Reset to top-left
}

// Main loop handler
void handleScatterFlockLoop(unsigned long currentTime) {
  // Send periodic pings
  if (currentTime - lastPingTime >= 3000) {
    sendPing();
    lastPingTime = currentTime;
  }

  // Check if it's time to show time display
  if (currentTime - lastTimeDisplayTrigger >= TIME_DISPLAY_INTERVAL) {
    scatterShouldShowTimeNext = true;
    lastTimeDisplayTrigger = currentTime;
  }

  switch (scatterPhase) {
    case SCATTER_CHAOTIC: {
      // Occasional twitches while chaotic
      if (currentTime - lastTwitchTime >= TWITCH_INTERVAL) {
        sendTwitchPattern();
        lastTwitchTime = currentTime;
        updateScatterFlockDisplay();
      }

      // Check if time to converge
      if (currentTime - phaseStartTime >= chaoticDuration) {
        if (scatterShouldShowTimeNext) {
          // Converge to time display
          scatterCurrentMinute = getCurrentMinute();
          sendTimePattern(scatterCurrentMinute);
          scatterPhase = SCATTER_SHOWING_TIME;
          scatterShouldShowTimeNext = false;
        } else {
          // Converge to random swarm pattern
          currentSwarmPattern = getRandomSwarmPattern();
          sendConvergePattern(currentSwarmPattern);
          scatterPhase = SCATTER_CONVERGING;
        }
        phaseStartTime = currentTime;
        updateScatterFlockDisplay();
      }
      break;
    }

    case SCATTER_CONVERGING: {
      // Wait for convergence animation to complete
      if (currentTime - phaseStartTime >= (unsigned long)(CONVERGE_DURATION * 1000)) {
        scatterPhase = SCATTER_UNIFIED;
        phaseStartTime = currentTime;
        updateScatterFlockDisplay();
      }
      break;
    }

    case SCATTER_UNIFIED: {
      // If radial pattern, continuously rotate the gears
      if (currentSwarmPattern == SWARM_RADIAL) {
        if (currentTime - lastRotationTime >= ROTATION_INTERVAL) {
          updateRotatingGears();
          lastRotationTime = currentTime;
        }
      }

      // Hold unified pattern briefly
      if (currentTime - phaseStartTime >= UNIFIED_DURATION) {
        // Scatter back to chaos
        sendScatterPattern();
        scatterPhase = SCATTER_CHAOTIC;
        phaseStartTime = currentTime;
        lastTwitchTime = currentTime;
        chaoticDuration = random(CHAOTIC_DURATION_MIN, CHAOTIC_DURATION_MAX);
        currentTwitchPattern = getRandomTwitchPattern();  // Pick new twitch pattern
        updateScatterFlockDisplay();
      }
      break;
    }

    case SCATTER_SHOWING_TIME: {
      // Wait for time display animation to complete
      if (currentTime - phaseStartTime >= (unsigned long)(CONVERGE_DURATION * 1000)) {
        scatterPhase = SCATTER_HOLDING_TIME;
        phaseStartTime = currentTime;
      }
      break;
    }

    case SCATTER_HOLDING_TIME: {
      // Hold time display
      if (currentTime - phaseStartTime >= SCATTER_TIME_HOLD_DURATION) {
        // Scatter back to chaos
        sendScatterPattern();
        scatterPhase = SCATTER_CHAOTIC;
        phaseStartTime = currentTime;
        lastTwitchTime = currentTime;
        chaoticDuration = random(CHAOTIC_DURATION_MIN, CHAOTIC_DURATION_MAX);
        currentTwitchPattern = getRandomTwitchPattern();  // Pick new twitch pattern
        updateScatterFlockDisplay();
      }
      break;
    }
  }
}

// Initialize scatter flock animation
void initScatterFlock() {
  scatterPhase = SCATTER_CHAOTIC;
  phaseStartTime = millis();
  lastTwitchTime = millis();
  lastTimeDisplayTrigger = millis();
  chaoticDuration = random(CHAOTIC_DURATION_MIN, CHAOTIC_DURATION_MAX);
  scatterShouldShowTimeNext = true;  // Start with time display
  currentTwitchPattern = getRandomTwitchPattern();  // Pick initial twitch pattern

  // Start with initial scatter
  sendScatterPattern();
  updateScatterFlockDisplay();

  Serial.println("Scatter Flock initialized");
}

#endif // SCATTER_FLOCK_ANIMATION_H
