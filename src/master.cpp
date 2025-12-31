#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <TFT_eSPI.h>
#include <CST816S.h>
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

// ===== DISPLAY SETUP =====
TFT_eSPI tft = TFT_eSPI();
CST816S touch(TOUCH_SDA, TOUCH_SCL, TOUCH_RST, TOUCH_INT);

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

// ===== FUNCTIONS =====

// Read touch input (CST816S capacitive touch)
bool readTouch(uint16_t &x, uint16_t &y) {
  if (touch.available()) {
    // Debounce
    unsigned long now = millis();
    if (now - lastTouchTime < TOUCH_DEBOUNCE) {
      return false;
    }
    lastTouchTime = now;

    // Get touch data
    x = touch.data.x;
    y = touch.data.y;

    // CST816S returns coordinates in portrait mode (240x320)
    // We're using landscape mode, so we need to rotate
    // Portrait: x=0-240, y=0-320
    // Landscape: x=0-320, y=0-240
    // Rotation 1 (landscape): new_x = y, new_y = 240 - x
    uint16_t temp_x = y;
    y = 240 - x;
    x = temp_x;

    return true;
  }
  return false;
}

// Draw main menu
void drawMenu() {
  tft.fillScreen(COLOR_BG);

  // Title
  tft.setTextColor(COLOR_ACCENT, COLOR_BG);
  tft.setTextSize(3);
  tft.setCursor(20, 10);
  tft.println("Twenty-Four");
  tft.setCursor(80, 40);
  tft.println("Times");

  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY, COLOR_BG);
  tft.setCursor(70, 70);
  tft.println("Select Mode:");

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

  // Button 4: Manual (bottom right) - disabled for now
  tft.fillRoundRect(170, 160, 140, 60, 8, TFT_DARKGREY);
  tft.setTextColor(TFT_LIGHTGREY, TFT_DARKGREY);
  tft.setTextSize(2);
  tft.setCursor(195, 175);
  tft.println("Manual");
  tft.setTextSize(1);
  tft.setCursor(185, 195);
  tft.println("Coming soon");
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
  // Button 4: Manual - disabled
  // if (x >= 170 && x <= 310 && y >= 160 && y <= 220) {
  //   return MODE_MANUAL;
  // }

  return MODE_MENU;  // No button pressed
}

// Send a random pattern (simulation mode)
void sendRandomPattern() {
  ESPNowPacket packet;
  packet.angleCmd.command = CMD_SET_ANGLES;
  packet.angleCmd.transition = getRandomTransition();
  packet.angleCmd.duration = floatToDuration(getRandomDuration());

  // Set random angles, directions, colors, and opacity for all pixels
  for (int i = 0; i < MAX_PIXELS; i++) {
    float angle1 = getRandomAngle();
    float angle2 = getRandomAngle();
    float angle3 = getRandomAngle();

    packet.angleCmd.setPixelAngles(i, angle1, angle2, angle3,
                                    DIR_SHORTEST, DIR_SHORTEST, DIR_SHORTEST);

    packet.angleCmd.setPixelStyle(i, getRandomColorIndex(), getRandomOpacity());
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
    tft.println("Random angles, colors, opacity");
    tft.println("Broadcasting to 24 pixels...");

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

    tft.setTextSize(1);
    tft.setCursor(70, 160);
    tft.println("Duration: 5 seconds");
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

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n========== MASTER CONTROLLER ==========");
  Serial.println("Twenty-Four Times - ESP-NOW Master (CYD)");
  Serial.println("=======================================\n");

  // Initialize backlight
  pinMode(TFT_BACKLIGHT, OUTPUT);
  digitalWrite(TFT_BACKLIGHT, HIGH);

  // Initialize I2C for touch
  Wire.begin(TOUCH_SDA, TOUCH_SCL);

  // Initialize touch controller
  touch.begin();
  Serial.println("Touch controller initialized");

  // Initialize TFT
  tft.init();
  tft.setRotation(1);  // Landscape mode (320x240)
  tft.fillScreen(COLOR_BG);

  // Show startup screen
  tft.setTextColor(COLOR_ACCENT, COLOR_BG);
  tft.setTextSize(3);
  tft.setCursor(20, 80);
  tft.println("Twenty-Four");
  tft.setCursor(80, 110);
  tft.println("Times");
  tft.setTextSize(1);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setCursor(80, 150);
  tft.println("Initializing...");

  delay(1000);

  // Initialize ESP-NOW in sender mode
  if (ESPNowComm::initSender(ESPNOW_CHANNEL)) {

    tft.fillScreen(COLOR_BG);
    tft.setTextColor(COLOR_ACCENT, COLOR_BG);
    tft.setTextSize(2);
    tft.setCursor(40, 100);
    tft.println("ESP-NOW Ready!");
    delay(1000);
  } else {
    Serial.println("ESP-NOW initialization failed!");

    tft.fillScreen(TFT_RED);
    tft.setTextColor(TFT_WHITE, TFT_RED);
    tft.setTextSize(2);
    tft.setCursor(20, 100);
    tft.println("ESP-NOW FAILED!");

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
          default:
            break;
        }
      }
    } else {
      // Any touch in non-menu mode returns to menu
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
      // Future: manual control interface
      break;
  }

  // Small delay to avoid busy-waiting
  delay(10);
}

