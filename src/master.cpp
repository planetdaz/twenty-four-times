#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <TFT_eSPI.h>
#include <ESPNowComm.h>

// ===== MASTER CONTROLLER FOR CYD (Capacitive Touch) =====
// This firmware runs on a CYD (Cheap Yellow Display) board
// and broadcasts synchronized commands to all pixel displays via ESP-NOW

// ===== BOARD CONFIGURATION =====
#define TFT_BACKLIGHT 27
#define TOUCH_SDA 33
#define TOUCH_SCL 32
#define TOUCH_INT 21
#define TOUCH_RST 25
#define CST816S_ADDR 0x15

// ===== DISPLAY SETUP =====
TFT_eSPI tft = TFT_eSPI();

// Display dimensions
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240

// ===== COLOR DEFINITIONS =====
#define COLOR_BG      TFT_BLACK
#define COLOR_TEXT    TFT_WHITE
#define COLOR_ACCENT  TFT_GREEN
#define COLOR_PATTERN TFT_CYAN
#define TFT_DARKBLUE  0x0010  // Dark blue color (RGB565)

// ===== CONTROL MODES =====
enum ControlMode {
  MODE_MENU,        // Main menu - select mode
  MODE_SIMULATION,  // Random patterns like the simulation
  MODE_PATTERNS,    // Cycle through test patterns
  MODE_IDENTIFY,    // Identify all pixels
  MODE_MANUAL       // Manual control (future)
};

ControlMode currentMode = MODE_MENU;

// Timing
unsigned long lastCommandTime = 0;
unsigned long modeStartTime = 0;
const unsigned long SIMULATION_INTERVAL = 5000;  // 5 seconds between random patterns
const unsigned long PATTERN_INTERVAL = 5000;     // 5 seconds between test patterns
const unsigned long IDENTIFY_DURATION = 5000;    // Identify phase duration

// Manual mode state
struct ManualState {
  uint8_t selectedPixel = 0;     // Currently selected pixel (0-23)
  float angles[3] = {0, 0, 0};   // Angles for the 3 hands
  uint8_t colorIndex = 0;        // Color palette index
  uint8_t opacity = 255;         // Opacity (0-255)
  TransitionType transition = TRANSITION_LINEAR;
  float duration = 2.0;          // Duration in seconds
} manualState;

// Test pattern state
int patternIndex = 0;

// Touch state
uint16_t touchX = 0, touchY = 0;
bool touched = false;
unsigned long lastTouchTime = 0;
const unsigned long TOUCH_DEBOUNCE = 200;  // 200ms debounce

// ===== TEST PATTERNS =====

struct TestPattern {
  const char* name;
  float angles[24][3];  // 24 pixels, 3 hands each
  TransitionType transition;
  float duration_sec;   // Duration in seconds
  uint8_t colorIndex;   // Color palette index
  uint8_t opacity;      // Opacity (0-255)
};

// Pattern 1: All hands pointing up (0°) - White on Black
TestPattern pattern_all_up = {
  "All Up",
  {
    {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0},
    {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0},
    {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0},
    {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}
  },
  TRANSITION_ELASTIC,
  3.0,  // 3 seconds
  0,    // White on Black
  255   // Full opacity
};

// Pattern 2: All hands pointing right (90°) - Black on White
TestPattern pattern_all_right = {
  "All Right",
  {
    {90, 90, 90}, {90, 90, 90}, {90, 90, 90}, {90, 90, 90}, {90, 90, 90}, {90, 90, 90},
    {90, 90, 90}, {90, 90, 90}, {90, 90, 90}, {90, 90, 90}, {90, 90, 90}, {90, 90, 90},
    {90, 90, 90}, {90, 90, 90}, {90, 90, 90}, {90, 90, 90}, {90, 90, 90}, {90, 90, 90},
    {90, 90, 90}, {90, 90, 90}, {90, 90, 90}, {90, 90, 90}, {90, 90, 90}, {90, 90, 90}
  },
  TRANSITION_EASE_IN_OUT,
  2.0,  // 2 seconds
  1,    // Black on White
  255   // Full opacity
};

// Pattern 3: All hands pointing down (180°) - Dark Brown on Cream
TestPattern pattern_all_down = {
  "All Down",
  {
    {180, 180, 180}, {180, 180, 180}, {180, 180, 180}, {180, 180, 180}, {180, 180, 180}, {180, 180, 180},
    {180, 180, 180}, {180, 180, 180}, {180, 180, 180}, {180, 180, 180}, {180, 180, 180}, {180, 180, 180},
    {180, 180, 180}, {180, 180, 180}, {180, 180, 180}, {180, 180, 180}, {180, 180, 180}, {180, 180, 180},
    {180, 180, 180}, {180, 180, 180}, {180, 180, 180}, {180, 180, 180}, {180, 180, 180}, {180, 180, 180}
  },
  TRANSITION_LINEAR,
  2.5,  // 2.5 seconds
  2,    // Dark Brown on Cream
  255   // Full opacity
};

// Pattern 4: All hands pointing left (270°) - White on Deep Sky Blue
TestPattern pattern_all_left = {
  "All Left",
  {
    {270, 270, 270}, {270, 270, 270}, {270, 270, 270}, {270, 270, 270}, {270, 270, 270}, {270, 270, 270},
    {270, 270, 270}, {270, 270, 270}, {270, 270, 270}, {270, 270, 270}, {270, 270, 270}, {270, 270, 270},
    {270, 270, 270}, {270, 270, 270}, {270, 270, 270}, {270, 270, 270}, {270, 270, 270}, {270, 270, 270},
    {270, 270, 270}, {270, 270, 270}, {270, 270, 270}, {270, 270, 270}, {270, 270, 270}, {270, 270, 270}
  },
  TRANSITION_BOUNCE,
  3.5,  // 3.5 seconds
  10,   // White on Deep Sky Blue
  255   // Full opacity
};

// Pattern 5: Staggered - each pixel different - Ivory on Deep Pink
TestPattern pattern_staggered = {
  "Staggered",
  {
    {0, 90, 180}, {90, 180, 270}, {180, 270, 0}, {270, 0, 90},
    {0, 90, 180}, {90, 180, 270}, {180, 270, 0}, {270, 0, 90},
    {0, 90, 180}, {90, 180, 270}, {180, 270, 0}, {270, 0, 90},
    {0, 90, 180}, {90, 180, 270}, {180, 270, 0}, {270, 0, 90},
    {0, 90, 180}, {90, 180, 270}, {180, 270, 0}, {270, 0, 90},
    {0, 90, 180}, {90, 180, 270}, {180, 270, 0}, {270, 0, 90}
  },
  TRANSITION_ELASTIC,
  4.0,  // 4 seconds
  11,   // Ivory on Deep Pink
  255   // Full opacity
};

// Array of all patterns
TestPattern* patterns[] = {
  &pattern_all_up,
  &pattern_all_right,
  &pattern_all_down,
  &pattern_all_left,
  &pattern_staggered
};
const int numPatterns = 5;

// ===== FUNCTION DECLARATIONS =====
void drawManualScreen();
void handleManualTouch(uint16_t x, uint16_t y);
void sendManualCommand(bool allPixels);

// ===== FUNCTIONS =====

// Read touch input (CST816S capacitive touch - direct I2C register reads)
bool readTouch(uint16_t &x, uint16_t &y) {
  // Debounce
  unsigned long now = millis();
  if (now - lastTouchTime < TOUCH_DEBOUNCE) {
    return false;
  }

  // CST816S Capacitive Touch - Direct I2C register reads
  Wire.beginTransmission(CST816S_ADDR);
  Wire.write(0x02);  // Start at finger count register
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  Wire.requestFrom(CST816S_ADDR, 5);
  if (Wire.available() >= 5) {
    uint8_t fingers = Wire.read();  // 0x02 - finger count
    uint8_t xh = Wire.read();       // 0x03
    uint8_t xl = Wire.read();       // 0x04
    uint8_t yh = Wire.read();       // 0x05
    uint8_t yl = Wire.read();       // 0x06

    if (fingers > 0) {
      uint16_t rawX = ((xh & 0x0F) << 8) | xl;
      uint16_t rawY = ((yh & 0x0F) << 8) | yl;

      // Map for landscape rotation (rotation=1)
      x = rawY;
      y = 240 - rawX;

      // Clamp to screen bounds
      x = constrain(x, 0, 319);
      y = constrain(y, 0, 239);

      lastTouchTime = now;
      return true;
    }
  }
  return false;
}

// Draw main menu
void drawMenu() {
  tft.fillScreen(COLOR_BG);

  // Title - centered for landscape mode (320x240)
  tft.setTextColor(COLOR_ACCENT, COLOR_BG);
  tft.setTextSize(3);
  tft.setTextDatum(TC_DATUM);  // Top center alignment
  tft.drawString("Twenty-Four", 160, 10);
  tft.drawString("Times", 160, 40);

  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY, COLOR_BG);
  tft.drawString("Select Mode:", 160, 70);
  tft.setTextDatum(TL_DATUM);  // Reset to top-left for buttons

  // Menu buttons (4 buttons in 2x2 grid)
  // Button layout: 160x100 each, 10px padding

  // Button 1: Simulation Mode (top left)
  tft.fillRoundRect(10, 90, 150, 60, 8, TFT_DARKGREEN);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREEN);
  tft.setTextSize(2);
  tft.setCursor(25, 105);
  tft.println("Simulation");
  tft.setTextSize(1);
  tft.setCursor(30, 125);
  tft.println("Random patterns");

  // Button 2: Test Patterns (top right)
  tft.fillRoundRect(170, 90, 140, 60, 8, TFT_DARKBLUE);
  tft.setTextColor(TFT_WHITE, TFT_DARKBLUE);
  tft.setTextSize(2);
  tft.setCursor(185, 105);
  tft.println("Patterns");
  tft.setTextSize(1);
  tft.setCursor(185, 125);
  tft.println("Cycle tests");

  // Button 3: Identify (bottom left)
  tft.fillRoundRect(10, 160, 150, 60, 8, TFT_PURPLE);
  tft.setTextColor(TFT_WHITE, TFT_PURPLE);
  tft.setTextSize(2);
  tft.setCursor(35, 175);
  tft.println("Identify");
  tft.setTextSize(1);
  tft.setCursor(30, 195);
  tft.println("Show pixel IDs");

  // Button 4: Manual (bottom right)
  tft.fillRoundRect(170, 160, 140, 60, 8, TFT_ORANGE);
  tft.setTextColor(TFT_WHITE, TFT_ORANGE);
  tft.setTextSize(2);
  tft.setCursor(195, 175);
  tft.println("Manual");
  tft.setTextSize(1);
  tft.setCursor(180, 195);
  tft.println("Direct control");
}

// Check which menu button was pressed
ControlMode checkMenuTouch(uint16_t x, uint16_t y) {
  // Button 1: Simulation (10, 90, 150, 60)
  if (x >= 10 && x <= 160 && y >= 90 && y <= 150) {
    return MODE_SIMULATION;
  }
  // Button 2: Patterns (170, 90, 140, 60)
  if (x >= 170 && x <= 310 && y >= 90 && y <= 150) {
    return MODE_PATTERNS;
  }
  // Button 3: Identify (10, 160, 150, 60)
  if (x >= 10 && x <= 160 && y >= 160 && y <= 220) {
    return MODE_IDENTIFY;
  }
  // Button 4: Manual (170, 160, 140, 60)
  if (x >= 170 && x <= 310 && y >= 160 && y <= 220) {
    return MODE_MANUAL;
  }

  return MODE_MENU;  // No button pressed
}

// Send a random pattern (simulation mode)
void sendRandomPattern() {
  ESPNowPacket packet;
  packet.angleCmd.command = CMD_SET_ANGLES;
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
    Serial.print("Sent random pattern: ");
    Serial.print(getTransitionName(packet.angleCmd.transition));
    Serial.print(", duration: ");
    Serial.print(durationToFloat(packet.angleCmd.duration), 1);
    Serial.println("s");

    // Update display
    tft.fillScreen(COLOR_BG);
    tft.setTextColor(COLOR_ACCENT, COLOR_BG);
    tft.setTextSize(2);
    tft.setCursor(10, 10);
    tft.println("SIMULATION MODE");

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
    Serial.println("Failed to send random pattern!");
  }
}

void updateDisplay(TestPattern* pattern) {
  tft.fillScreen(COLOR_BG);

  // Title
  tft.setTextColor(COLOR_ACCENT, COLOR_BG);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("Twenty-Four Times");
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setTextSize(1);
  tft.setCursor(10, 35);
  tft.println("ESP-NOW Master Controller");

  // Pattern info
  tft.setTextSize(2);
  tft.setTextColor(COLOR_PATTERN, COLOR_BG);
  tft.setCursor(10, 70);
  tft.print("Pattern: ");
  tft.println(pattern->name);

  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setTextSize(1);
  tft.setCursor(10, 95);
  tft.print("Duration: ");
  tft.print(pattern->duration_sec, 1);
  tft.println(" sec");

  tft.setCursor(10, 110);
  tft.print("Transition: ");
  tft.println(getTransitionName(pattern->transition));

  tft.setCursor(10, 125);
  tft.print("Color: ");
  if (pattern->colorIndex < COLOR_PALETTE_SIZE) {
    tft.println(COLOR_PALETTE[pattern->colorIndex].name);
  } else {
    tft.println("Invalid");
  }

  tft.setCursor(10, 140);
  tft.print("Opacity: ");
  tft.println(pattern->opacity);

  // Status
  tft.setCursor(10, 165);
  tft.setTextColor(COLOR_ACCENT, COLOR_BG);
  tft.println("Broadcasting to 24 pixels...");

  // Next pattern countdown
  tft.setCursor(10, 190);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.print("Next in ");
  tft.print(PATTERN_INTERVAL / 1000);
  tft.println(" sec");

  // Back to menu hint
  tft.setCursor(10, 210);
  tft.setTextColor(TFT_YELLOW, COLOR_BG);
  tft.println("Touch screen to return to menu");

  // MAC address
  tft.setCursor(10, 230);
  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY, COLOR_BG);
  tft.print("MAC: ");
  tft.println(ESPNowComm::getMacAddress());
}

void sendIdentifyCommand(uint8_t pixelId) {
  ESPNowPacket packet;
  packet.identify.command = CMD_IDENTIFY;
  packet.identify.pixelId = pixelId;

  if (ESPNowComm::sendPacket(&packet, sizeof(IdentifyPacket))) {
    Serial.print("Sent IDENTIFY command for pixel ");
    if (pixelId == 255) {
      Serial.println("ALL");
    } else {
      Serial.println(pixelId);
    }

    // Update display
    tft.fillScreen(TFT_BLUE);
    tft.setTextColor(TFT_WHITE, TFT_BLUE);
    tft.setTextSize(3);
    tft.setCursor(40, 80);
    tft.println("IDENTIFY MODE");

    tft.setTextSize(2);
    tft.setCursor(60, 120);
    if (pixelId == 255) {
      tft.println("All Pixels");
    } else {
      tft.print("Pixel ID: ");
      tft.println(pixelId);
    }

  } else {
    Serial.println("Failed to send IDENTIFY command!");
  }
}

void sendPattern(TestPattern* pattern) {
  ESPNowPacket packet;
  packet.angleCmd.command = CMD_SET_ANGLES;
  packet.angleCmd.transition = pattern->transition;
  packet.angleCmd.duration = floatToDuration(pattern->duration_sec);

  // Set angles and directions for all pixels
  for (int i = 0; i < MAX_PIXELS; i++) {
    // Set angles with shortest path direction (default)
    packet.angleCmd.setPixelAngles(i,
      pattern->angles[i][0],
      pattern->angles[i][1],
      pattern->angles[i][2],
      DIR_SHORTEST,  // Let pixel choose shortest path
      DIR_SHORTEST,
      DIR_SHORTEST
    );

    // Set color and opacity for each pixel
    packet.angleCmd.setPixelStyle(i, pattern->colorIndex, pattern->opacity);
  }

  // Send the packet
  if (ESPNowComm::sendPacket(&packet, sizeof(AngleCommandPacket))) {
    Serial.print("Sent pattern: ");
    Serial.print(pattern->name);
    Serial.print(" (duration: ");
    Serial.print(pattern->duration_sec, 1);
    Serial.print("s, color: ");
    Serial.print(pattern->colorIndex);
    Serial.print(", opacity: ");
    Serial.print(pattern->opacity);
    Serial.println(")");

    // Update display
    updateDisplay(pattern);
  } else {
    Serial.println("Failed to send packet!");

    // Show error on display
    tft.fillScreen(TFT_RED);
    tft.setTextColor(TFT_WHITE, TFT_RED);
    tft.setTextSize(3);
    tft.setCursor(60, 100);
    tft.println("SEND FAILED!");
  }
}

// ===== MANUAL MODE FUNCTIONS =====

void drawManualScreen() {
  tft.fillScreen(COLOR_BG);

  // Title
  tft.setTextColor(COLOR_ACCENT, COLOR_BG);
  tft.setTextSize(2);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("Manual Control", 160, 5);
  tft.setTextDatum(TL_DATUM);

  // Pixel selector
  tft.setTextSize(1);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setCursor(5, 30);
  tft.print("Pixel: ");
  tft.print(manualState.selectedPixel);

  // Prev/Next buttons for pixel selection
  tft.fillRoundRect(60, 28, 30, 16, 4, TFT_DARKGREY);
  tft.fillRoundRect(95, 28, 30, 16, 4, TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.setCursor(68, 30);
  tft.print("<");
  tft.setCursor(103, 30);
  tft.print(">");

  // Angle controls (3 rows)
  const char* handNames[] = {"Hand 1:", "Hand 2:", "Hand 3:"};
  for (int i = 0; i < 3; i++) {
    int y = 55 + i * 30;
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.setCursor(5, y);
    tft.print(handNames[i]);
    tft.setCursor(55, y);
    tft.printf("%3.0f", manualState.angles[i]);
    tft.print((char)247);  // Degree symbol

    // -/+ buttons
    tft.fillRoundRect(100, y - 2, 25, 16, 4, TFT_DARKGREY);
    tft.fillRoundRect(130, y - 2, 25, 16, 4, TFT_DARKGREY);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
    tft.setCursor(108, y);
    tft.print("-");
    tft.setCursor(138, y);
    tft.print("+");
  }

  // Color selector
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setCursor(5, 145);
  tft.print("Color:");
  tft.fillRoundRect(50, 143, 30, 16, 4, TFT_DARKGREY);
  tft.fillRoundRect(85, 143, 30, 16, 4, TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.setCursor(58, 145);
  tft.print("<");
  tft.setCursor(93, 145);
  tft.print(">");
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setCursor(120, 145);
  tft.print(manualState.colorIndex);

  // Opacity selector
  tft.setCursor(5, 165);
  tft.print("Opacity:");
  tft.fillRoundRect(60, 163, 30, 16, 4, TFT_DARKGREY);
  tft.fillRoundRect(95, 163, 30, 16, 4, TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.setCursor(68, 165);
  tft.print("-");
  tft.setCursor(103, 165);
  tft.print("+");
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setCursor(130, 165);
  tft.print(manualState.opacity);

  // Duration selector
  tft.setCursor(5, 185);
  tft.print("Duration:");
  tft.fillRoundRect(70, 183, 30, 16, 4, TFT_DARKGREY);
  tft.fillRoundRect(105, 183, 30, 16, 4, TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.setCursor(78, 185);
  tft.print("-");
  tft.setCursor(113, 185);
  tft.print("+");
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setCursor(140, 185);
  tft.printf("%.1fs", manualState.duration);

  // Send button (large, prominent)
  tft.fillRoundRect(170, 30, 140, 50, 8, TFT_GREEN);
  tft.setTextColor(TFT_WHITE, TFT_GREEN);
  tft.setTextSize(2);
  tft.setCursor(195, 45);
  tft.println("SEND");

  // All Pixels button
  tft.fillRoundRect(170, 90, 140, 35, 8, TFT_BLUE);
  tft.setTextColor(TFT_WHITE, TFT_BLUE);
  tft.setTextSize(1);
  tft.setCursor(185, 100);
  tft.println("Send to ALL Pixels");

  // Reset button
  tft.fillRoundRect(170, 135, 140, 35, 8, TFT_MAROON);
  tft.setTextColor(TFT_WHITE, TFT_MAROON);
  tft.setCursor(200, 145);
  tft.println("Reset to 0");

  // Back button
  tft.fillRoundRect(170, 180, 140, 35, 8, TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.setCursor(205, 190);
  tft.println("< Menu");
}

void handleManualTouch(uint16_t x, uint16_t y) {
  bool needsRedraw = false;

  // Pixel prev/next (60, 28, 30, 16) and (95, 28, 30, 16)
  if (y >= 28 && y <= 44) {
    if (x >= 60 && x <= 90) {
      manualState.selectedPixel = (manualState.selectedPixel == 0) ? 23 : manualState.selectedPixel - 1;
      needsRedraw = true;
    } else if (x >= 95 && x <= 125) {
      manualState.selectedPixel = (manualState.selectedPixel + 1) % 24;
      needsRedraw = true;
    }
  }

  // Angle controls (3 rows at y=55, 85, 115)
  for (int i = 0; i < 3; i++) {
    int y_base = 55 + i * 30;
    if (y >= y_base - 2 && y <= y_base + 14) {
      // Minus button (100, y-2, 25, 16)
      if (x >= 100 && x <= 125) {
        manualState.angles[i] -= 15;
        if (manualState.angles[i] < 0) manualState.angles[i] += 360;
        needsRedraw = true;
      }
      // Plus button (130, y-2, 25, 16)
      else if (x >= 130 && x <= 155) {
        manualState.angles[i] += 15;
        if (manualState.angles[i] >= 360) manualState.angles[i] -= 360;
        needsRedraw = true;
      }
    }
  }

  // Color prev/next (50, 143, 30, 16) and (85, 143, 30, 16)
  if (y >= 143 && y <= 159) {
    if (x >= 50 && x <= 80) {
      manualState.colorIndex = (manualState.colorIndex == 0) ? (COLOR_PALETTE_SIZE - 1) : manualState.colorIndex - 1;
      needsRedraw = true;
    } else if (x >= 85 && x <= 115) {
      manualState.colorIndex = (manualState.colorIndex + 1) % COLOR_PALETTE_SIZE;
      needsRedraw = true;
    }
  }

  // Opacity -/+ (60, 163, 30, 16) and (95, 163, 30, 16)
  if (y >= 163 && y <= 179) {
    if (x >= 60 && x <= 90) {
      if (manualState.opacity >= 25) manualState.opacity -= 25;
      needsRedraw = true;
    } else if (x >= 95 && x <= 125) {
      if (manualState.opacity <= 230) manualState.opacity += 25;
      needsRedraw = true;
    }
  }

  // Duration -/+ (70, 183, 30, 16) and (105, 183, 30, 16)
  if (y >= 183 && y <= 199) {
    if (x >= 70 && x <= 100) {
      if (manualState.duration > 0.5) manualState.duration -= 0.5;
      needsRedraw = true;
    } else if (x >= 105 && x <= 135) {
      if (manualState.duration < 10.0) manualState.duration += 0.5;
      needsRedraw = true;
    }
  }

  // SEND button (170, 30, 140, 50)
  if (x >= 170 && x <= 310 && y >= 30 && y <= 80) {
    sendManualCommand(false);  // Send to selected pixel only
    return;
  }

  // Send to ALL button (170, 90, 140, 35)
  if (x >= 170 && x <= 310 && y >= 90 && y <= 125) {
    sendManualCommand(true);  // Send to all pixels
    return;
  }

  // Reset button (170, 135, 140, 35)
  if (x >= 170 && x <= 310 && y >= 135 && y <= 170) {
    manualState.angles[0] = 0;
    manualState.angles[1] = 0;
    manualState.angles[2] = 0;
    needsRedraw = true;
  }

  // Back button (170, 180, 140, 35)
  if (x >= 170 && x <= 310 && y >= 180 && y <= 215) {
    currentMode = MODE_MENU;
    drawMenu();
    return;
  }

  if (needsRedraw) {
    drawManualScreen();
  }
}

void sendManualCommand(bool allPixels) {
  ESPNowPacket packet;
  packet.angleCmd.command = CMD_SET_ANGLES;
  packet.angleCmd.transition = manualState.transition;
  packet.angleCmd.duration = floatToDuration(manualState.duration);

  if (allPixels) {
    // Send same angles to all pixels
    for (int i = 0; i < MAX_PIXELS; i++) {
      packet.angleCmd.setPixelAngles(i,
        manualState.angles[0],
        manualState.angles[1],
        manualState.angles[2],
        DIR_SHORTEST, DIR_SHORTEST, DIR_SHORTEST);
      packet.angleCmd.setPixelStyle(i, manualState.colorIndex, manualState.opacity);
    }
    Serial.println("Sending manual command to ALL pixels");
  } else {
    // Send to selected pixel only, others get current angles (no change)
    for (int i = 0; i < MAX_PIXELS; i++) {
      if (i == manualState.selectedPixel) {
        packet.angleCmd.setPixelAngles(i,
          manualState.angles[0],
          manualState.angles[1],
          manualState.angles[2],
          DIR_SHORTEST, DIR_SHORTEST, DIR_SHORTEST);
        packet.angleCmd.setPixelStyle(i, manualState.colorIndex, manualState.opacity);
      } else {
        // Keep other pixels at their current state (send 0,0,0 with instant transition)
        packet.angleCmd.setPixelAngles(i, 0, 0, 0, DIR_SHORTEST, DIR_SHORTEST, DIR_SHORTEST);
        packet.angleCmd.setPixelStyle(i, 0, 0);  // Invisible
      }
    }
    Serial.print("Sending manual command to pixel ");
    Serial.println(manualState.selectedPixel);
  }

  if (ESPNowComm::sendPacket(&packet, sizeof(AngleCommandPacket))) {
    Serial.println("Manual command sent successfully");

    // Brief visual feedback
    tft.fillRoundRect(170, 30, 140, 50, 8, TFT_DARKGREEN);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREEN);
    tft.setTextSize(2);
    tft.setCursor(200, 45);
    tft.println("SENT!");
    delay(200);
    drawManualScreen();
  } else {
    Serial.println("Failed to send manual command");

    // Error feedback
    tft.fillRoundRect(170, 30, 140, 50, 8, TFT_RED);
    tft.setTextColor(TFT_WHITE, TFT_RED);
    tft.setTextSize(2);
    tft.setCursor(185, 45);
    tft.println("FAILED!");
    delay(500);
    drawManualScreen();
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n========== MASTER CONTROLLER ==========");
  Serial.println("Twenty-Four Times - ESP-NOW Master (CYD)");
  Serial.println("=======================================\n");

  // Initialize backlight
  pinMode(TFT_BACKLIGHT, OUTPUT);
  digitalWrite(TFT_BACKLIGHT, HIGH);

  // Initialize I2C for touch (CST816S)
  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  Serial.println("I2C initialized for touch controller");

  // Initialize TFT
  tft.init();
  tft.setRotation(1);  // Landscape mode (320x240)
  tft.fillScreen(COLOR_BG);

  // Show startup screen - centered for landscape mode (320x240)
  tft.setTextColor(COLOR_ACCENT, COLOR_BG);
  tft.setTextSize(3);
  tft.setTextDatum(TC_DATUM);  // Top center alignment
  tft.drawString("Twenty-Four", 160, 80);
  tft.drawString("Times", 160, 110);
  tft.setTextSize(1);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.drawString("Initializing...", 160, 150);

  delay(1000);

  // Initialize ESP-NOW in sender mode
  if (ESPNowComm::initSender(ESPNOW_CHANNEL)) {

    tft.fillScreen(COLOR_BG);
    tft.setTextColor(COLOR_ACCENT, COLOR_BG);
    tft.setTextSize(2);
    tft.setTextDatum(MC_DATUM);  // Middle center alignment
    tft.drawString("ESP-NOW Ready!", 160, 120);
    delay(1000);
  } else {
    Serial.println("ESP-NOW initialization failed!");

    tft.fillScreen(TFT_RED);
    tft.setTextColor(TFT_WHITE, TFT_RED);
    tft.setTextSize(2);
    tft.setTextDatum(MC_DATUM);  // Middle center alignment
    tft.drawString("ESP-NOW FAILED!", 160, 120);

    while (1) delay(1000);  // Halt
  }

  // Show menu
  drawMenu();
  currentMode = MODE_MENU;
  modeStartTime = millis();
}

void loop() {
  unsigned long currentTime = millis();

  // Check for touch input
  uint16_t tx, ty;
  if (readTouch(tx, ty)) {
    Serial.print("Touch detected at: ");
    Serial.print(tx);
    Serial.print(", ");
    Serial.println(ty);

    // Handle touch based on current mode
    if (currentMode == MODE_MENU) {
      // Check which button was pressed
      ControlMode newMode = checkMenuTouch(tx, ty);
      if (newMode != MODE_MENU) {
        currentMode = newMode;
        modeStartTime = currentTime;
        lastCommandTime = currentTime;
        patternIndex = 0;

        Serial.print("Mode changed to: ");
        Serial.println(newMode);

        // Initialize the new mode
        switch (currentMode) {
          case MODE_SIMULATION:
            sendRandomPattern();
            break;
          case MODE_PATTERNS:
            sendPattern(patterns[0]);
            break;
          case MODE_IDENTIFY:
            sendIdentifyCommand(255);
            break;
          case MODE_MANUAL:
            drawManualScreen();
            break;
          default:
            break;
        }
      }
    } else if (currentMode == MODE_MANUAL) {
      // Manual mode has its own touch handler
      handleManualTouch(tx, ty);
    } else {
      // Any touch in other modes returns to menu
      currentMode = MODE_MENU;
      drawMenu();
      Serial.println("Returned to menu");
    }
  }

  // Handle mode-specific logic
  switch (currentMode) {
    case MODE_MENU:
      // Nothing to do - waiting for touch
      break;

    case MODE_SIMULATION: {
      // Send random patterns every SIMULATION_INTERVAL, starting after previous animation completes
      if (currentTime - lastCommandTime >= SIMULATION_INTERVAL) {
        sendRandomPattern();
        lastCommandTime = currentTime;
      }
      break;
    }

    case MODE_PATTERNS: {
      // Cycle through test patterns every PATTERN_INTERVAL
      if (currentTime - lastCommandTime >= PATTERN_INTERVAL) {
        sendPattern(patterns[patternIndex]);
        patternIndex = (patternIndex + 1) % numPatterns;
        lastCommandTime = currentTime;
      }
      break;
    }

    case MODE_IDENTIFY: {
      // Identify mode runs once, then waits for touch to return to menu
      // Display stays on identify screen until touched
      break;
    }

    case MODE_MANUAL:
      // Manual mode is fully touch-driven, no automatic updates
      break;
  }

  // Small delay to avoid busy-waiting
  delay(10);
}

