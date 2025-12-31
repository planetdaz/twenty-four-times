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

// Timing
unsigned long lastCommandTime = 0;
const unsigned long COMMAND_INTERVAL = 5000;  // Send new command every 5 seconds

// Test pattern state
int patternIndex = 0;

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
  tft.print(COMMAND_INTERVAL / 1000);
  tft.println(" sec");

  // MAC address
  tft.setCursor(10, 220);
  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY, COLOR_BG);
  tft.print("MAC: ");
  tft.println(ESPNowComm::getMacAddress());
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
    Serial.println("ESP-NOW sender initialized!");
    Serial.println("Broadcasting test patterns every 5 seconds...\n");

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

  lastCommandTime = millis();

  // Show first pattern immediately
  sendPattern(patterns[0]);
}

void loop() {
  unsigned long currentTime = millis();
  
  // Send a new pattern every COMMAND_INTERVAL milliseconds
  if (currentTime - lastCommandTime >= COMMAND_INTERVAL) {
    sendPattern(patterns[patternIndex]);
    
    // Move to next pattern
    patternIndex = (patternIndex + 1) % numPatterns;
    
    lastCommandTime = currentTime;
  }
  
  // Small delay to avoid busy-waiting
  delay(10);
}

