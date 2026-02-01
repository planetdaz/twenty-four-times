#ifndef UNITY_ANIMATION_H
#define UNITY_ANIMATION_H

#include <Arduino.h>
#include <ESPNowComm.h>
#include <TFT_eSPI.h>

// Unity Animation - All pixels move in unison with synchronized random patterns
// This creates a choreographed, unified visual effect across all displays

// Timing for Unity animation
const unsigned long UNITY_INTERVAL = 5000;  // 5 seconds between random patterns

// External references (provided by master.cpp)
extern TFT_eSPI tft;
extern unsigned long lastCommandTime;

// Color definitions (from master.cpp)
#define COLOR_BG      TFT_BLACK
#define COLOR_TEXT    TFT_WHITE
#define COLOR_ACCENT  TFT_GREEN

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
  uint8_t opacity = getRandomOpacity();

  // Apply same values to all pixels for synchronized movement
  for (int i = 0; i < MAX_PIXELS; i++) {
    packet.angleCmd.setPixelAngles(i, angle1, angle2, angle3, dir1, dir2, dir3);
    packet.angleCmd.setPixelStyle(i, colorIndex, opacity);
  }

  // Send the packet
  if (ESPNowComm::sendPacket(&packet, sizeof(AngleCommandPacket))) {
    Serial.print("Sent Unity pattern: ");
    Serial.print(getTransitionName(packet.angleCmd.transition));
    Serial.print(", duration: ");
    Serial.print(durationToFloat(packet.angleCmd.duration), 1);
    Serial.println("s");

    // Update display
    tft.fillScreen(COLOR_BG);
    tft.setTextColor(COLOR_ACCENT, COLOR_BG);
    tft.setTextSize(2);
    tft.setCursor(10, 10);
    tft.println("UNITY ANIMATION");

    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.setTextSize(1);
    tft.setCursor(10, 40);
    tft.print("Transition: ");
    tft.println(getTransitionName(packet.angleCmd.transition));

    tft.setCursor(10, 55);
    tft.print("Duration: ");
    tft.print(durationToFloat(packet.angleCmd.duration), 1);
    tft.println(" sec");

    tft.setCursor(10, 75);
    tft.setTextColor(COLOR_ACCENT, COLOR_BG);
    tft.println("All pixels move in unison");
    tft.println("Random angles & directions");

    tft.setCursor(10, 110);
    tft.setTextColor(TFT_YELLOW, COLOR_BG);
    tft.println("Touch screen to return to menu");

  } else {
    Serial.println("Failed to send Unity pattern!");
  }
}

// Handle Unity animation loop - sends patterns at regular intervals
void handleUnityLoop(unsigned long currentTime) {
  // Send random patterns every UNITY_INTERVAL, starting after previous animation completes
  if (currentTime - lastCommandTime >= UNITY_INTERVAL) {
    sendUnityPattern();
    lastCommandTime = currentTime;
  }
}

#endif // UNITY_ANIMATION_H
