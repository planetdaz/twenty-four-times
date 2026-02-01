#ifndef FLUID_TIME_ANIMATION_H
#define FLUID_TIME_ANIMATION_H

#include <Arduino.h>
#include <ESPNowComm.h>
#include <TFT_eSPI.h>

// Fluid Time Animation - All pixels move to the same target angles/directions,
// but staggered in time by column to create a wave/ripple effect

// External references (provided by master.cpp)
extern TFT_eSPI tft;
extern unsigned long lastCommandTime;

// Color definitions (from master.cpp)
#define COLOR_BG      TFT_BLACK
#define COLOR_TEXT    TFT_WHITE
#define COLOR_ACCENT  TFT_GREEN

// ===== FLUID TIME STATE =====

// Phase tracking
enum FluidTimePhase {
  FLUID_IDLE,           // Ready to generate new pattern
  FLUID_SENDING_GROUPS, // Actively sending to groups with delays
  FLUID_WAITING         // Waiting for animations to complete before next pattern
};

FluidTimePhase fluidPhase = FLUID_IDLE;
uint8_t currentGroup = 0;
unsigned long lastGroupSendTime = 0;
unsigned long fluidAnimationStartTime = 0;

// Configuration
const uint8_t NUM_COLUMNS = 8;
const unsigned long FLUID_GROUP_DELAY = 300;  // ms delay between columns (configurable)
const unsigned long FLUID_PAUSE_AFTER = 2000; // ms pause after all animations complete

// Pattern storage - same pattern sent to all groups, just at different times
struct FluidPattern {
  float angle1, angle2, angle3;
  RotationDirection dir1, dir2, dir3;
  uint8_t colorIndex;
  TransitionType transition;
  float duration;
} currentFluidPattern;

// ===== HELPER FUNCTIONS =====

// Get a random duration for Fluid Time (longer, slower animations)
// Range: 6.0 to 10.0 seconds
inline float getFluidDuration() {
  // Random duration biased toward longer times
  float r1 = random(401) / 100.0f;  // 0.0 to 4.0
  float r2 = random(401) / 100.0f;  // 0.0 to 4.0
  float duration = max(r1, r2) + 6.0f;  // 6.0 to 10.0 seconds
  return duration;
}

// Send the current fluid pattern to a specific column
void sendFluidPatternToColumn(uint8_t columnIndex) {
  // Column N contains pixels: N, N+8, N+16
  // Example: Column 0 = pixels 0, 8, 16
  //          Column 7 = pixels 7, 15, 23

  ESPNowPacket packet;
  packet.angleCmd.command = CMD_SET_ANGLES;
  packet.angleCmd.clearTargetMask();
  packet.angleCmd.transition = currentFluidPattern.transition;
  packet.angleCmd.duration = floatToDuration(currentFluidPattern.duration);

  // Set the target mask for this column's 3 pixels
  packet.angleCmd.setTargetPixel(columnIndex);       // Row 0
  packet.angleCmd.setTargetPixel(columnIndex + 8);   // Row 1
  packet.angleCmd.setTargetPixel(columnIndex + 16);  // Row 2

  // Set same angles/directions/style for all pixels in the packet
  // (Only targeted pixels will respond, but we fill all for consistency)
  for (int i = 0; i < MAX_PIXELS; i++) {
    packet.angleCmd.setPixelAngles(i,
      currentFluidPattern.angle1,
      currentFluidPattern.angle2,
      currentFluidPattern.angle3,
      currentFluidPattern.dir1,
      currentFluidPattern.dir2,
      currentFluidPattern.dir3);
    packet.angleCmd.setPixelStyle(i, currentFluidPattern.colorIndex, 255);
  }

  if (ESPNowComm::sendPacket(&packet, sizeof(AngleCommandPacket))) {
    Serial.print("Sent Fluid Time to Column ");
    Serial.print(columnIndex);
    Serial.print(" (pixels ");
    Serial.print(columnIndex);
    Serial.print(",");
    Serial.print(columnIndex + 8);
    Serial.print(",");
    Serial.print(columnIndex + 16);
    Serial.println(")");
  } else {
    Serial.print("Failed to send to Column ");
    Serial.println(columnIndex);
  }
}

// Update the display to show current state
void updateFluidTimeDisplay(uint8_t sentColumns) {
  tft.fillScreen(COLOR_BG);
  tft.setTextColor(COLOR_ACCENT, COLOR_BG);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("FLUID TIME");

  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setTextSize(1);
  tft.setCursor(10, 40);
  tft.print("Transition: ");
  tft.println(getTransitionName(currentFluidPattern.transition));

  tft.setCursor(10, 55);
  tft.print("Duration: ");
  tft.print(currentFluidPattern.duration, 1);
  tft.println(" sec");

  tft.setCursor(10, 70);
  tft.print("Delay: ");
  tft.print(FLUID_GROUP_DELAY);
  tft.println(" ms");

  tft.setCursor(10, 90);
  tft.setTextColor(COLOR_ACCENT, COLOR_BG);
  tft.println("Staggered wave effect");
  tft.println("Columns animate in sequence");

  // Show progress
  tft.setCursor(10, 120);
  tft.setTextColor(TFT_CYAN, COLOR_BG);
  tft.print("Columns sent: ");
  tft.print(sentColumns);
  tft.print(" / ");
  tft.println(NUM_COLUMNS);

  tft.setCursor(10, 140);
  tft.setTextColor(TFT_YELLOW, COLOR_BG);
  tft.println("Touch screen to return to menu");
}

// ===== MAIN LOOP HANDLER =====

void handleFluidTimeLoop(unsigned long currentTime) {
  switch (fluidPhase) {
    case FLUID_IDLE: {
      // Generate ONE random pattern for all pixels
      currentFluidPattern.angle1 = getRandomAngle();
      currentFluidPattern.angle2 = getRandomAngle();
      currentFluidPattern.angle3 = getRandomAngle();
      currentFluidPattern.dir1 = (random(2) == 0) ? DIR_CW : DIR_CCW;
      currentFluidPattern.dir2 = (random(2) == 0) ? DIR_CW : DIR_CCW;
      currentFluidPattern.dir3 = (random(2) == 0) ? DIR_CW : DIR_CCW;
      currentFluidPattern.colorIndex = getRandomColorIndex();
      currentFluidPattern.transition = getRandomTransition();
      currentFluidPattern.duration = getFluidDuration(); // 6-10 seconds

      Serial.print("New Fluid Time pattern: ");
      Serial.print(getTransitionName(currentFluidPattern.transition));
      Serial.print(", ");
      Serial.print(currentFluidPattern.duration, 1);
      Serial.println("s");

      // Send to first column immediately
      currentGroup = 0;
      sendFluidPatternToColumn(currentGroup);
      updateFluidTimeDisplay(currentGroup + 1);
      lastGroupSendTime = currentTime;
      fluidAnimationStartTime = currentTime;
      fluidPhase = FLUID_SENDING_GROUPS;
      break;
    }

    case FLUID_SENDING_GROUPS: {
      // Send to next group after delay
      if (currentTime - lastGroupSendTime >= FLUID_GROUP_DELAY) {
        currentGroup++;
        if (currentGroup < NUM_COLUMNS) {
          sendFluidPatternToColumn(currentGroup);
          updateFluidTimeDisplay(currentGroup + 1);
          lastGroupSendTime = currentTime;
        } else {
          // All groups sent, wait for animations to complete
          Serial.println("All columns sent, waiting for animations to complete");
          fluidPhase = FLUID_WAITING;
        }
      }
      break;
    }

    case FLUID_WAITING: {
      // Wait for animations to complete + pause before next pattern
      // Total wait = duration of animation + pause time
      unsigned long totalAnimationTime = (unsigned long)(currentFluidPattern.duration * 1000);
      unsigned long totalWaitTime = totalAnimationTime + FLUID_PAUSE_AFTER;

      if (currentTime - fluidAnimationStartTime >= totalWaitTime) {
        Serial.println("Starting next pattern");
        fluidPhase = FLUID_IDLE;
      }
      break;
    }
  }
}

#endif // FLUID_TIME_ANIMATION_H
