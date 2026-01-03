#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <TFT_eSPI.h>
#include <ESPNowComm.h>

// ===== MASTER CONTROLLER FOR CYD =====
// This firmware runs on a CYD (Cheap Yellow Display) board
// and broadcasts synchronized commands to all pixel displays via ESP-NOW
// Supports both capacitive and resistive touch versions

// ===== BOARD-SPECIFIC CONFIGURATION =====
#if defined(BOARD_CYD_RESISTIVE)
  // ESP32-2432S028R (E32R28T) with XPT2046 Resistive Touch
  // Touch uses SEPARATE SPI bus from display!
  // Touch SPI: SCLK=25, MOSI=32, MISO=39, CS=33, IRQ=36
  #include <XPT2046_Touchscreen.h>

  #define TOUCH_CS   33
  #define TOUCH_IRQ  36
  #define TOUCH_SCLK 25
  #define TOUCH_MOSI 32
  #define TOUCH_MISO 39
  #define TFT_BACKLIGHT 21
  #define BOARD_NAME "ESP32-2432S028R (Resistive)"

  // Touch calibration values (adjust for your specific board)
  #define TOUCH_MIN_X 300
  #define TOUCH_MAX_X 3900
  #define TOUCH_MIN_Y 300
  #define TOUCH_MAX_Y 3900

  // Create second SPI bus for touch
  SPIClass touchSPI(HSPI);
  XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);

#elif defined(BOARD_CYD_CAPACITIVE)
  // JC2432W328C (Guition) with CST816S Capacitive Touch (I2C)
  // Official pin config from: https://github.com/rzeldent/platformio-espressif32-sunton
  #define TOUCH_SDA 33
  #define TOUCH_SCL 32
  #define TOUCH_INT 21
  #define TOUCH_RST 25
  #define TFT_BACKLIGHT 27
  #define CST816S_ADDR 0x15
  #define BOARD_NAME "JC2432W328C (Capacitive)"

#else
  #error "No board defined! Use -DBOARD_CYD_RESISTIVE or -DBOARD_CYD_CAPACITIVE"
#endif

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
  MODE_DIGITS,      // Display digits 0-9 with animations
  MODE_IDENTIFY,    // Identify all pixels
  MODE_MANUAL       // Manual control (future)
};

ControlMode currentMode = MODE_MENU;

// Timing
unsigned long lastCommandTime = 0;
unsigned long lastPingTime = 0;
unsigned long modeStartTime = 0;
const unsigned long SIMULATION_INTERVAL = 5000;  // 5 seconds between random patterns
const unsigned long IDENTIFY_DURATION = 5000;    // Identify phase duration
const unsigned long PING_INTERVAL = 5000;        // 5 seconds between pings in manual mode

// Manual mode state
struct ManualState {
  uint8_t selectedPixel = 0;     // Currently selected pixel (0-23)
  float angles[3] = {0, 0, 0};   // Angles for the 3 hands
  RotationDirection directions[3] = {DIR_CW, DIR_CW, DIR_CW};  // Direction for each hand
  uint8_t colorIndex = 0;        // Color palette index
  uint8_t opacity = 255;         // Opacity (0-255)
  TransitionType transition = TRANSITION_LINEAR;
  float duration = 2.0;          // Duration in seconds
} manualState;

// Touch state
uint16_t touchX = 0, touchY = 0;
bool touched = false;
unsigned long lastTouchTime = 0;
const unsigned long TOUCH_DEBOUNCE = 200;  // 200ms debounce

// ===== DIGIT DEFINITIONS =====

// Digit mapping for 6 pixels arranged as:
// Pixel IDs: 0, 1 (top row)
//            8, 9 (middle row) 
//           16,17 (bottom row)
// Each digit has angles for each pixel's 3 hands
// Angles: 0=up, 90=right, 180=down, 225=empty, 270=left
// Style: normal opacity=255, blank pixels opacity=50 with 225° angles

struct DigitPattern {
  float angles[6][3];  // 6 pixels, 3 hands each
  uint8_t opacity[6];  // Opacity for each pixel (255=normal, 50=blank)
};

DigitPattern digitPatterns[12] = {
  // Digit '0'
  {{{90, 180, 180}, {180, 270, 270}, {0, 180, 180}, {0, 180, 180}, {0, 90, 90}, {0, 270, 270}},
   {255, 255, 255, 255, 255, 255}},
  
  // Digit '1' 
  {{{180, 180, 180}, {225, 225, 225}, {0, 180, 180}, {225, 225, 225}, {0, 0, 0}, {225, 225, 225}},
   {255, 50, 255, 50, 255, 50}},
  
  // Digit '2'
  {{{90, 90, 90}, {270, 180, 270}, {90, 180, 180}, {0, 270, 0}, {0, 90, 90}, {270, 270, 270}},
   {255, 255, 255, 255, 255, 255}},
  
  // Digit '3'
  {{{90, 90, 90}, {270, 180, 270}, {90, 90, 90}, {0, 180, 270}, {90, 90, 90}, {0, 270, 0}},
   {255, 255, 255, 255, 255, 255}},
  
  // Digit '4'
  {{{180, 180, 180}, {180, 180, 180}, {0, 90, 90}, {0, 180, 270}, {225, 225, 225}, {0, 0, 0}},
   {255, 255, 255, 255, 50, 255}},
  
  // Digit '5'
  {{{90, 180, 180}, {270, 270, 270}, {0, 90, 90}, {270, 180, 270}, {0, 90, 90}, {0, 270, 0}},
   {255, 255, 255, 255, 255, 255}},
  
  // Digit '6'
  {{{180, 90, 90}, {270, 180, 270}, {0, 90, 180}, {270, 180, 270}, {0, 90, 90}, {0, 270, 0}},
   {255, 255, 255, 255, 255, 255}},
  
  // Digit '7'
  {{{90, 180, 180}, {180, 270, 270}, {225, 225, 225}, {0, 180, 180}, {225, 225, 225}, {0, 0, 0}},
   {255, 255, 50, 255, 50, 255}},
  
  // Digit '8'
  {{{90, 180, 180}, {180, 270, 270}, {0, 90, 180}, {0, 180, 270}, {0, 90, 90}, {0, 270, 0}},
   {255, 255, 255, 255, 255, 255}},
  
  // Digit '9'
  {{{90, 180, 180}, {180, 270, 270}, {0, 90, 90}, {0, 180, 270}, {0, 90, 90}, {0, 270, 0}},
   {255, 255, 255, 255, 255, 255}},
  
  // ':' (colon)
  {{{180, 180, 180}, {225, 225, 225}, {225, 225, 225}, {225, 225, 225}, {0, 0, 0}, {225, 225, 225}},
   {255, 50, 50, 50, 255, 50}},
  
  // ' ' (space)
  {{{225, 225, 225}, {225, 225, 225}, {225, 225, 225}, {225, 225, 225}, {225, 225, 225}, {225, 225, 225}},
   {50, 50, 50, 50, 50, 50}}
};

// Pixel ID mapping for the 6-pixel digit display
// Physical wiring: IDs 0,1 (top), 8,9 (middle), 16,17 (bottom)
const uint8_t digitPixelIds[6] = {0, 1, 8, 9, 16, 17};

// ===== FUNCTION DECLARATIONS =====
void drawManualScreen();
void handleManualTouch(uint16_t x, uint16_t y);
void sendManualCommand(bool allPixels);
void sendPing();
void drawDigitsScreen();
void handleDigitsTouch(uint16_t x, uint16_t y);
void sendDigitPattern(uint8_t digit);

// ===== FUNCTIONS =====

// Read touch input (supports both resistive and capacitive touch)
bool readTouch(uint16_t &x, uint16_t &y) {
  // Debounce
  unsigned long now = millis();
  if (now - lastTouchTime < TOUCH_DEBOUNCE) {
    return false;
  }

#if defined(BOARD_CYD_RESISTIVE)
  // XPT2046 Resistive Touch
  if (ts.touched()) {
    TS_Point p = ts.getPoint();

    // Map raw values to screen coordinates
    x = map(p.x, TOUCH_MIN_X, TOUCH_MAX_X, 0, 320);
    y = map(p.y, TOUCH_MIN_Y, TOUCH_MAX_Y, 0, 240);

    // Clamp to screen bounds
    x = constrain(x, 0, 319);
    y = constrain(y, 0, 239);

    lastTouchTime = now;
    return true;
  }
  return false;

#elif defined(BOARD_CYD_CAPACITIVE)
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
#endif
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

  // Button 2: Digits (top right)
  tft.fillRoundRect(170, 90, 140, 60, 8, TFT_DARKBLUE);
  tft.setTextColor(TFT_WHITE, TFT_DARKBLUE);
  tft.setTextSize(2);
  tft.setCursor(195, 105);
  tft.println("Digits");
  tft.setTextSize(1);
  tft.setCursor(180, 125);
  tft.println("Display 0-9");

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
  // Button 2: Digits (170, 90, 140, 60)
  if (x >= 170 && x <= 310 && y >= 90 && y <= 150) {
    return MODE_DIGITS;
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

// ===== DIGITS MODE FUNCTIONS =====

void drawDigitsScreen() {
  tft.fillScreen(COLOR_BG);

  // Title
  tft.setTextColor(COLOR_ACCENT, COLOR_BG);
  tft.setTextSize(2);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("Digits Mode", 160, 5);
  tft.setTextDatum(TL_DATUM);

  tft.setTextSize(1);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setCursor(70, 25);
  tft.println("Touch a digit to display:");

  // Draw number buttons in a 2x5 grid
  // Top row: 0-4
  for (int i = 0; i <= 4; i++) {
    int x = 10 + i * 60;
    int y = 45;
    
    tft.fillRoundRect(x, y, 50, 40, 4, TFT_DARKGREEN);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREEN);
    tft.setTextSize(3);
    tft.setCursor(x + 20, y + 10);
    tft.print(i);
  }

  // Bottom row: 5-9
  for (int i = 5; i <= 9; i++) {
    int x = 10 + (i - 5) * 60;
    int y = 95;
    
    tft.fillRoundRect(x, y, 50, 40, 4, TFT_DARKGREEN);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREEN);
    tft.setTextSize(3);
    tft.setCursor(x + 20, y + 10);
    tft.print(i);
  }

  // Special characters row
  // Colon button
  tft.fillRoundRect(10, 145, 50, 40, 4, TFT_PURPLE);
  tft.setTextColor(TFT_WHITE, TFT_PURPLE);
  tft.setTextSize(3);
  tft.setCursor(25, 155);
  tft.print(":");

  // Space button
  tft.fillRoundRect(70, 145, 50, 40, 4, TFT_PURPLE);
  tft.setTextColor(TFT_WHITE, TFT_PURPLE);
  tft.setTextSize(2);
  tft.setCursor(80, 155);
  tft.print("SPC");

  // Back button
  tft.fillRoundRect(200, 145, 100, 40, 4, TFT_RED);
  tft.setTextColor(TFT_WHITE, TFT_RED);
  tft.setTextSize(2);
  tft.setCursor(225, 155);
  tft.print("Back");
}

void handleDigitsTouch(uint16_t x, uint16_t y) {
  // Check digit buttons (0-4, top row)
  if (y >= 45 && y <= 85) {
    for (int i = 0; i <= 4; i++) {
      int buttonX = 10 + i * 60;
      if (x >= buttonX && x <= buttonX + 50) {
        sendDigitPattern(i);
        return;
      }
    }
  }
  
  // Check digit buttons (5-9, bottom row)
  if (y >= 95 && y <= 135) {
    for (int i = 5; i <= 9; i++) {
      int buttonX = 10 + (i - 5) * 60;
      if (x >= buttonX && x <= buttonX + 50) {
        sendDigitPattern(i);
        return;
      }
    }
  }
  
  // Check special character buttons
  if (y >= 145 && y <= 185) {
    // Colon button
    if (x >= 10 && x <= 60) {
      sendDigitPattern(10); // ':' is index 10
      return;
    }
    // Space button
    if (x >= 70 && x <= 120) {
      sendDigitPattern(11); // ' ' is index 11
      return;
    }
    // Back button
    if (x >= 200 && x <= 300) {
      currentMode = MODE_MENU;
      drawMenu();
      return;
    }
  }
}

void sendDigitPattern(uint8_t digit) {
  if (digit > 11) return; // Invalid digit
  
  ESPNowPacket packet;
  packet.angleCmd.command = CMD_SET_ANGLES;
  
  // Use random transition and duration for animation
  packet.angleCmd.transition = getRandomTransition();
  packet.angleCmd.duration = floatToDuration(getRandomDuration());
  
  // Random color for the digit
  uint8_t colorIndex = getRandomColorIndex();
  
  // Clear all pixels first (set to 225° with low opacity)
  for (int i = 0; i < MAX_PIXELS; i++) {
    packet.angleCmd.setPixelAngles(i, 225, 225, 225, DIR_SHORTEST, DIR_SHORTEST, DIR_SHORTEST);
    packet.angleCmd.setPixelStyle(i, colorIndex, 50);
  }
  
  // Set the 6 pixels that make up the digit
  DigitPattern& pattern = digitPatterns[digit];
  for (int i = 0; i < 6; i++) {
    uint8_t pixelId = digitPixelIds[i];
    packet.angleCmd.setPixelAngles(pixelId,
      pattern.angles[i][0],
      pattern.angles[i][1],  
      pattern.angles[i][2],
      DIR_SHORTEST,
      DIR_SHORTEST,
      DIR_SHORTEST
    );
    packet.angleCmd.setPixelStyle(pixelId, colorIndex, pattern.opacity[i]);
  }
  
  // Send the packet
  if (ESPNowComm::sendPacket(&packet, sizeof(AngleCommandPacket))) {
    const char* digitNames[] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", ":", " "};
    Serial.print("Sent digit: ");
    Serial.print(digitNames[digit]);
    Serial.print(" with transition: ");
    Serial.print(getTransitionName(packet.angleCmd.transition));
    Serial.print(", duration: ");
    Serial.print(durationToFloat(packet.angleCmd.duration), 1);
    Serial.println("s");
    
    // Update display to show what was sent
    tft.fillRect(130, 145, 60, 40, COLOR_BG);
    tft.setTextColor(COLOR_ACCENT, COLOR_BG);
    tft.setTextSize(3);
    tft.setCursor(145, 155);
    tft.print(digitNames[digit]);
    
  } else {
    Serial.println("Failed to send digit packet!");
    
    tft.fillRect(130, 145, 60, 40, TFT_RED);
    tft.setTextColor(TFT_WHITE, TFT_RED);
    tft.setTextSize(1);
    tft.setCursor(140, 160);
    tft.print("ERROR");
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

    // -/+ buttons for angle
    tft.fillRoundRect(100, y - 2, 25, 16, 4, TFT_DARKGREY);
    tft.fillRoundRect(130, y - 2, 25, 16, 4, TFT_DARKGREY);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
    tft.setCursor(108, y);
    tft.print("-");
    tft.setCursor(138, y);
    tft.print("+");

    // Direction toggle button (CW/CCW)
    uint16_t dirColor = (manualState.directions[i] == DIR_CW) ? TFT_ORANGE : TFT_PURPLE;
    tft.fillRoundRect(5, y + 15, 150, 12, 3, dirColor);
    tft.setTextColor(TFT_WHITE, dirColor);
    tft.setTextSize(1);
    tft.setCursor(10, y + 16);
    tft.print(manualState.directions[i] == DIR_CW ? "Clockwise (CW)" : "Counter-CW (CCW)");
  }

  // Color selector
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setCursor(5, 165);
  tft.print("Color:");
  tft.fillRoundRect(50, 163, 30, 16, 4, TFT_DARKGREY);
  tft.fillRoundRect(85, 163, 30, 16, 4, TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.setCursor(58, 165);
  tft.print("<");
  tft.setCursor(93, 165);
  tft.print(">");
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setCursor(120, 165);
  tft.print(manualState.colorIndex);

  // Opacity selector
  tft.setCursor(5, 185);
  tft.print("Opacity:");
  tft.fillRoundRect(60, 183, 30, 16, 4, TFT_DARKGREY);
  tft.fillRoundRect(95, 183, 30, 16, 4, TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.setCursor(68, 185);
  tft.print("-");
  tft.setCursor(103, 185);
  tft.print("+");
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setCursor(130, 185);
  tft.print(manualState.opacity);

  // Duration selector
  tft.setCursor(5, 205);
  tft.print("Duration:");
  tft.fillRoundRect(70, 203, 30, 16, 4, TFT_DARKGREY);
  tft.fillRoundRect(105, 203, 30, 16, 4, TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.setCursor(78, 205);
  tft.print("-");
  tft.setCursor(113, 205);
  tft.print("+");
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setCursor(140, 205);
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
    // Angle +/- buttons
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
    // Direction toggle button (5, y+15, 150, 12)
    if (y >= y_base + 15 && y <= y_base + 27) {
      if (x >= 5 && x <= 155) {
        manualState.directions[i] = (manualState.directions[i] == DIR_CW) ? DIR_CCW : DIR_CW;
        needsRedraw = true;
      }
    }
  }

  // Color prev/next (50, 163, 30, 16) and (85, 163, 30, 16)
  if (y >= 163 && y <= 179) {
    if (x >= 50 && x <= 80) {
      manualState.colorIndex = (manualState.colorIndex == 0) ? (COLOR_PALETTE_SIZE - 1) : manualState.colorIndex - 1;
      needsRedraw = true;
    } else if (x >= 85 && x <= 115) {
      manualState.colorIndex = (manualState.colorIndex + 1) % COLOR_PALETTE_SIZE;
      needsRedraw = true;
    }
  }

  // Opacity -/+ (60, 183, 30, 16) and (95, 183, 30, 16)
  if (y >= 183 && y <= 199) {
    if (x >= 60 && x <= 90) {
      if (manualState.opacity >= 25) manualState.opacity -= 25;
      needsRedraw = true;
    } else if (x >= 95 && x <= 125) {
      if (manualState.opacity <= 230) manualState.opacity += 25;
      needsRedraw = true;
    }
  }

  // Duration -/+ (70, 203, 30, 16) and (105, 203, 30, 16)
  if (y >= 203 && y <= 219) {
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
    // Send same angles and directions to all pixels (synchronized movement)
    for (int i = 0; i < MAX_PIXELS; i++) {
      packet.angleCmd.setPixelAngles(i,
        manualState.angles[0],
        manualState.angles[1],
        manualState.angles[2],
        manualState.directions[0],
        manualState.directions[1],
        manualState.directions[2]);
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
          manualState.directions[0],
          manualState.directions[1],
          manualState.directions[2]);
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

void sendPing() {
  ESPNowPacket packet;
  packet.ping.command = CMD_PING;
  packet.ping.timestamp = millis();

  if (ESPNowComm::sendPacket(&packet, sizeof(PingPacket))) {
    Serial.println("Ping sent to keep pixels alive");
  } else {
    Serial.println("Failed to send ping");
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n========== MASTER CONTROLLER ==========");
  Serial.println("Twenty-Four Times - ESP-NOW Master");
  Serial.println(BOARD_NAME);
  Serial.println("=======================================\n");

  // Initialize backlight
  pinMode(TFT_BACKLIGHT, OUTPUT);
  digitalWrite(TFT_BACKLIGHT, HIGH);
  Serial.println("Backlight ON");

  // ===== TOUCH CONTROLLER INITIALIZATION =====
#if defined(BOARD_CYD_RESISTIVE)
  // XPT2046 Resistive Touch - uses SEPARATE SPI bus from display
  Serial.printf("Touch pins: CS=%d, IRQ=%d, SCLK=%d, MOSI=%d, MISO=%d\n",
                TOUCH_CS, TOUCH_IRQ, TOUCH_SCLK, TOUCH_MOSI, TOUCH_MISO);

  // Initialize the separate SPI bus for touch (HSPI)
  touchSPI.begin(TOUCH_SCLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  ts.begin(touchSPI);
  ts.setRotation(1);  // Match display rotation
  Serial.println("XPT2046 touch controller initialized on HSPI");

#elif defined(BOARD_CYD_CAPACITIVE)
  // CST816S Capacitive Touch (I2C)
  Serial.printf("Touch pins: SDA=%d, SCL=%d, RST=%d, INT=%d\n",
                TOUCH_SDA, TOUCH_SCL, TOUCH_RST, TOUCH_INT);

  // Configure RST pin and perform reset
  pinMode(TOUCH_RST, OUTPUT);
  digitalWrite(TOUCH_RST, LOW);
  delay(20);
  digitalWrite(TOUCH_RST, HIGH);
  delay(100);  // Wait for CST816S to boot
  Serial.println("Touch controller reset complete");

  // Configure INT pin
  pinMode(TOUCH_INT, INPUT);

  // Initialize I2C for touch on correct pins (SDA=33, SCL=32)
  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  delay(50);

  // Verify touch controller is present and read info
  Wire.beginTransmission(CST816S_ADDR);
  if (Wire.endTransmission() == 0) {
    Serial.println("CST816S found at 0x15");

    // Read chip info
    Wire.beginTransmission(CST816S_ADDR);
    Wire.write(0xA7);  // Chip ID register
    Wire.endTransmission(false);
    Wire.requestFrom(CST816S_ADDR, 3);
    if (Wire.available() >= 3) {
      byte chipId = Wire.read();
      byte projId = Wire.read();
      byte fwVer = Wire.read();
      Serial.printf("  Chip ID: 0x%02X, Project: %d, FW: %d\n", chipId, projId, fwVer);
    }
  } else {
    Serial.println("WARNING: CST816S not found!");
  }
#endif

  Serial.println("Touch controller ready");

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

        Serial.print("Mode changed to: ");
        Serial.println(newMode);

        // Initialize the new mode
        switch (currentMode) {
          case MODE_SIMULATION:
            sendRandomPattern();
            break;
          case MODE_DIGITS:
            drawDigitsScreen();
            break;
          case MODE_IDENTIFY:
            sendIdentifyCommand(255);
            break;
          case MODE_MANUAL:
            drawManualScreen();
            lastPingTime = currentTime;  // Initialize ping timer
            break;
          default:
            break;
        }
      }
    } else if (currentMode == MODE_MANUAL) {
      // Manual mode has its own touch handler
      handleManualTouch(tx, ty);
    } else if (currentMode == MODE_DIGITS) {
      // Digits mode has its own touch handler
      handleDigitsTouch(tx, ty);
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

    case MODE_DIGITS: {
      // Digits mode waits for touch input - no automatic behavior
      break;
    }

    case MODE_IDENTIFY: {
      // Identify mode runs once, then waits for touch to return to menu
      // Display stays on identify screen until touched
      break;
    }

    case MODE_MANUAL: {
      // Send periodic pings to keep pixels alive
      if (currentTime - lastPingTime >= PING_INTERVAL) {
        sendPing();
        lastPingTime = currentTime;
      }
      break;
    }
  }

  // Small delay to avoid busy-waiting
  delay(10);
}

