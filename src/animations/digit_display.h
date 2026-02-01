#ifndef DIGIT_DISPLAY_H
#define DIGIT_DISPLAY_H

#include <Arduino.h>
#include <ESPNowComm.h>

// External references (provided by master.cpp)
extern DigitPattern digitPatterns[];
extern const uint8_t digit1PixelIds[6];  // Left digit pixels
extern const uint8_t digit2PixelIds[6];  // Right digit pixels
extern unsigned long lastCommandTime;

// ===== CONSOLIDATED DIGIT DISPLAY FUNCTIONS =====
//
// General-purpose two-digit display function
// Used by time animations, digits mode, and any future digit displays
//
// Parameters:
//   leftDigit, rightDigit - The digits to display (0-11: 0-9, colon, space)
//   colorIndex            - Color palette index to use
//   transition            - Transition type (e.g., TRANSITION_EASE_IN_OUT)
//   durationSeconds       - How long the transition should take
//   dir1, dir2, dir3      - Default rotation directions (used if randomizePerPixel=false)
//   randomizePerPixel     - If true, each pixel gets random directions (digits mode style)
//                          If false, all pixels use dir1/dir2/dir3 (time animation style)

void sendTwoDigitDisplay(
  uint8_t leftDigit,
  uint8_t rightDigit,
  uint8_t colorIndex,
  TransitionType transition,
  float durationSeconds,
  RotationDirection dir1 = DIR_SHORTEST,
  RotationDirection dir2 = DIR_SHORTEST,
  RotationDirection dir3 = DIR_SHORTEST,
  bool randomizePerPixel = false
) {
  if (leftDigit > 11 || rightDigit > 11) return;  // Invalid digit

  ESPNowPacket packet;
  packet.angleCmd.command = CMD_SET_ANGLES;
  packet.angleCmd.clearTargetMask();

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

    // Get directions for this pixel (either randomized or unified)
    RotationDirection pixelDir1, pixelDir2, pixelDir3;
    if (randomizePerPixel) {
      pixelDir1 = (random(2) == 0) ? DIR_CW : DIR_CCW;
      pixelDir2 = (random(2) == 0) ? DIR_CW : DIR_CCW;
      pixelDir3 = (random(2) == 0) ? DIR_CW : DIR_CCW;
    } else {
      pixelDir1 = dir1;
      pixelDir2 = dir2;
      pixelDir3 = dir3;
    }

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
          pixelDir1, pixelDir2, pixelDir3);
        packet.angleCmd.setPixelStyle(pixelId, colorIndex, spacePattern.opacity[i]);
      } else {
        // Column 1: use column 0 from "1" pattern (remap indices)
        // i=1 → use pattern[0], i=3 → use pattern[2], i=5 → use pattern[4]
        uint8_t sourceIdx = i - 1;  // Map column 1 to column 0 of source pattern
        packet.angleCmd.setPixelAngles(pixelId,
          leftPattern.angles[sourceIdx][0],
          leftPattern.angles[sourceIdx][1],
          leftPattern.angles[sourceIdx][2],
          pixelDir1, pixelDir2, pixelDir3);
        packet.angleCmd.setPixelStyle(pixelId, colorIndex, leftPattern.opacity[sourceIdx]);
      }
    } else {
      // Other digits: use pattern as-is
      packet.angleCmd.setPixelAngles(pixelId,
        leftPattern.angles[i][0],
        leftPattern.angles[i][1],
        leftPattern.angles[i][2],
        pixelDir1, pixelDir2, pixelDir3);
      packet.angleCmd.setPixelStyle(pixelId, colorIndex, leftPattern.opacity[i]);
    }
  }

  // ===== SET RIGHT DIGIT PIXELS =====
  for (int i = 0; i < 6; i++) {
    uint8_t pixelId = digit2PixelIds[i];

    // Get directions for this pixel (either randomized or unified)
    RotationDirection pixelDir1, pixelDir2, pixelDir3;
    if (randomizePerPixel) {
      pixelDir1 = (random(2) == 0) ? DIR_CW : DIR_CCW;
      pixelDir2 = (random(2) == 0) ? DIR_CW : DIR_CCW;
      pixelDir3 = (random(2) == 0) ? DIR_CW : DIR_CCW;
    } else {
      pixelDir1 = dir1;
      pixelDir2 = dir2;
      pixelDir3 = dir3;
    }

    // Right digit: use pattern as-is (left-justified, even for "1")
    // This keeps digits close together (e.g., "21" not "2 1")
    packet.angleCmd.setPixelAngles(pixelId,
      rightPattern.angles[i][0],
      rightPattern.angles[i][1],
      rightPattern.angles[i][2],
      pixelDir1, pixelDir2, pixelDir3);
    packet.angleCmd.setPixelStyle(pixelId, colorIndex, rightPattern.opacity[i]);
  }

  // Set transition and duration
  packet.angleCmd.transition = transition;
  packet.angleCmd.duration = floatToDuration(durationSeconds);

  // Send the packet
  ESPNowComm::sendPacket(&packet, sizeof(AngleCommandPacket));
  lastCommandTime = millis();
}

// ===== CONVENIENCE WRAPPER FOR TIME ANIMATIONS =====
//
// Simplified function for time-based animations
// Automatically extracts left/right digits from minute value (0-59)

void sendTwoDigitTime(
  uint8_t minute,
  uint8_t colorIndex,
  TransitionType transition,
  float durationSeconds,
  RotationDirection dir1 = DIR_SHORTEST,
  RotationDirection dir2 = DIR_SHORTEST,
  RotationDirection dir3 = DIR_SHORTEST
) {
  uint8_t leftDigit = minute / 10;   // Tens digit (0-5)
  uint8_t rightDigit = minute % 10;  // Ones digit (0-9)

  sendTwoDigitDisplay(
    leftDigit, rightDigit,
    colorIndex,
    transition,
    durationSeconds,
    dir1, dir2, dir3,
    false  // Use unified directions for time animations
  );
}

#endif // DIGIT_DISPLAY_H
