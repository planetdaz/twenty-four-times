#ifndef FLUID_TIME_ANIMATION_H
#define FLUID_TIME_ANIMATION_H

#include <Arduino.h>
#include <ESPNowComm.h>
#include <TFT_eSPI.h>

// Fluid Time Animation - Enhanced with multiple patterns, direction modes, and multi-stage effects
// All pixels move to the same target angles/directions, but staggered in time to create wave/ripple effects

// External references (provided by master.cpp)
extern TFT_eSPI tft;
extern unsigned long lastCommandTime;
extern unsigned long lastPingTime;
void sendPing();  // External function to ping pixels

// Digit pattern support (from master.cpp)
// DigitPattern struct already defined in master.cpp before this header is included
extern DigitPattern digitPatterns[];
extern const uint8_t digit1PixelIds[6];  // Left digit pixels
extern const uint8_t digit2PixelIds[6];  // Right digit pixels

// Color definitions (from master.cpp)
#define COLOR_BG      TFT_BLACK
#define COLOR_TEXT    TFT_WHITE
#define COLOR_ACCENT  TFT_GREEN

// ===== CONFIGURATION ENUMS =====

// Pattern types - different wave directions
enum FluidPattern {
  PATTERN_LEFT_RIGHT,   // Columns 0->7
  PATTERN_RIGHT_LEFT,   // Columns 7->0
  PATTERN_TOP_BOTTOM,   // Rows 0->2
  PATTERN_BOTTOM_TOP,   // Rows 2->0
  PATTERN_CENTER_OUT,   // Center columns outward
  PATTERN_EDGES_IN      // Edge columns inward
};

// Direction coordination modes
enum DirectionMode {
  DIR_MODE_UNIFIED,     // All hands rotate same direction (creates rotation effect)
  DIR_MODE_RANDOM,      // Each hand picks random direction (current behavior)
  DIR_MODE_ALTERNATING  // Groups alternate CW/CCW
};

// Multi-stage animation modes
enum MultiStageMode {
  STAGE_SINGLE,      // One wave, then pause
  STAGE_PING_PONG,   // Wave forward, then reverse immediately
  STAGE_DOUBLE_WAVE  // Send second wave partway through first
};

// Mirror/symmetry modes for kaleidoscopic effects
enum MirrorMode {
  MIRROR_NONE,           // All pixels same (current behavior)
  MIRROR_HORIZONTAL,     // Horizontal pairs mirror left-right
  MIRROR_VERTICAL,       // Vertical pairs mirror up-down
  MIRROR_QUAD,           // 2x2 groups mirror in all directions
  MIRROR_FULL_LR,        // Entire display mirrors left-right
  MIRROR_FULL_UD,        // Entire display mirrors up-down
  MIRROR_KALEIDOSCOPE    // Complex radial mirroring
};

// ===== STATE TRACKING =====

// Phase tracking
enum FluidTimePhase {
  FLUID_IDLE,           // Ready to generate new pattern
  FLUID_SENDING_GROUPS, // Actively sending to groups with delays
  FLUID_WAITING,        // Waiting for animations to complete before next pattern
  FLUID_HOLDING_TIME    // Holding time display for 5-7 seconds
};

FluidTimePhase fluidPhase = FLUID_IDLE;
uint8_t currentGroup = 0;
uint8_t totalGroups = 0;
unsigned long lastGroupSendTime = 0;
unsigned long fluidAnimationStartTime = 0;
unsigned long timeHoldStartTime = 0;

// Current animation parameters (randomized each cycle)
FluidPattern currentPattern;
DirectionMode currentDirMode;
MultiStageMode currentStageMode;
MirrorMode currentMirrorMode;
uint8_t currentStage = 0;  // For multi-stage animations (0 or 1)
unsigned long baseGroupDelay;
float baseDuration;

// Group ordering for current pattern
uint8_t groupOrder[24];  // Max 24 individual pixels if needed

// Pattern storage
struct FluidPatternData {
  float angle1, angle2, angle3;
  RotationDirection dir1, dir2, dir3;
  uint8_t colorIndex;
  TransitionType transition;
  float duration;
  unsigned long delay;  // Per-group delay
} currentFluidPattern;

// Time display tracking
uint8_t currentMinute = 0;  // Current minute (0-59)
unsigned long lastMinuteChange = 0;  // When minute last changed
const unsigned long MINUTE_INTERVAL = 60000;  // 60 seconds per minute
const unsigned long TIME_HOLD_DURATION = 6000;  // Hold time display for 6 seconds
bool showingTime = false;  // True when displaying time instead of random
bool shouldShowTimeNext = true;  // Flag to show time on next IDLE cycle (start with time display)

// ===== HELPER FUNCTIONS =====

// Get a random duration for Fluid Time (longer, slower animations)
// Range: 6.0 to 10.0 seconds, biased toward longer
inline float getFluidDuration() {
  float r1 = random(401) / 100.0f;  // 0.0 to 4.0
  float r2 = random(401) / 100.0f;  // 0.0 to 4.0
  float duration = max(r1, r2) + 6.0f;  // 6.0 to 10.0 seconds
  return duration;
}

// Get pattern name for display
const char* getPatternName(FluidPattern pattern) {
  switch (pattern) {
    case PATTERN_LEFT_RIGHT: return "Left->Right";
    case PATTERN_RIGHT_LEFT: return "Right->Left";
    case PATTERN_TOP_BOTTOM: return "Top->Bottom";
    case PATTERN_BOTTOM_TOP: return "Bottom->Top";
    case PATTERN_CENTER_OUT: return "Center Out";
    case PATTERN_EDGES_IN: return "Edges In";
    default: return "Unknown";
  }
}

// Get direction mode name for display
const char* getDirectionModeName(DirectionMode mode) {
  switch (mode) {
    case DIR_MODE_UNIFIED: return "Unified";
    case DIR_MODE_RANDOM: return "Random";
    case DIR_MODE_ALTERNATING: return "Alternating";
    default: return "Unknown";
  }
}

// Get stage mode name for display
const char* getStageModeName(MultiStageMode mode) {
  switch (mode) {
    case STAGE_SINGLE: return "Single";
    case STAGE_PING_PONG: return "Ping-Pong";
    case STAGE_DOUBLE_WAVE: return "Double Wave";
    default: return "Unknown";
  }
}

// Get mirror mode name for display
const char* getMirrorModeName(MirrorMode mode) {
  switch (mode) {
    case MIRROR_NONE: return "None";
    case MIRROR_HORIZONTAL: return "Horiz Pairs";
    case MIRROR_VERTICAL: return "Vert Pairs";
    case MIRROR_QUAD: return "Quad Groups";
    case MIRROR_FULL_LR: return "Full L-R";
    case MIRROR_FULL_UD: return "Full U-D";
    case MIRROR_KALEIDOSCOPE: return "Kaleidoscope";
    default: return "Unknown";
  }
}

// Mirror angle left-right (swap left/right)
inline float mirrorAngleLR(float angle) {
  // 0° stays 0°, 90° becomes 270°, 180° stays 180°, 270° becomes 90°
  if (angle == 90.0f) return 270.0f;
  if (angle == 270.0f) return 90.0f;
  return angle;  // 0° and 180° unchanged
}

// Mirror angle up-down (swap up/down)
inline float mirrorAngleUD(float angle) {
  // 0° becomes 180°, 90° stays 90°, 180° becomes 0°, 270° stays 270°
  if (angle == 0.0f) return 180.0f;
  if (angle == 180.0f) return 0.0f;
  return angle;  // 90° and 270° unchanged
}

// Apply mirroring to angles based on pixel position
// Returns mirrored angles for the given pixel based on current mirror mode
void getMirroredAngles(uint8_t pixelId, float baseAngle1, float baseAngle2, float baseAngle3,
                       float& outAngle1, float& outAngle2, float& outAngle3) {
  // Calculate row and column from pixel ID
  uint8_t row = pixelId / 8;
  uint8_t col = pixelId % 8;

  // Start with base angles
  outAngle1 = baseAngle1;
  outAngle2 = baseAngle2;
  outAngle3 = baseAngle3;

  switch (currentMirrorMode) {
    case MIRROR_NONE:
      // No mirroring, return base angles
      break;

    case MIRROR_HORIZONTAL:
      // Horizontal pairs: mirror within each pair (0-1, 2-3, 4-5, 6-7)
      if (col % 2 == 1) {  // Odd columns mirror left-right
        outAngle1 = mirrorAngleLR(baseAngle1);
        outAngle2 = mirrorAngleLR(baseAngle2);
        outAngle3 = mirrorAngleLR(baseAngle3);
      }
      break;

    case MIRROR_VERTICAL:
      // Vertical pairs: row 0 and row 2 mirror, row 1 is center
      if (row == 2) {  // Bottom row mirrors top row
        outAngle1 = mirrorAngleUD(baseAngle1);
        outAngle2 = mirrorAngleUD(baseAngle2);
        outAngle3 = mirrorAngleUD(baseAngle3);
      }
      break;

    case MIRROR_QUAD: {
      // 2x2 quad groups: mirror both horizontally and vertically
      // Treat columns in pairs (0-1, 2-3, 4-5, 6-7)
      bool mirrorH = (col % 2 == 1);
      bool mirrorV = (row == 2);  // Bottom row mirrors top

      if (mirrorH && mirrorV) {
        // Mirror both ways
        outAngle1 = mirrorAngleUD(mirrorAngleLR(baseAngle1));
        outAngle2 = mirrorAngleUD(mirrorAngleLR(baseAngle2));
        outAngle3 = mirrorAngleUD(mirrorAngleLR(baseAngle3));
      } else if (mirrorH) {
        // Mirror left-right only
        outAngle1 = mirrorAngleLR(baseAngle1);
        outAngle2 = mirrorAngleLR(baseAngle2);
        outAngle3 = mirrorAngleLR(baseAngle3);
      } else if (mirrorV) {
        // Mirror up-down only
        outAngle1 = mirrorAngleUD(baseAngle1);
        outAngle2 = mirrorAngleUD(baseAngle2);
        outAngle3 = mirrorAngleUD(baseAngle3);
      }
      break;
    }

    case MIRROR_FULL_LR:
      // Full left-right mirror: col 0-3 are base, col 4-7 mirror
      if (col >= 4) {
        outAngle1 = mirrorAngleLR(baseAngle1);
        outAngle2 = mirrorAngleLR(baseAngle2);
        outAngle3 = mirrorAngleLR(baseAngle3);
      }
      break;

    case MIRROR_FULL_UD:
      // Full up-down mirror: row 0 is base, rows 1-2 mirror
      if (row >= 1) {
        outAngle1 = mirrorAngleUD(baseAngle1);
        outAngle2 = mirrorAngleUD(baseAngle2);
        outAngle3 = mirrorAngleUD(baseAngle3);
      }
      break;

    case MIRROR_KALEIDOSCOPE: {
      // Kaleidoscope: combine full L-R and U-D mirroring
      bool kalMirrorH = (col >= 4);
      bool kalMirrorV = (row >= 1);

      if (kalMirrorH && kalMirrorV) {
        outAngle1 = mirrorAngleUD(mirrorAngleLR(baseAngle1));
        outAngle2 = mirrorAngleUD(mirrorAngleLR(baseAngle2));
        outAngle3 = mirrorAngleUD(mirrorAngleLR(baseAngle3));
      } else if (kalMirrorH) {
        outAngle1 = mirrorAngleLR(baseAngle1);
        outAngle2 = mirrorAngleLR(baseAngle2);
        outAngle3 = mirrorAngleLR(baseAngle3);
      } else if (kalMirrorV) {
        outAngle1 = mirrorAngleUD(baseAngle1);
        outAngle2 = mirrorAngleUD(baseAngle2);
        outAngle3 = mirrorAngleUD(baseAngle3);
      }
      break;
    }
  }
}

// Build group order based on pattern type
void buildGroupOrder() {
  switch (currentPattern) {
    case PATTERN_LEFT_RIGHT:
      // Columns 0->7
      totalGroups = 8;
      for (uint8_t i = 0; i < 8; i++) {
        groupOrder[i] = i;
      }
      break;

    case PATTERN_RIGHT_LEFT:
      // Columns 7->0
      totalGroups = 8;
      for (uint8_t i = 0; i < 8; i++) {
        groupOrder[i] = 7 - i;
      }
      break;

    case PATTERN_TOP_BOTTOM:
      // Rows 0->2 (each row is 8 pixels)
      totalGroups = 3;
      groupOrder[0] = 0;  // Row 0
      groupOrder[1] = 1;  // Row 1
      groupOrder[2] = 2;  // Row 2
      break;

    case PATTERN_BOTTOM_TOP:
      // Rows 2->0
      totalGroups = 3;
      groupOrder[0] = 2;  // Row 2
      groupOrder[1] = 1;  // Row 1
      groupOrder[2] = 0;  // Row 0
      break;

    case PATTERN_CENTER_OUT:
      // Center columns outward: 3,4, 2,5, 1,6, 0,7
      totalGroups = 8;
      groupOrder[0] = 3;
      groupOrder[1] = 4;
      groupOrder[2] = 2;
      groupOrder[3] = 5;
      groupOrder[4] = 1;
      groupOrder[5] = 6;
      groupOrder[6] = 0;
      groupOrder[7] = 7;
      break;

    case PATTERN_EDGES_IN:
      // Edge columns inward: 0,7, 1,6, 2,5, 3,4
      totalGroups = 8;
      groupOrder[0] = 0;
      groupOrder[1] = 7;
      groupOrder[2] = 1;
      groupOrder[3] = 6;
      groupOrder[4] = 2;
      groupOrder[5] = 5;
      groupOrder[6] = 3;
      groupOrder[7] = 4;
      break;
  }
}

// Send pattern to a specific group (column or row)
void sendFluidPatternToGroup(uint8_t groupIndex) {
  ESPNowPacket packet;
  packet.angleCmd.command = CMD_SET_ANGLES;
  packet.angleCmd.clearTargetMask();
  packet.angleCmd.transition = currentFluidPattern.transition;
  packet.angleCmd.duration = floatToDuration(currentFluidPattern.duration);

  // When showing time, only target the 12 digit pixels
  if (showingTime) {
    // Target only digit pixels
    for (int i = 0; i < 6; i++) {
      packet.angleCmd.setTargetPixel(digit1PixelIds[i]);
      packet.angleCmd.setTargetPixel(digit2PixelIds[i]);
    }
  } else {
    // Determine which pixels to target based on pattern type
    if (currentPattern == PATTERN_TOP_BOTTOM || currentPattern == PATTERN_BOTTOM_TOP) {
      // Row-based: target all 8 pixels in the row
      uint8_t row = groupOrder[groupIndex];
      for (uint8_t col = 0; col < 8; col++) {
        uint8_t pixelId = row * 8 + col;
        packet.angleCmd.setTargetPixel(pixelId);
      }
    } else {
      // Column-based: target 3 pixels in the column
      uint8_t col = groupOrder[groupIndex];
      packet.angleCmd.setTargetPixel(col);       // Row 0
      packet.angleCmd.setTargetPixel(col + 8);   // Row 1
      packet.angleCmd.setTargetPixel(col + 16);  // Row 2
    }
  }

  // Generate directions based on mode
  RotationDirection dir1, dir2, dir3;
  if (currentDirMode == DIR_MODE_UNIFIED) {
    // All hands same direction (use the stored direction from pattern)
    dir1 = currentFluidPattern.dir1;
    dir2 = currentFluidPattern.dir2;
    dir3 = currentFluidPattern.dir3;
  } else if (currentDirMode == DIR_MODE_ALTERNATING) {
    // Alternate by group
    if (groupIndex % 2 == 0) {
      dir1 = DIR_CW;
      dir2 = DIR_CW;
      dir3 = DIR_CW;
    } else {
      dir1 = DIR_CCW;
      dir2 = DIR_CCW;
      dir3 = DIR_CCW;
    }
  } else {
    // Random per hand
    dir1 = (random(2) == 0) ? DIR_CW : DIR_CCW;
    dir2 = (random(2) == 0) ? DIR_CW : DIR_CCW;
    dir3 = (random(2) == 0) ? DIR_CW : DIR_CCW;
  }

  // Set angles/directions/style for all pixels
  if (showingTime) {
    // Use digit patterns for time display
    uint8_t leftDigit = currentMinute / 10;
    uint8_t rightDigit = currentMinute % 10;

    DigitPattern& leftPattern = digitPatterns[leftDigit];
    DigitPattern& spacePattern = digitPatterns[11];  // Space pattern for right-aligning "1"
    DigitPattern& rightPattern = digitPatterns[rightDigit];

    // Set left digit angles (with right-align for "1")
    for (int i = 0; i < 6; i++) {
      uint8_t pixelId = digit1PixelIds[i];

      if (leftDigit == 1) {
        // Special handling for "1": right-align it
        // The "1" pattern has the digit in column 0, we want it in column 1
        // Pixel indices: 0,2,4 = column 0; 1,3,5 = column 1
        if (i % 2 == 0) {
          // Column 0: use space pattern
          packet.angleCmd.setPixelAngles(pixelId,
            spacePattern.angles[i][0],
            spacePattern.angles[i][1],
            spacePattern.angles[i][2],
            dir1, dir2, dir3);
          packet.angleCmd.setPixelStyle(pixelId, currentFluidPattern.colorIndex, spacePattern.opacity[i]);
        } else {
          // Column 1: use column 0 from "1" pattern (remap indices)
          // i=1 → use pattern[0], i=3 → use pattern[2], i=5 → use pattern[4]
          uint8_t sourceIdx = i - 1;  // Map column 1 to column 0 of source pattern
          packet.angleCmd.setPixelAngles(pixelId,
            leftPattern.angles[sourceIdx][0],
            leftPattern.angles[sourceIdx][1],
            leftPattern.angles[sourceIdx][2],
            dir1, dir2, dir3);
          packet.angleCmd.setPixelStyle(pixelId, currentFluidPattern.colorIndex, leftPattern.opacity[sourceIdx]);
        }
      } else {
        // Other digits: use pattern as-is
        packet.angleCmd.setPixelAngles(pixelId,
          leftPattern.angles[i][0],
          leftPattern.angles[i][1],
          leftPattern.angles[i][2],
          dir1, dir2, dir3);
        packet.angleCmd.setPixelStyle(pixelId, currentFluidPattern.colorIndex, leftPattern.opacity[i]);
      }
    }

    // Set right digit angles
    for (int i = 0; i < 6; i++) {
      uint8_t pixelId = digit2PixelIds[i];
      packet.angleCmd.setPixelAngles(pixelId,
        rightPattern.angles[i][0],
        rightPattern.angles[i][1],
        rightPattern.angles[i][2],
        dir1, dir2, dir3);
      packet.angleCmd.setPixelStyle(pixelId, currentFluidPattern.colorIndex, rightPattern.opacity[i]);
    }
  } else {
    // Use random pattern for all pixels WITH MIRRORING
    for (int i = 0; i < MAX_PIXELS; i++) {
      // Apply mirroring based on pixel position
      float mirroredAngle1, mirroredAngle2, mirroredAngle3;
      getMirroredAngles(i,
        currentFluidPattern.angle1,
        currentFluidPattern.angle2,
        currentFluidPattern.angle3,
        mirroredAngle1, mirroredAngle2, mirroredAngle3);

      packet.angleCmd.setPixelAngles(i,
        mirroredAngle1,
        mirroredAngle2,
        mirroredAngle3,
        dir1, dir2, dir3);
      packet.angleCmd.setPixelStyle(i, currentFluidPattern.colorIndex, 255);
    }
  }

  ESPNowComm::sendPacket(&packet, sizeof(AngleCommandPacket));
}

// Generate new random pattern parameters
void generateFluidPattern() {
  // Randomize pattern type
  currentPattern = (FluidPattern)random(6);

  // Randomize direction mode
  currentDirMode = (DirectionMode)random(3);

  // Randomize mirror mode (all modes equally likely)
  currentMirrorMode = (MirrorMode)random(7);

  // Randomize multi-stage mode (favor single wave slightly)
  int stageRand = random(10);
  if (stageRand < 5) {
    currentStageMode = STAGE_SINGLE;
  } else if (stageRand < 8) {
    currentStageMode = STAGE_PING_PONG;
  } else {
    currentStageMode = STAGE_DOUBLE_WAVE;
  }

  // Randomize timing
  baseGroupDelay = random(150, 501);  // 150-500ms
  baseDuration = getFluidDuration();  // 6-10 seconds

  // Build group order for this pattern
  buildGroupOrder();

  // Generate ONE set of target angles for all pixels
  currentFluidPattern.angle1 = getRandomAngle();
  currentFluidPattern.angle2 = getRandomAngle();
  currentFluidPattern.angle3 = getRandomAngle();

  // For unified mode, pick one direction for all hands
  if (currentDirMode == DIR_MODE_UNIFIED) {
    RotationDirection unifiedDir = (random(2) == 0) ? DIR_CW : DIR_CCW;
    currentFluidPattern.dir1 = unifiedDir;
    currentFluidPattern.dir2 = unifiedDir;
    currentFluidPattern.dir3 = unifiedDir;
  } else {
    // Store as random, will be overridden per group if needed
    currentFluidPattern.dir1 = (random(2) == 0) ? DIR_CW : DIR_CCW;
    currentFluidPattern.dir2 = (random(2) == 0) ? DIR_CW : DIR_CCW;
    currentFluidPattern.dir3 = (random(2) == 0) ? DIR_CW : DIR_CCW;
  }

  currentFluidPattern.colorIndex = getRandomColorIndex();
  currentFluidPattern.transition = getRandomTransition();

  // Apply duration variation (±15%)
  float variation = 0.85f + (random(31) / 100.0f);  // 0.85 to 1.15
  currentFluidPattern.duration = baseDuration * variation;

  Serial.println("=== New Fluid Time Pattern ===");
  Serial.print("Pattern: ");
  Serial.println(getPatternName(currentPattern));
  Serial.print("Direction Mode: ");
  Serial.println(getDirectionModeName(currentDirMode));
  Serial.print("Mirror Mode: ");
  Serial.println(getMirrorModeName(currentMirrorMode));
  Serial.print("Stage Mode: ");
  Serial.println(getStageModeName(currentStageMode));
  Serial.print("Base Delay: ");
  Serial.print(baseGroupDelay);
  Serial.println("ms");
  Serial.print("Duration: ");
  Serial.print(currentFluidPattern.duration, 1);
  Serial.println("s");
  Serial.print("Transition: ");
  Serial.println(getTransitionName(currentFluidPattern.transition));
}

// Generate time display pattern (uses digit angles instead of random)
void generateFluidTimePattern() {
  // Randomize pattern type
  currentPattern = (FluidPattern)random(6);

  // Randomize direction mode
  currentDirMode = (DirectionMode)random(3);

  // For time display, NO mirroring (keep digits readable)
  currentMirrorMode = MIRROR_NONE;

  // For time display, use single wave (no multi-stage)
  currentStageMode = STAGE_SINGLE;

  // Randomize timing
  baseGroupDelay = random(150, 501);  // 150-500ms
  baseDuration = getFluidDuration();  // 6-10 seconds

  // Build group order for this pattern
  buildGroupOrder();

  // Get digit patterns for current minute
  uint8_t leftDigit = currentMinute / 10;   // Tens digit
  uint8_t rightDigit = currentMinute % 10;  // Ones digit

  // Keep current color (don't generate new one)
  // Color was already set in previous pattern and stored in currentFluidPattern.colorIndex

  currentFluidPattern.transition = getRandomTransition();

  // Apply duration variation (±15%)
  float variation = 0.85f + (random(31) / 100.0f);  // 0.85 to 1.15
  currentFluidPattern.duration = baseDuration * variation;

  // For unified mode, pick one direction for all hands
  if (currentDirMode == DIR_MODE_UNIFIED) {
    RotationDirection unifiedDir = (random(2) == 0) ? DIR_CW : DIR_CCW;
    currentFluidPattern.dir1 = unifiedDir;
    currentFluidPattern.dir2 = unifiedDir;
    currentFluidPattern.dir3 = unifiedDir;
  } else {
    currentFluidPattern.dir1 = (random(2) == 0) ? DIR_CW : DIR_CCW;
    currentFluidPattern.dir2 = (random(2) == 0) ? DIR_CW : DIR_CCW;
    currentFluidPattern.dir3 = (random(2) == 0) ? DIR_CW : DIR_CCW;
  }

  Serial.println("=== Fluid Time Display ===");
  Serial.print("Time: ");
  Serial.print(leftDigit);
  Serial.println(rightDigit);
  Serial.print("Pattern: ");
  Serial.println(getPatternName(currentPattern));
  Serial.print("Direction Mode: ");
  Serial.println(getDirectionModeName(currentDirMode));
  Serial.print("Duration: ");
  Serial.print(currentFluidPattern.duration, 1);
  Serial.println("s");
  Serial.print("Transition: ");
  Serial.println(getTransitionName(currentFluidPattern.transition));

  showingTime = true;
}

// Update the display to show current state
void updateFluidTimeDisplay() {
  tft.fillScreen(COLOR_BG);
  tft.setTextColor(COLOR_ACCENT, COLOR_BG);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("FLUID TIME");

  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setTextSize(1);

  // Show if displaying time
  if (showingTime) {
    tft.setCursor(10, 30);
    tft.setTextColor(TFT_CYAN, COLOR_BG);
    tft.setTextSize(2);
    tft.print("Time: ");
    tft.print(currentMinute / 10);
    tft.println(currentMinute % 10);
    tft.setTextSize(1);
  }

  tft.setCursor(10, showingTime ? 50 : 35);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.print("Pattern: ");
  tft.println(getPatternName(currentPattern));

  int yOffset = showingTime ? 15 : 0;

  tft.setCursor(10, 50 + yOffset);
  tft.print("Mode: ");
  tft.println(getDirectionModeName(currentDirMode));

  tft.setCursor(10, 65 + yOffset);
  tft.print("Stage: ");
  tft.print(getStageModeName(currentStageMode));
  if (currentStageMode != STAGE_SINGLE) {
    tft.print(" (");
    tft.print(currentStage + 1);
    tft.print("/2)");
  }

  tft.setCursor(10, 85 + yOffset);
  tft.print("Transition: ");
  tft.println(getTransitionName(currentFluidPattern.transition));

  tft.setCursor(10, 100 + yOffset);
  tft.print("Duration: ");
  tft.print(currentFluidPattern.duration, 1);
  tft.println("s");

  // Show progress
  tft.setCursor(10, 120 + yOffset);
  tft.setTextColor(TFT_CYAN, COLOR_BG);
  tft.print("Progress: ");
  tft.print(currentGroup + 1);  // Display 1-based counting
  tft.print(" / ");
  tft.println(totalGroups);

  tft.setCursor(10, 140 + yOffset);
  tft.setTextColor(TFT_YELLOW, COLOR_BG);
  tft.println("Touch to return");
}

// ===== MAIN LOOP HANDLER =====

void handleFluidTimeLoop(unsigned long currentTime) {
  // Send periodic pings to keep pixels alive (every 3 seconds)
  if (currentTime - lastPingTime >= 3000) {
    sendPing();
    lastPingTime = currentTime;
  }

  // Check if it's time to change minute and show time
  if (currentTime - lastMinuteChange >= MINUTE_INTERVAL) {
    currentMinute = (currentMinute + 1) % 60;  // Loop 0-59
    lastMinuteChange = currentTime;
    shouldShowTimeNext = true;  // Set flag to show time on next IDLE cycle

    Serial.print("Minute changed to: ");
    Serial.print(currentMinute / 10);
    Serial.println(currentMinute % 10);
  }

  switch (fluidPhase) {
    case FLUID_IDLE: {
      // Decide whether to show time or random pattern
      if (shouldShowTimeNext) {
        // Generate time display pattern
        generateFluidTimePattern();
        shouldShowTimeNext = false;  // Clear flag
      } else {
        // Generate random pattern
        generateFluidPattern();
        showingTime = false;
      }
      currentStage = 0;

      // Send to first group immediately
      currentGroup = 0;
      sendFluidPatternToGroup(currentGroup);
      updateFluidTimeDisplay();
      lastGroupSendTime = currentTime;
      fluidAnimationStartTime = currentTime;
      fluidPhase = FLUID_SENDING_GROUPS;
      break;
    }

    case FLUID_SENDING_GROUPS: {
      // For double wave mode, check if we should start stage 2
      if (currentStageMode == STAGE_DOUBLE_WAVE && currentStage == 0) {
        // Start second wave when halfway through first wave
        if (currentGroup >= totalGroups / 2) {
          currentStage = 1;
          // Keep the same pattern but restart from beginning
          // This creates overlapping waves
          Serial.println("Starting second wave (overlap)");
        }
      }

      // Send to next group after delay
      if (currentTime - lastGroupSendTime >= baseGroupDelay) {
        currentGroup++;
        if (currentGroup < totalGroups) {
          sendFluidPatternToGroup(currentGroup);
          updateFluidTimeDisplay();
          lastGroupSendTime = currentTime;
        } else {
          // All groups sent for this stage
          if (currentStageMode == STAGE_PING_PONG && currentStage == 0) {
            // Start reverse wave immediately
            Serial.println("Starting ping-pong reverse");
            currentStage = 1;

            // Reverse the group order
            uint8_t temp[24];
            for (uint8_t i = 0; i < totalGroups; i++) {
              temp[i] = groupOrder[totalGroups - 1 - i];
            }
            for (uint8_t i = 0; i < totalGroups; i++) {
              groupOrder[i] = temp[i];
            }

            // Restart sending from first group (now reversed)
            currentGroup = 0;
            sendFluidPatternToGroup(currentGroup);
            updateFluidTimeDisplay();
            lastGroupSendTime = currentTime;
          } else {
            // Done with all stages, wait for animations to complete
            Serial.println("All stages sent, waiting for completion");
            fluidPhase = FLUID_WAITING;
          }
        }
      }
      break;
    }

    case FLUID_WAITING: {
      // Wait for animations to complete + pause before next pattern
      unsigned long totalAnimationTime = (unsigned long)(currentFluidPattern.duration * 1000);
      unsigned long totalWaitTime = totalAnimationTime + 1500;  // Shorter pause for continuous flow

      if (currentTime - fluidAnimationStartTime >= totalWaitTime) {
        if (showingTime) {
          // After showing time, hold it for 5-7 seconds
          Serial.println("Holding time display");
          fluidPhase = FLUID_HOLDING_TIME;
          timeHoldStartTime = currentTime;
        } else {
          // Regular pattern, start next one
          Serial.println("Starting next pattern");
          fluidPhase = FLUID_IDLE;
        }
      }
      break;
    }

    case FLUID_HOLDING_TIME: {
      // Hold the time display for TIME_HOLD_DURATION
      if (currentTime - timeHoldStartTime >= TIME_HOLD_DURATION) {
        Serial.println("Time hold complete, back to random patterns");
        showingTime = false;  // Clear time flag
        fluidPhase = FLUID_IDLE;  // Go back to random patterns
      }
      break;
    }
  }
}

#endif // FLUID_TIME_ANIMATION_H
