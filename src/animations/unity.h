#ifndef UNITY_ANIMATION_H
#define UNITY_ANIMATION_H

#include <Arduino.h>
#include <ESPNowComm.h>
#include <TFT_eSPI.h>

// Unity Animation - All pixels move in unison with synchronized random patterns
// This creates a choreographed, unified visual effect across all displays
// Periodically shows current time digits

// Timing for Unity animation
const unsigned long UNITY_PATTERN_INTERVAL = 5000;  // 5 seconds between random patterns
const unsigned long UNITY_TIME_DISPLAY_INTERVAL = 60000; // Show time every 60 seconds
const unsigned long UNITY_TIME_HOLD_DURATION = 6000;     // Hold time display for 6 seconds

// External references (provided by master.cpp)
extern TFT_eSPI tft;
extern unsigned long lastCommandTime;
extern unsigned long lastPingTime;
void sendPing();  // External function to ping pixels
uint8_t getCurrentMinute();  // Get current minute from real-time clock
String getCurrentTimeString();  // Get formatted time string
void sendTwoDigitTime(uint8_t minute, uint8_t colorIndex, TransitionType transition,
                      float durationSeconds, RotationDirection dir1, RotationDirection dir2,
                      RotationDirection dir3);  // From digit_display.h

// Color definitions (from master.cpp)
#define COLOR_BG      TFT_BLACK
#define COLOR_TEXT    TFT_WHITE
#define COLOR_ACCENT  TFT_GREEN

// ===== STATE TRACKING =====
enum UnityPhase {
  UNITY_PATTERN,      // Showing random patterns
  UNITY_SHOWING_TIME, // Showing current time digits
};

UnityPhase unityPhase = UNITY_PATTERN;
unsigned long lastUnityPattern = 0;
unsigned long lastUnityTimeDisplay = 0;
unsigned long unityTimeHoldStart = 0;
uint8_t unityCurrentMinute = 0;
bool shouldShowUnityTime = true;  // Start with time display

// Send a Unity pattern - all pixels move in synchronized unison
void sendUnityPattern() {
  ESPNowPacket packet;
  packet.angleCmd.command = CMD_SET_ANGLES;
  packet.angleCmd.clearTargetMask();  // Target all pixels (broadcast mode)
  packet.angleCmd.transition = getRandomTransition();
  packet.angleCmd.duration = floatToDuration(getRandomDuration());

  // Generate random values ONCE for all pixels (synchronized movement)
  float angle1 = getRandomAngle();
  float angle2 = getRandomAngle();
  float angle3 = getRandomAngle();

  // Random directions for choreographic control (all pixels move in unison)
  RotationDirection dir1 = (random(2) == 0) ? DIR_CW : DIR_CCW;
  RotationDirection dir2 = (random(2) == 0) ? DIR_CW : DIR_CCW;
  RotationDirection dir3 = (random(2) == 0) ? DIR_CW : DIR_CCW;

  uint8_t colorIndex = getRandomColorIndex();
  uint8_t opacity = 255;  // Always full opacity

  // Apply same values to all pixels for synchronized movement
  for (int i = 0; i < MAX_PIXELS; i++) {
    packet.angleCmd.setPixelAngles(i, angle1, angle2, angle3, dir1, dir2, dir3);
    packet.angleCmd.setPixelStyle(i, colorIndex, opacity);
  }

  // Send the packet
  ESPNowComm::sendPacket(&packet, sizeof(AngleCommandPacket));
  lastCommandTime = millis();

  Serial.print("Unity pattern: ");
  Serial.print(getTransitionName(packet.angleCmd.transition));
  Serial.print(", ");
  Serial.print(durationToFloat(packet.angleCmd.duration), 1);
  Serial.println("s");
}

// Update display screen
void updateUnityDisplay() {
  tft.fillScreen(COLOR_BG);
  tft.setTextColor(COLOR_ACCENT, COLOR_BG);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("UNITY");

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
  tft.print("Mode: ");
  if (unityPhase == UNITY_SHOWING_TIME) {
    tft.setTextColor(TFT_CYAN, COLOR_BG);
    tft.print("Showing time: ");
    tft.print(unityCurrentMinute / 10);
    tft.println(unityCurrentMinute % 10);
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
  } else {
    tft.println("Unified patterns");
  }

  tft.setCursor(10, 55);
  tft.setTextColor(COLOR_ACCENT, COLOR_BG);
  tft.println("All pixels synchronized");

  tft.setCursor(10, 75);
  tft.setTextColor(TFT_YELLOW, COLOR_BG);
  tft.println("Touch to return");
}

// Handle Unity animation loop - sends patterns at regular intervals
void handleUnityLoop(unsigned long currentTime) {
  // Send periodic pings to keep pixels alive (every 3 seconds)
  if (currentTime - lastPingTime >= 3000) {
    sendPing();
    lastPingTime = currentTime;
  }

  switch (unityPhase) {
    case UNITY_PATTERN: {
      // Check if it's time to show time
      if (currentTime - lastUnityTimeDisplay >= UNITY_TIME_DISPLAY_INTERVAL) {
        // Transition to time display
        unityCurrentMinute = getCurrentMinute();
        unityPhase = UNITY_SHOWING_TIME;
        unityTimeHoldStart = currentTime;

        // Send time display with unified directions (all pixels same)
        RotationDirection dir1 = (random(2) == 0) ? DIR_CW : DIR_CCW;
        RotationDirection dir2 = (random(2) == 0) ? DIR_CW : DIR_CCW;
        RotationDirection dir3 = (random(2) == 0) ? DIR_CW : DIR_CCW;

        sendTwoDigitTime(
          unityCurrentMinute,
          getRandomColorIndex(),
          TRANSITION_EASE_IN_OUT,
          2.0f,  // 2 second transition
          dir1, dir2, dir3
        );

        updateUnityDisplay();

        Serial.print("Unity showing time: ");
        Serial.print(unityCurrentMinute / 10);
        Serial.println(unityCurrentMinute % 10);
      } else {
        // Send random patterns every 5 seconds
        if (currentTime - lastUnityPattern >= UNITY_PATTERN_INTERVAL) {
          sendUnityPattern();
          lastUnityPattern = currentTime;
          updateUnityDisplay();
        }
      }
      break;
    }

    case UNITY_SHOWING_TIME: {
      // Hold time display for 6 seconds
      if (currentTime - unityTimeHoldStart >= UNITY_TIME_HOLD_DURATION) {
        unityPhase = UNITY_PATTERN;
        lastUnityTimeDisplay = currentTime;
        lastUnityPattern = currentTime;  // Reset pattern timer
        sendUnityPattern();  // Resume with random pattern
        updateUnityDisplay();

        Serial.println("Unity returning to patterns");
      }
      break;
    }
  }
}

#endif // UNITY_ANIMATION_H
