#ifndef DIGIT_DISPLAY_H
#define DIGIT_DISPLAY_H

#include <Arduino.h>
#include <ESPNowComm.h>

// External references (provided by master.cpp)
extern DigitPattern digitPatterns[];
extern const uint8_t digit1PixelIds[6];  // Left digit pixels
extern const uint8_t digit2PixelIds[6];  // Right digit pixels
extern unsigned long lastCommandTime;

// ===== CONSOLIDATED DIGIT DISPLAY FUNCTION =====
//
// Sends a two-digit time display (00-59) to the pixel grid
// Handles special right-alignment for digit "1"
// Used by all time-based animations
//
// Parameters:
//   minute         - The minute value to display (0-59)
//   colorIndex     - Color palette index to use
//   transition     - Transition type (e.g., TRANSITION_EASE_IN_OUT, TRANSITION_LINEAR)
//   durationSeconds - How long the transition should take
//   dir1, dir2, dir3 - Optional rotation directions for the 3 hands (defaults to DIR_SHORTEST)

void sendTwoDigitTime(
  uint8_t minute,
  uint8_t colorIndex,
  TransitionType transition,
  float durationSeconds,
  RotationDirection dir1 = DIR_SHORTEST,
  RotationDirection dir2 = DIR_SHORTEST,
  RotationDirection dir3 = DIR_SHORTEST
) {
  ESPNowPacket packet;
  packet.angleCmd.command = CMD_SET_ANGLES;
  packet.angleCmd.clearTargetMask();

  // Extract left and right digits
  uint8_t leftDigit = minute / 10;   // Tens digit (0-5)
  uint8_t rightDigit = minute % 10;  // Ones digit (0-9)

  // Get digit patterns
  DigitPattern& leftPattern = digitPatterns[leftDigit];
  DigitPattern& rightPattern = digitPatterns[rightDigit];
  DigitPattern& spacePattern = digitPatterns[11];  // Space pattern for right-aligning "1"

  // Target only the 12 digit pixels
  for (int i = 0; i < 6; i++) {
    packet.angleCmd.setTargetPixel(digit1PixelIds[i]);
    packet.angleCmd.setTargetPixel(digit2PixelIds[i]);
  }

  // ===== SET LEFT DIGIT PIXELS =====
  for (int i = 0; i < 6; i++) {
    uint8_t pixelId = digit1PixelIds[i];

    // Special handling for digit "1" - right-align it
    // The "1" pattern has the digit in column 0, we want it in column 1
    // Pixel indices: 0,2,4 = column 0; 1,3,5 = column 1
    if (leftDigit == 1) {
      if (i % 2 == 0) {
        // Column 0: use space pattern (blank)
        packet.angleCmd.setPixelAngles(pixelId,
          spacePattern.angles[i][0],
          spacePattern.angles[i][1],
          spacePattern.angles[i][2],
          dir1, dir2, dir3);
        packet.angleCmd.setPixelStyle(pixelId, colorIndex, spacePattern.opacity[i]);
      } else {
        // Column 1: use column 0 from "1" pattern (remap indices)
        // i=1 → use pattern[0], i=3 → use pattern[2], i=5 → use pattern[4]
        uint8_t sourceIdx = i - 1;  // Map column 1 to column 0 of source pattern
        packet.angleCmd.setPixelAngles(pixelId,
          leftPattern.angles[sourceIdx][0],
          leftPattern.angles[sourceIdx][1],
          leftPattern.angles[sourceIdx][2],
          dir1, dir2, dir3);
        packet.angleCmd.setPixelStyle(pixelId, colorIndex, leftPattern.opacity[sourceIdx]);
      }
    } else {
      // Other digits: use pattern as-is
      packet.angleCmd.setPixelAngles(pixelId,
        leftPattern.angles[i][0],
        leftPattern.angles[i][1],
        leftPattern.angles[i][2],
        dir1, dir2, dir3);
      packet.angleCmd.setPixelStyle(pixelId, colorIndex, leftPattern.opacity[i]);
    }
  }

  // ===== SET RIGHT DIGIT PIXELS =====
  for (int i = 0; i < 6; i++) {
    uint8_t pixelId = digit2PixelIds[i];

    // Special handling for digit "1" - right-align it
    if (rightDigit == 1) {
      if (i % 2 == 0) {
        // Column 0: use space pattern (blank)
        packet.angleCmd.setPixelAngles(pixelId,
          spacePattern.angles[i][0],
          spacePattern.angles[i][1],
          spacePattern.angles[i][2],
          dir1, dir2, dir3);
        packet.angleCmd.setPixelStyle(pixelId, colorIndex, spacePattern.opacity[i]);
      } else {
        // Column 1: use column 0 from "1" pattern (remap indices)
        uint8_t sourceIdx = i - 1;
        packet.angleCmd.setPixelAngles(pixelId,
          rightPattern.angles[sourceIdx][0],
          rightPattern.angles[sourceIdx][1],
          rightPattern.angles[sourceIdx][2],
          dir1, dir2, dir3);
        packet.angleCmd.setPixelStyle(pixelId, colorIndex, rightPattern.opacity[sourceIdx]);
      }
    } else {
      // Other digits: use pattern as-is
      packet.angleCmd.setPixelAngles(pixelId,
        rightPattern.angles[i][0],
        rightPattern.angles[i][1],
        rightPattern.angles[i][2],
        dir1, dir2, dir3);
      packet.angleCmd.setPixelStyle(pixelId, colorIndex, rightPattern.opacity[i]);
    }
  }

  // Set transition and duration
  packet.angleCmd.transition = transition;
  packet.angleCmd.duration = floatToDuration(durationSeconds);

  // Send the packet
  ESPNowComm::sendPacket(&packet, sizeof(AngleCommandPacket));
  lastCommandTime = millis();
}

#endif // DIGIT_DISPLAY_H
