#ifndef METRONOME_TIME_ANIMATION_H
#define METRONOME_TIME_ANIMATION_H

#include <Arduino.h>
#include <ESPNowComm.h>
#include <TFT_eSPI.h>

// Metronome Time Animation - Polyrhythmic visual music
// Each pixel acts as a metronome ticking at different tempos
// Creates layered rhythmic patterns between time displays

// External references (provided by master.cpp)
extern TFT_eSPI tft;
extern unsigned long lastCommandTime;
extern unsigned long lastPingTime;
void sendPing();  // External function to ping pixels
uint8_t getCurrentMinute();  // Get current minute from real-time clock
String getCurrentTimeString();  // Get formatted time string

// Digit pattern support (from master.cpp)
extern DigitPattern digitPatterns[];
extern const uint8_t digit1PixelIds[6];  // Left digit pixels
extern const uint8_t digit2PixelIds[6];  // Right digit pixels

// Shared helper functions (from digit_display.h)
void sendTwoDigitTime(uint8_t minute, uint8_t colorIndex, TransitionType transition,
                      float durationSeconds, RotationDirection dir1,
                      RotationDirection dir2, RotationDirection dir3);

// Color definitions
#define COLOR_BG      TFT_BLACK
#define COLOR_TEXT    TFT_WHITE
#define COLOR_ACCENT  TFT_GREEN

// ===== METRONOME CONFIGURATION =====

// Tempo patterns - different rhythmic relationships
enum TempoPattern {
  TEMPO_ROWS,        // Each row has same tempo
  TEMPO_COLUMNS,     // Each column has same tempo
  TEMPO_CHECKERBOARD, // Alternating fast/slow
  TEMPO_GRADIENT,    // Smooth tempo gradient
  TEMPO_RANDOM       // Random tempo per pixel
};

// Swing patterns - how metronomes move
enum SwingPattern {
  SWING_SIMPLE,      // Simple back-and-forth (0° ↔ 180°)
  SWING_PENDULUM,    // Wide arc swing (45° ↔ 315°)
  SWING_ROTATE,      // Full rotation that pauses at extremes
  SWING_TRIPLE       // All 3 hands swing together at different phases
};

// ===== STATE TRACKING =====

// Phase tracking
enum MetronomePhase {
  METRO_TICKING,     // Normal metronome ticking
  METRO_SHOWING_TIME, // Paused to show time digits
  METRO_RESUMING     // Transitioning back to ticking
};

MetronomePhase metroPhase = METRO_TICKING;
unsigned long lastMetroTickTime = 0;
unsigned long timeDisplayStart = 0;
unsigned long metroLastMinuteChange = 0;  // Unique name to avoid conflict with fluid_time.h

// Configuration
const unsigned long METRO_MINUTE_INTERVAL = 60000;  // 60 seconds between time displays
const unsigned long METRO_TIME_HOLD = 6000;  // Hold time display for 6 seconds
bool metroShouldShowTime = true;  // Start by showing time
uint8_t metroCurrentMinute = 0;

// Current pattern configuration
TempoPattern currentTempoPattern;
SwingPattern currentSwingPattern;
uint8_t metroColorIndex;

// Per-pixel metronome state (24 pixels max)
struct MetronomePixel {
  float currentAngle1, currentAngle2, currentAngle3;  // Current angles
  float targetAngle1, targetAngle2, targetAngle3;     // Target angles
  float swingMin1, swingMin2, swingMin3;              // Swing range min
  float swingMax1, swingMax2, swingMax3;              // Swing range max
  uint16_t bpm;                                        // Beats per minute
  unsigned long lastBeat;                              // Time of last beat
  bool swingingUp;                                     // Direction flag
} metroPixels[24];

// ===== HELPER FUNCTIONS =====

// Convert BPM to milliseconds per beat
inline unsigned long bpmToMs(uint16_t bpm) {
  return 60000 / bpm;
}

// Get tempo for a pixel based on pattern and position
uint16_t getTempoForPixel(uint8_t pixelId, TempoPattern pattern) {
  uint8_t row = pixelId / 8;  // 0-2
  uint8_t col = pixelId % 8;  // 0-7

  switch (pattern) {
    case TEMPO_ROWS:
      // Top row: 160 BPM (fast), Middle: 80 BPM, Bottom: 40 BPM (slow)
      return (row == 0) ? 160 : (row == 1) ? 80 : 40;

    case TEMPO_COLUMNS:
      // Tempo increases left to right (40 to ~157 BPM)
      return 40 + (col * 17);  // 40, 57, 74, 91, 108, 125, 142, 159

    case TEMPO_CHECKERBOARD:
      // Alternating fast/slow
      return ((row + col) % 2 == 0) ? 120 : 60;

    case TEMPO_GRADIENT:
      // Diagonal gradient from top-left (40) to bottom-right (~170)
      return 40 + ((row + col) * 13);

    case TEMPO_RANDOM: {
      // Random tempo in musical range (braces to avoid cross-initialization)
      uint16_t tempos[] = {40, 60, 80, 120, 160};
      return tempos[random(5)];
    }
  }

  return 80;  // Default fallback
}

// Initialize metronome for a pixel
void initMetronomePixel(uint8_t pixelId, TempoPattern tempoPattern, SwingPattern swingPattern) {
  MetronomePixel& metro = metroPixels[pixelId];

  // Set tempo
  metro.bpm = getTempoForPixel(pixelId, tempoPattern);
  metro.lastBeat = millis();
  metro.swingingUp = true;

  // Set swing range based on pattern
  switch (swingPattern) {
    case SWING_SIMPLE:
      // Simple 0° to 180° swing
      metro.swingMin1 = metro.swingMin2 = metro.swingMin3 = 0;
      metro.swingMax1 = metro.swingMax2 = metro.swingMax3 = 180;
      break;

    case SWING_PENDULUM:
      // Wide pendulum arc
      metro.swingMin1 = metro.swingMin2 = metro.swingMin3 = 45;
      metro.swingMax1 = metro.swingMax2 = metro.swingMax3 = 315;
      break;

    case SWING_ROTATE:
      // Full rotation (0° to 360°)
      metro.swingMin1 = metro.swingMin2 = metro.swingMin3 = 0;
      metro.swingMax1 = metro.swingMax2 = metro.swingMax3 = 360;
      break;

    case SWING_TRIPLE:
      // Each hand swings at different range, creating phase offset
      metro.swingMin1 = 0;   metro.swingMax1 = 180;
      metro.swingMin2 = 60;  metro.swingMax2 = 240;
      metro.swingMin3 = 120; metro.swingMax3 = 300;
      break;
  }

  // Start at min position
  metro.currentAngle1 = metro.targetAngle1 = metro.swingMin1;
  metro.currentAngle2 = metro.targetAngle2 = metro.swingMin2;
  metro.currentAngle3 = metro.targetAngle3 = metro.swingMin3;
}

// Generate new random metronome pattern
void generateMetronomePattern() {
  // Randomize patterns
  currentTempoPattern = (TempoPattern)random(5);
  currentSwingPattern = (SwingPattern)random(4);
  metroColorIndex = random(4);  // 0-3 for color variety

  // Initialize all pixels
  for (uint8_t i = 0; i < 24; i++) {
    initMetronomePixel(i, currentTempoPattern, currentSwingPattern);
  }

  Serial.print("New metronome pattern: Tempo=");
  Serial.print(currentTempoPattern);
  Serial.print(", Swing=");
  Serial.println(currentSwingPattern);
}

// Send metronome tick command to a single pixel
void sendMetronomeTick(uint8_t pixelId) {
  MetronomePixel& metro = metroPixels[pixelId];

  // Determine beat duration based on BPM
  unsigned long beatDuration = bpmToMs(metro.bpm);

  ESPNowPacket packet;
  packet.angleCmd.command = CMD_SET_ANGLES;
  packet.angleCmd.clearTargetMask();
  packet.angleCmd.setTargetPixel(pixelId);

  // Set angles to target position
  packet.angleCmd.setPixelAngles(
    pixelId,
    metro.targetAngle1,
    metro.targetAngle2,
    metro.targetAngle3,
    DIR_SHORTEST,  // Always take shortest path for crisp tick
    DIR_SHORTEST,
    DIR_SHORTEST
  );

  // Use smooth ease transition for gradual, weighted swing
  packet.angleCmd.transition = TRANSITION_EASE_IN_OUT;

  // Duration is 90% of full beat for smooth, deliberate movement
  packet.angleCmd.duration = floatToDuration(beatDuration * 0.9f / 1000.0f);  // Convert ms to seconds

  // Set color
  packet.angleCmd.setPixelStyle(pixelId, metroColorIndex, 255);

  // Send packet
  ESPNowComm::sendPacket(&packet, sizeof(AngleCommandPacket));

  // Update current angle
  metro.currentAngle1 = metro.targetAngle1;
  metro.currentAngle2 = metro.targetAngle2;
  metro.currentAngle3 = metro.targetAngle3;
}

// Send time display (pauses all metronomes and shows digits)
void sendMetroTimeDisplay() {
  // Get current time
  metroCurrentMinute = getCurrentMinute();

  Serial.print("Metronome showing time: ");
  Serial.print(metroCurrentMinute / 10);
  Serial.println(metroCurrentMinute % 10);

  // Use the shared digit display helper
  sendTwoDigitTime(
    metroCurrentMinute,
    metroColorIndex,
    TRANSITION_EASE_IN_OUT,
    1.0f,  // 1 second to form digits
    DIR_SHORTEST,
    DIR_SHORTEST,
    DIR_SHORTEST
  );
}

// Update display info
void updateMetronomeDisplay() {
  tft.fillScreen(COLOR_BG);
  tft.setTextColor(COLOR_ACCENT, COLOR_BG);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("METRONOME");

  // Display current time in top-right
  tft.setTextSize(1);
  tft.setTextColor(TFT_CYAN, COLOR_BG);
  tft.setTextDatum(TR_DATUM);
  tft.drawString(getCurrentTimeString(), 310, 10);
  tft.setTextDatum(TL_DATUM);

  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setTextSize(1);

  // Show current state
  tft.setCursor(10, 35);
  if (metroPhase == METRO_SHOWING_TIME) {
    tft.setTextColor(TFT_CYAN, COLOR_BG);
    tft.setTextSize(2);
    tft.print("Time: ");
    tft.print(metroCurrentMinute / 10);
    tft.println(metroCurrentMinute % 10);
    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
  } else {
    tft.print("State: Ticking");
  }

  tft.setCursor(10, 55);
  tft.print("Pattern: ");
  switch (currentTempoPattern) {
    case TEMPO_ROWS: tft.println("Rows"); break;
    case TEMPO_COLUMNS: tft.println("Columns"); break;
    case TEMPO_CHECKERBOARD: tft.println("Checkerboard"); break;
    case TEMPO_GRADIENT: tft.println("Gradient"); break;
    case TEMPO_RANDOM: tft.println("Random"); break;
  }

  tft.setCursor(10, 70);
  tft.print("Swing: ");
  switch (currentSwingPattern) {
    case SWING_SIMPLE: tft.println("Simple"); break;
    case SWING_PENDULUM: tft.println("Pendulum"); break;
    case SWING_ROTATE: tft.println("Rotate"); break;
    case SWING_TRIPLE: tft.println("Triple"); break;
  }

  // Show some BPM examples
  tft.setCursor(10, 90);
  tft.println("Tempos (BPM):");
  tft.setCursor(10, 105);
  tft.print("Pixel 0: ");
  tft.println(metroPixels[0].bpm);
  tft.setCursor(10, 120);
  tft.print("Pixel 8: ");
  tft.println(metroPixels[8].bpm);
  tft.setCursor(10, 135);
  tft.print("Pixel 16: ");
  tft.println(metroPixels[16].bpm);

  // Back hint
  tft.setCursor(10, 210);
  tft.setTextColor(TFT_DARKGREY, COLOR_BG);
  tft.println("Touch to return to menu");
}

// Main metronome loop handler
void handleMetronomeLoop(unsigned long currentTime) {
  // Send periodic pings
  if (currentTime - lastPingTime >= 3000) {
    sendPing();
    lastPingTime = currentTime;
  }

  // Check if it's time to show time display
  if (metroPhase == METRO_TICKING && currentTime - metroLastMinuteChange >= METRO_MINUTE_INTERVAL) {
    metroShouldShowTime = true;
    metroLastMinuteChange = currentTime;
  }

  switch (metroPhase) {
    case METRO_TICKING: {
      // Check if we should switch to time display
      if (metroShouldShowTime) {
        metroPhase = METRO_SHOWING_TIME;
        timeDisplayStart = currentTime;
        sendMetroTimeDisplay();
        updateMetronomeDisplay();
        metroShouldShowTime = false;
        break;
      }

      // Update each metronome tick
      for (uint8_t pixelId = 0; pixelId < 24; pixelId++) {
        MetronomePixel& metro = metroPixels[pixelId];
        unsigned long beatDuration = bpmToMs(metro.bpm);

        // Check if it's time for this metronome to tick
        if (currentTime - metro.lastBeat >= beatDuration / 2) {
          // Toggle direction
          metro.swingingUp = !metro.swingingUp;

          // Set new target angles
          if (metro.swingingUp) {
            metro.targetAngle1 = metro.swingMax1;
            metro.targetAngle2 = metro.swingMax2;
            metro.targetAngle3 = metro.swingMax3;
          } else {
            metro.targetAngle1 = metro.swingMin1;
            metro.targetAngle2 = metro.swingMin2;
            metro.targetAngle3 = metro.swingMin3;
          }

          // Send tick command
          sendMetronomeTick(pixelId);
          metro.lastBeat = currentTime;
        }
      }
      break;
    }

    case METRO_SHOWING_TIME: {
      // Hold time display for duration
      if (currentTime - timeDisplayStart >= METRO_TIME_HOLD) {
        metroPhase = METRO_RESUMING;
        // Generate new metronome pattern
        generateMetronomePattern();
        updateMetronomeDisplay();

        // Send initial tick to all pixels
        for (uint8_t i = 0; i < 24; i++) {
          sendMetronomeTick(i);
          delay(20);  // Small delay between pixels
        }

        // Immediately return to ticking
        metroPhase = METRO_TICKING;
      }
      break;
    }

    case METRO_RESUMING:
      // Handled inline above
      break;
  }
}

#endif // METRONOME_TIME_ANIMATION_H
