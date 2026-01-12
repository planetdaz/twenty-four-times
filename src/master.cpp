#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <TFT_eSPI.h>
#include <ESPNowComm.h>
#include <WebServer.h>
#include <LittleFS.h>

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
  MODE_PROVISION,   // Discovery and provisioning of pixels
  MODE_MANUAL,      // Manual control (future)
  MODE_OTA          // OTA firmware update for pixels
};

ControlMode currentMode = MODE_MENU;

// ===== PROVISIONING STATE =====
enum ProvisionPhase {
  PHASE_IDLE,        // Initial state - show start button
  PHASE_DISCOVERING, // Broadcasting discovery, collecting MACs
  PHASE_ASSIGNING    // Cycling through MACs, assigning IDs
};

ProvisionPhase provisionPhase = PHASE_IDLE;
uint8_t discoveredMacs[MAX_PIXELS][6];  // Collected MAC addresses
uint8_t discoveredIds[MAX_PIXELS];      // Current IDs of discovered pixels
uint8_t discoveredCount = 0;            // Number of MACs discovered
uint8_t selectedMacIndex = 0;           // Currently selected MAC for assignment
uint8_t nextIdToAssign = 0;             // Next ID to assign
unsigned long lastDiscoveryTime = 0;    // For discovery timing
const unsigned long DISCOVERY_INTERVAL = 3000;  // Send discovery every 3 seconds
const unsigned long DISCOVERY_WINDOW = 5000;    // Wait 5 seconds for responses

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

// ===== OTA UPDATE STATE =====
// WiFi AP configuration for OTA
const char* OTA_AP_SSID = "TwentyFourTimes";
const char* OTA_AP_PASSWORD = "clockupdate";  // Min 8 characters
const char* OTA_FIRMWARE_PATH = "/firmware.bin";

// HTTP server for serving firmware
WebServer otaServer(80);
bool otaServerRunning = false;

// OTA state tracking
enum OTAPhase {
  OTA_IDLE,           // Waiting for user to start
  OTA_READY,          // Server running, ready to send notify
  OTA_IN_PROGRESS,    // Update sent, waiting for pixels
  OTA_COMPLETE        // All done
};

OTAPhase otaPhase = OTA_IDLE;
uint32_t firmwareSize = 0;
uint8_t otaPixelStatus[MAX_PIXELS];  // Status of each pixel (OTAStatus enum)
uint8_t otaPixelProgress[MAX_PIXELS]; // Progress of each pixel (0-100)

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

// Pixel ID mappings for two-digit display (12 pixels in 3 rows × 4 columns)
// Layout:
//   Row 0: [0]  [1]  | [2]  [3]   <- Left digit top, Right digit top
//   Row 1: [8]  [9]  | [10] [11]  <- Left digit mid, Right digit mid
//   Row 2: [16] [17] | [18] [19]  <- Left digit bot, Right digit bot
const uint8_t digit1PixelIds[6] = {0, 1, 8, 9, 16, 17};    // Left digit
const uint8_t digit2PixelIds[6] = {2, 3, 10, 11, 18, 19};  // Right digit

// Current color for digits mode
uint8_t currentDigitColor = 0;

// Current speed for digits mode (duration in seconds)
float currentDigitSpeed = 2.0;

// Auto-cycle mode variables (cycles 00-99 for two-digit display)
bool autoCycleEnabled = false;
uint8_t autoCycleNumber = 0;        // Current number (0-99)
bool autoCycleDirection = true;     // true = 0->99, false = 99->0
unsigned long lastAutoCycleTime = 0;

// ===== FUNCTION DECLARATIONS =====
void drawManualScreen();
void handleManualTouch(uint16_t x, uint16_t y);
void sendManualCommand(bool allPixels);
void sendPing();
void drawDigitsScreen();
void handleDigitsTouch(uint16_t x, uint16_t y);
void sendTwoDigitPattern(uint8_t leftDigit, uint8_t rightDigit);
// Provisioning functions
void onMasterPacketReceived(const ESPNowPacket* packet, size_t len);
void drawProvisionScreen();
void handleProvisionTouch(uint16_t x, uint16_t y);
void sendDiscoveryCommand();
void sendHighlightCommand(uint8_t* targetMac, HighlightState state);
void sendAssignIdCommand(uint8_t* targetMac, uint8_t newId);
// OTA functions
void initOTAServer();
void stopOTAServer();
void drawOTAScreen();
void handleOTATouch(uint16_t x, uint16_t y);
void sendOTANotifyCommand();
void handleOTAAck(const OTAAckPacket& ack);

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

  // Button 3: Provision (bottom left)
  tft.fillRoundRect(10, 160, 150, 60, 8, TFT_PURPLE);
  tft.setTextColor(TFT_WHITE, TFT_PURPLE);
  tft.setTextSize(2);
  tft.setCursor(30, 175);
  tft.println("Provision");
  tft.setTextSize(1);
  tft.setCursor(25, 195);
  tft.println("Discover & assign");

  // Button 4: Manual (bottom right - make room for OTA)
  tft.fillRoundRect(170, 160, 65, 60, 8, TFT_ORANGE);
  tft.setTextColor(TFT_WHITE, TFT_ORANGE);
  tft.setTextSize(1);
  tft.setCursor(178, 180);
  tft.println("Manual");

  // Button 5: OTA Update (far bottom right)
  tft.fillRoundRect(245, 160, 65, 60, 8, TFT_CYAN);
  tft.setTextColor(TFT_BLACK, TFT_CYAN);
  tft.setTextSize(2);
  tft.setCursor(262, 175);
  tft.println("OTA");
  tft.setTextSize(1);
  tft.setCursor(252, 200);
  tft.println("Update");
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
  // Button 3: Provision (10, 160, 150, 60)
  if (x >= 10 && x <= 160 && y >= 160 && y <= 220) {
    return MODE_PROVISION;
  }
  // Button 4: Manual (170, 160, 65, 60)
  if (x >= 170 && x <= 235 && y >= 160 && y <= 220) {
    return MODE_MANUAL;
  }
  // Button 5: OTA (245, 160, 65, 60)
  if (x >= 245 && x <= 310 && y >= 160 && y <= 220) {
    return MODE_OTA;
  }

  return MODE_MENU;  // No button pressed
}

// Send a random pattern (simulation mode)
void sendRandomPattern() {
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

// ===== PROVISIONING FUNCTIONS =====

// Receive callback for discovery responses and OTA acks from pixels
void onMasterPacketReceived(const ESPNowPacket* packet, size_t len) {
  if (packet->command == CMD_DISCOVERY && provisionPhase == PHASE_DISCOVERING) {
    const DiscoveryResponsePacket& resp = packet->discoveryResponse;

    // Check if this MAC is already in our list
    bool duplicate = false;
    for (uint8_t i = 0; i < discoveredCount; i++) {
      if (memcmp(discoveredMacs[i], resp.mac, 6) == 0) {
        duplicate = true;
        break;
      }
    }

    if (!duplicate && discoveredCount < MAX_PIXELS) {
      // Add to discovered list
      memcpy(discoveredMacs[discoveredCount], resp.mac, 6);
      discoveredIds[discoveredCount] = resp.currentId;
      discoveredCount++;

      Serial.print("Discovered pixel: ");
      for (int i = 0; i < 6; i++) {
        if (i > 0) Serial.print(":");
        Serial.printf("%02X", resp.mac[i]);
      }
      Serial.print(" (ID: ");
      if (resp.currentId == PIXEL_ID_UNPROVISIONED) {
        Serial.print("unprovisioned");
      } else {
        Serial.print(resp.currentId);
      }
      Serial.println(")");
    }
  } else if (packet->command == CMD_OTA_ACK) {
    handleOTAAck(packet->otaAck);
  }
}

// Format MAC address for display
void formatMac(uint8_t* mac, char* buffer) {
  sprintf(buffer, "%02X:%02X:%02X:%02X:%02X:%02X",
          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// Send discovery command
void sendDiscoveryCommand() {
  ESPNowPacket packet;
  packet.discovery.command = CMD_DISCOVERY;
  packet.discovery.excludeCount = min(discoveredCount, (uint8_t)20);

  // Copy already discovered MACs to exclude list
  for (uint8_t i = 0; i < packet.discovery.excludeCount; i++) {
    memcpy(packet.discovery.excludeMacs[i], discoveredMacs[i], 6);
  }

  if (ESPNowComm::sendPacket(&packet, sizeof(DiscoveryCommandPacket))) {
    Serial.print("Sent DISCOVERY command (excluding ");
    Serial.print(packet.discovery.excludeCount);
    Serial.println(" MACs)");
  }
}

// Send highlight command to a specific pixel
void sendHighlightCommand(uint8_t* targetMac, HighlightState state) {
  ESPNowPacket packet;
  packet.highlight.command = CMD_HIGHLIGHT;
  memcpy(packet.highlight.targetMac, targetMac, 6);
  packet.highlight.state = state;

  ESPNowComm::sendPacket(&packet, sizeof(HighlightPacket));
}

// Send pixel ID assignment command
void sendAssignIdCommand(uint8_t* targetMac, uint8_t newId) {
  ESPNowPacket packet;
  packet.setPixelId.command = CMD_SET_PIXEL_ID;
  memcpy(packet.setPixelId.targetMac, targetMac, 6);
  packet.setPixelId.pixelId = newId;

  if (ESPNowComm::sendPacket(&packet, sizeof(SetPixelIdPacket))) {
    Serial.print("Assigned ID ");
    Serial.print(newId);
    Serial.println(" to pixel");
  }
}

// Draw the provisioning screen based on current phase
void drawProvisionScreen() {
  tft.fillScreen(COLOR_BG);

  // Title
  tft.setTextColor(COLOR_ACCENT, COLOR_BG);
  tft.setTextSize(2);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("Provision Pixels", 160, 5);
  tft.setTextDatum(TL_DATUM);

  if (provisionPhase == PHASE_IDLE) {
    // Show start button and info
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.setTextSize(1);
    tft.setCursor(10, 40);
    tft.println("Discover and assign IDs to pixels.");
    tft.setCursor(10, 55);
    tft.println("Pixels will flash during discovery.");

    // Start Discovery button
    tft.fillRoundRect(60, 90, 200, 50, 8, TFT_DARKGREEN);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREEN);
    tft.setTextSize(2);
    tft.setCursor(75, 105);
    tft.println("Start Discovery");

    // Back button
    tft.fillRoundRect(110, 170, 100, 40, 8, TFT_DARKGREY);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
    tft.setTextSize(2);
    tft.setCursor(135, 180);
    tft.println("Back");

  } else if (provisionPhase == PHASE_DISCOVERING) {
    // Show discovery progress
    tft.setTextColor(TFT_YELLOW, COLOR_BG);
    tft.setTextSize(2);
    tft.setCursor(60, 50);
    tft.println("Discovering...");

    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.setTextSize(3);
    tft.setCursor(100, 90);
    tft.print("Found: ");
    tft.print(discoveredCount);
    tft.println("/24");

    // Stop button
    tft.fillRoundRect(20, 160, 130, 50, 8, TFT_RED);
    tft.setTextColor(TFT_WHITE, TFT_RED);
    tft.setTextSize(2);
    tft.setCursor(55, 175);
    tft.println("Stop");

    // Begin Assignment button (only if we have discovered some)
    if (discoveredCount > 0) {
      tft.fillRoundRect(170, 160, 130, 50, 8, TFT_DARKGREEN);
      tft.setTextColor(TFT_WHITE, TFT_DARKGREEN);
      tft.setTextSize(2);
      tft.setCursor(190, 175);
      tft.println("Assign");
    }

  } else if (provisionPhase == PHASE_ASSIGNING) {
    // Show assignment UI
    char macStr[18];
    formatMac(discoveredMacs[selectedMacIndex], macStr);

    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.setTextSize(1);
    tft.setCursor(10, 35);
    tft.print("Pixel ");
    tft.print(selectedMacIndex + 1);
    tft.print(" of ");
    tft.println(discoveredCount);

    // Show MAC address
    tft.setTextSize(1);
    tft.setCursor(10, 55);
    tft.print("MAC: ");
    tft.println(macStr);

    // Show current ID
    tft.setCursor(10, 70);
    tft.print("Current ID: ");
    if (discoveredIds[selectedMacIndex] == PIXEL_ID_UNPROVISIONED) {
      tft.setTextColor(TFT_YELLOW, COLOR_BG);
      tft.println("None");
    } else {
      tft.println(discoveredIds[selectedMacIndex]);
    }

    // Show next ID to assign
    tft.setTextColor(TFT_CYAN, COLOR_BG);
    tft.setTextSize(2);
    tft.setCursor(10, 95);
    tft.print("Assign ID: ");
    tft.setTextSize(3);
    tft.println(nextIdToAssign);

    // Prev/Next buttons
    tft.fillRoundRect(10, 140, 60, 35, 4, TFT_DARKBLUE);
    tft.setTextColor(TFT_WHITE, TFT_DARKBLUE);
    tft.setTextSize(2);
    tft.setCursor(20, 148);
    tft.println("Prev");

    tft.fillRoundRect(80, 140, 60, 35, 4, TFT_DARKBLUE);
    tft.setCursor(90, 148);
    tft.println("Next");

    // Assign button
    tft.fillRoundRect(160, 140, 70, 35, 4, TFT_DARKGREEN);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREEN);
    tft.setCursor(165, 148);
    tft.println("Assign");

    // Skip button
    tft.fillRoundRect(240, 140, 70, 35, 4, TFT_ORANGE);
    tft.setTextColor(TFT_WHITE, TFT_ORANGE);
    tft.setCursor(255, 148);
    tft.println("Skip");

    // Back button
    tft.fillRoundRect(10, 190, 80, 35, 4, TFT_DARKGREY);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
    tft.setCursor(25, 198);
    tft.println("Back");

    // Done button
    tft.fillRoundRect(230, 190, 80, 35, 4, TFT_PURPLE);
    tft.setTextColor(TFT_WHITE, TFT_PURPLE);
    tft.setCursor(245, 198);
    tft.println("Done");
  }
}

// Handle touch in provisioning mode
void handleProvisionTouch(uint16_t x, uint16_t y) {
  if (provisionPhase == PHASE_IDLE) {
    // Start Discovery button (60, 90, 200, 50)
    if (x >= 60 && x <= 260 && y >= 90 && y <= 140) {
      discoveredCount = 0;
      selectedMacIndex = 0;
      provisionPhase = PHASE_DISCOVERING;
      lastDiscoveryTime = millis();
      sendDiscoveryCommand();
      drawProvisionScreen();
      return;
    }

    // Back button (110, 170, 100, 40)
    if (x >= 110 && x <= 210 && y >= 170 && y <= 210) {
      currentMode = MODE_MENU;
      drawMenu();
      return;
    }

  } else if (provisionPhase == PHASE_DISCOVERING) {
    // Stop button (20, 160, 130, 50)
    if (x >= 20 && x <= 150 && y >= 160 && y <= 210) {
      provisionPhase = PHASE_IDLE;
      drawProvisionScreen();
      return;
    }

    // Assign button (170, 160, 130, 50)
    if (x >= 170 && x <= 300 && y >= 160 && y <= 210 && discoveredCount > 0) {
      provisionPhase = PHASE_ASSIGNING;
      selectedMacIndex = 0;
      nextIdToAssign = 0;
      // Highlight the first pixel
      sendHighlightCommand(discoveredMacs[selectedMacIndex], HIGHLIGHT_SELECTED);
      drawProvisionScreen();
      return;
    }

  } else if (provisionPhase == PHASE_ASSIGNING) {
    // Prev button (10, 140, 60, 35)
    if (x >= 10 && x <= 70 && y >= 140 && y <= 175) {
      // Un-highlight current
      sendHighlightCommand(discoveredMacs[selectedMacIndex], HIGHLIGHT_IDLE);
      // Move to previous
      if (selectedMacIndex > 0) {
        selectedMacIndex--;
      } else {
        selectedMacIndex = discoveredCount - 1;
      }
      // Highlight new selection
      sendHighlightCommand(discoveredMacs[selectedMacIndex], HIGHLIGHT_SELECTED);
      drawProvisionScreen();
      return;
    }

    // Next button (80, 140, 60, 35)
    if (x >= 80 && x <= 140 && y >= 140 && y <= 175) {
      // Un-highlight current
      sendHighlightCommand(discoveredMacs[selectedMacIndex], HIGHLIGHT_IDLE);
      // Move to next
      selectedMacIndex = (selectedMacIndex + 1) % discoveredCount;
      // Highlight new selection
      sendHighlightCommand(discoveredMacs[selectedMacIndex], HIGHLIGHT_SELECTED);
      drawProvisionScreen();
      return;
    }

    // Assign button (160, 140, 70, 35)
    if (x >= 160 && x <= 230 && y >= 140 && y <= 175) {
      // Assign the ID
      sendAssignIdCommand(discoveredMacs[selectedMacIndex], nextIdToAssign);
      // Update local state
      discoveredIds[selectedMacIndex] = nextIdToAssign;
      // Show assigned state
      sendHighlightCommand(discoveredMacs[selectedMacIndex], HIGHLIGHT_ASSIGNED);
      delay(500);  // Brief pause to see confirmation
      // Move to next and increment ID
      nextIdToAssign++;
      if (selectedMacIndex < discoveredCount - 1) {
        selectedMacIndex++;
        sendHighlightCommand(discoveredMacs[selectedMacIndex], HIGHLIGHT_SELECTED);
      }
      drawProvisionScreen();
      return;
    }

    // Skip button (240, 140, 70, 35)
    if (x >= 240 && x <= 310 && y >= 140 && y <= 175) {
      // Un-highlight current
      sendHighlightCommand(discoveredMacs[selectedMacIndex], HIGHLIGHT_IDLE);
      // Move to next without assigning
      selectedMacIndex = (selectedMacIndex + 1) % discoveredCount;
      // Highlight new selection
      sendHighlightCommand(discoveredMacs[selectedMacIndex], HIGHLIGHT_SELECTED);
      drawProvisionScreen();
      return;
    }

    // Back button (10, 190, 80, 35)
    if (x >= 10 && x <= 90 && y >= 190 && y <= 225) {
      // Un-highlight current
      sendHighlightCommand(discoveredMacs[selectedMacIndex], HIGHLIGHT_IDLE);
      provisionPhase = PHASE_DISCOVERING;
      drawProvisionScreen();
      return;
    }

    // Done button (230, 190, 80, 35)
    if (x >= 230 && x <= 310 && y >= 190 && y <= 225) {
      // Un-highlight current
      sendHighlightCommand(discoveredMacs[selectedMacIndex], HIGHLIGHT_IDLE);
      provisionPhase = PHASE_IDLE;
      currentMode = MODE_MENU;
      drawMenu();
      return;
    }
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

  // Back button (smaller to make room for auto-cycle)
  tft.fillRoundRect(250, 145, 60, 40, 4, TFT_RED);
  tft.setTextColor(TFT_WHITE, TFT_RED);
  tft.setTextSize(1);
  tft.setCursor(265, 160);
  tft.print("Back");
  
  // Auto-cycle toggle button
  uint16_t cycleColor = autoCycleEnabled ? TFT_GREEN : TFT_DARKGREY;
  tft.fillRoundRect(130, 145, 110, 40, 4, cycleColor);
  tft.setTextColor(TFT_WHITE, cycleColor);
  tft.setTextSize(1);
  tft.setCursor(135, 155);
  tft.print("Auto-Cycle");
  tft.setCursor(140, 167);
  tft.print(autoCycleEnabled ? "ON" : "OFF");

  // Color control row
  tft.setTextSize(1);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setCursor(10, 195);
  tft.println("Color:");
  
  // Color prev button
  tft.fillRoundRect(50, 195, 40, 25, 4, TFT_ORANGE);
  tft.setTextColor(TFT_WHITE, TFT_ORANGE);
  tft.setTextSize(2);
  tft.setCursor(65, 200);
  tft.print("<");
  
  // Color next button
  tft.fillRoundRect(95, 195, 40, 25, 4, TFT_ORANGE);
  tft.setTextColor(TFT_WHITE, TFT_ORANGE);
  tft.setTextSize(2);
  tft.setCursor(110, 200);
  tft.print(">");
  
  // Show current color index
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setTextSize(1);
  tft.setCursor(145, 202);
  tft.print("#");
  tft.print(currentDigitColor);
  
  // Speed control row
  tft.setCursor(200, 195);
  tft.println("Speed:");
  
  // Speed slower button
  tft.fillRoundRect(240, 195, 30, 25, 4, TFT_PURPLE);
  tft.setTextColor(TFT_WHITE, TFT_PURPLE);
  tft.setTextSize(2);
  tft.setCursor(250, 200);
  tft.print("-");
  
  // Speed faster button
  tft.fillRoundRect(275, 195, 30, 25, 4, TFT_PURPLE);
  tft.setTextColor(TFT_WHITE, TFT_PURPLE);
  tft.setTextSize(2);
  tft.setCursor(285, 200);
  tft.print("+");
  
  // Show current speed
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setTextSize(1);
  tft.setCursor(200, 210);
  tft.printf("%.1fs", currentDigitSpeed);
}

void handleDigitsTouch(uint16_t x, uint16_t y) {
  // Check digit buttons (0-4, top row) - shows as "X " (digit left, space right)
  if (y >= 45 && y <= 85) {
    for (int i = 0; i <= 4; i++) {
      int buttonX = 10 + i * 60;
      if (x >= buttonX && x <= buttonX + 50) {
        sendTwoDigitPattern(i, 11);  // digit on left, space on right
        return;
      }
    }
  }

  // Check digit buttons (5-9, bottom row) - shows as "X " (digit left, space right)
  if (y >= 95 && y <= 135) {
    for (int i = 5; i <= 9; i++) {
      int buttonX = 10 + (i - 5) * 60;
      if (x >= buttonX && x <= buttonX + 50) {
        sendTwoDigitPattern(i, 11);  // digit on left, space on right
        return;
      }
    }
  }

  // Check special character buttons
  if (y >= 145 && y <= 185) {
    // Colon button - shows ": " (colon left, space right)
    if (x >= 10 && x <= 60) {
      sendTwoDigitPattern(10, 11);  // colon on left, space on right
      return;
    }
    // Space button - shows "  " (space both)
    if (x >= 70 && x <= 120) {
      sendTwoDigitPattern(11, 11);  // space on both
      return;
    }
    // Auto-cycle toggle button
    if (x >= 130 && x <= 240) {
      autoCycleEnabled = !autoCycleEnabled;
      if (autoCycleEnabled) {
        // Reset auto-cycle state when enabling
        autoCycleNumber = 0;
        autoCycleDirection = true;
        lastAutoCycleTime = millis();
      }
      drawDigitsScreen();
      return;
    }
    // Back button (updated coordinates)
    if (x >= 250 && x <= 310) {
      currentMode = MODE_MENU;
      drawMenu();
      return;
    }
  }
  
  // Check color control buttons
  if (y >= 195 && y <= 220) {
    // Color prev button
    if (x >= 50 && x <= 90) {
      currentDigitColor = (currentDigitColor == 0) ? (COLOR_PALETTE_SIZE - 1) : currentDigitColor - 1;
      drawDigitsScreen();
      return;
    }
    // Color next button
    if (x >= 95 && x <= 135) {
      currentDigitColor = (currentDigitColor + 1) % COLOR_PALETTE_SIZE;
      drawDigitsScreen();
      return;
    }
    // Speed slower button
    if (x >= 240 && x <= 270) {
      if (currentDigitSpeed < 5.0) currentDigitSpeed += 0.5;
      drawDigitsScreen();
      return;
    }
    // Speed faster button
    if (x >= 275 && x <= 305) {
      if (currentDigitSpeed > 0.5) currentDigitSpeed -= 0.5;
      drawDigitsScreen();
      return;
    }
  }
}

// Send two digits pattern to 12 pixels (3 rows × 4 columns)
// Only the 12 targeted pixels will respond; others continue their current animation
void sendTwoDigitPattern(uint8_t leftDigit, uint8_t rightDigit) {
  if (leftDigit > 11 || rightDigit > 11) return; // Invalid digit

  ESPNowPacket packet;
  packet.angleCmd.command = CMD_SET_ANGLES;

  // Target only the 12 pixels used for the two-digit display
  packet.angleCmd.clearTargetMask();
  for (int i = 0; i < 6; i++) {
    packet.angleCmd.setTargetPixel(digit1PixelIds[i]);
    packet.angleCmd.setTargetPixel(digit2PixelIds[i]);
  }

  // Use random transition and set duration from speed control
  packet.angleCmd.transition = getRandomTransition();
  packet.angleCmd.duration = floatToDuration(currentDigitSpeed);

  // Set left digit (digit 1) pattern
  DigitPattern& leftPattern = digitPatterns[leftDigit];
  for (int i = 0; i < 6; i++) {
    uint8_t pixelId = digit1PixelIds[i];

    // Random directions for each hand
    RotationDirection dir1 = (random(2) == 0) ? DIR_CW : DIR_CCW;
    RotationDirection dir2 = (random(2) == 0) ? DIR_CW : DIR_CCW;
    RotationDirection dir3 = (random(2) == 0) ? DIR_CW : DIR_CCW;

    packet.angleCmd.setPixelAngles(pixelId,
      leftPattern.angles[i][0],
      leftPattern.angles[i][1],
      leftPattern.angles[i][2],
      dir1, dir2, dir3);
    packet.angleCmd.setPixelStyle(pixelId, currentDigitColor, leftPattern.opacity[i]);
  }

  // Set right digit (digit 2) pattern
  DigitPattern& rightPattern = digitPatterns[rightDigit];
  for (int i = 0; i < 6; i++) {
    uint8_t pixelId = digit2PixelIds[i];

    // Random directions for each hand
    RotationDirection dir1 = (random(2) == 0) ? DIR_CW : DIR_CCW;
    RotationDirection dir2 = (random(2) == 0) ? DIR_CW : DIR_CCW;
    RotationDirection dir3 = (random(2) == 0) ? DIR_CW : DIR_CCW;

    packet.angleCmd.setPixelAngles(pixelId,
      rightPattern.angles[i][0],
      rightPattern.angles[i][1],
      rightPattern.angles[i][2],
      dir1, dir2, dir3);
    packet.angleCmd.setPixelStyle(pixelId, currentDigitColor, rightPattern.opacity[i]);
  }

  // Send the packet
  if (ESPNowComm::sendPacket(&packet, sizeof(AngleCommandPacket))) {
    const char* digitNames[] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", ":", " "};
    Serial.print("Sent two digits: ");
    Serial.print(digitNames[leftDigit]);
    Serial.print(digitNames[rightDigit]);
    Serial.print(" with transition: ");
    Serial.print(getTransitionName(packet.angleCmd.transition));
    Serial.print(", duration: ");
    Serial.print(durationToFloat(packet.angleCmd.duration), 1);
    Serial.println("s (targeting 12 pixels only)");
  } else {
    Serial.println("Failed to send two-digit packet!");
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
    // Target all pixels (broadcast mode)
    packet.angleCmd.clearTargetMask();

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
    // Target only the selected pixel - others will ignore the command
    packet.angleCmd.clearTargetMask();
    packet.angleCmd.setTargetPixel(manualState.selectedPixel);

    // Only need to set values for the targeted pixel
    packet.angleCmd.setPixelAngles(manualState.selectedPixel,
      manualState.angles[0],
      manualState.angles[1],
      manualState.angles[2],
      manualState.directions[0],
      manualState.directions[1],
      manualState.directions[2]);
    packet.angleCmd.setPixelStyle(manualState.selectedPixel, manualState.colorIndex, manualState.opacity);

    Serial.print("Sending manual command to pixel ");
    Serial.print(manualState.selectedPixel);
    Serial.println(" (others will continue current animation)");
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

// ===== OTA UPDATE FUNCTIONS =====

// Handle OTA status acknowledgment from pixel
void handleOTAAck(const OTAAckPacket& ack) {
  if (ack.pixelId < MAX_PIXELS) {
    otaPixelStatus[ack.pixelId] = ack.status;
    otaPixelProgress[ack.pixelId] = ack.progress;

    Serial.print("OTA ACK from pixel ");
    Serial.print(ack.pixelId);
    Serial.print(": status=");
    Serial.print(ack.status);
    Serial.print(", progress=");
    Serial.print(ack.progress);
    Serial.println("%");

    // Refresh OTA screen if we're in OTA mode
    if (currentMode == MODE_OTA && otaPhase == OTA_IN_PROGRESS) {
      drawOTAScreen();
    }
  }
}

// HTTP handler for serving firmware binary
void handleFirmwareRequest() {
  File file = LittleFS.open(OTA_FIRMWARE_PATH, "r");
  if (!file) {
    otaServer.send(404, "text/plain", "Firmware not found");
    Serial.println("OTA: Firmware file not found!");
    return;
  }

  Serial.print("OTA: Serving firmware, size=");
  Serial.println(file.size());

  otaServer.streamFile(file, "application/octet-stream");
  file.close();
}

// Initialize WiFi AP and HTTP server for OTA
void initOTAServer() {
  if (otaServerRunning) return;

  Serial.println("OTA: Starting WiFi AP...");

  // Start WiFi in AP+STA mode (allows ESP-NOW to continue working)
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(OTA_AP_SSID, OTA_AP_PASSWORD);

  IPAddress apIP = WiFi.softAPIP();
  Serial.print("OTA: AP started. IP: ");
  Serial.println(apIP);

  // Initialize LittleFS
  if (!LittleFS.begin(true)) {
    Serial.println("OTA: LittleFS mount failed!");
    return;
  }

  // Check if firmware file exists
  if (LittleFS.exists(OTA_FIRMWARE_PATH)) {
    File file = LittleFS.open(OTA_FIRMWARE_PATH, "r");
    firmwareSize = file.size();
    file.close();
    Serial.print("OTA: Firmware found, size=");
    Serial.println(firmwareSize);
  } else {
    firmwareSize = 0;
    Serial.println("OTA: No firmware file found. Upload via USB first.");
  }

  // Set up HTTP routes
  otaServer.on("/firmware.bin", HTTP_GET, handleFirmwareRequest);
  otaServer.on("/", HTTP_GET, []() {
    otaServer.send(200, "text/plain", "Twenty-Four Times OTA Server");
  });

  otaServer.begin();
  otaServerRunning = true;
  otaPhase = OTA_READY;

  Serial.println("OTA: HTTP server started on port 80");

  // Re-initialize ESP-NOW (WiFi mode change may have disrupted it)
  ESPNowComm::initSender(ESPNOW_CHANNEL);
  ESPNowComm::setReceiveCallback(onMasterPacketReceived);
}

// Stop OTA server and return to normal operation
void stopOTAServer() {
  if (!otaServerRunning) return;

  otaServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  otaServerRunning = false;
  otaPhase = OTA_IDLE;

  // Re-initialize ESP-NOW
  ESPNowComm::initSender(ESPNOW_CHANNEL);
  ESPNowComm::setReceiveCallback(onMasterPacketReceived);

  Serial.println("OTA: Server stopped");
}

// Send OTA notify command to all pixels
void sendOTANotifyCommand() {
  if (firmwareSize == 0) {
    Serial.println("OTA: No firmware to send!");
    return;
  }

  ESPNowPacket packet;
  packet.otaNotify.command = CMD_OTA_NOTIFY;

  // Copy WiFi credentials
  strncpy(packet.otaNotify.ssid, OTA_AP_SSID, 31);
  packet.otaNotify.ssid[31] = '\0';
  strncpy(packet.otaNotify.password, OTA_AP_PASSWORD, 31);
  packet.otaNotify.password[31] = '\0';

  // Build firmware URL using AP IP
  IPAddress apIP = WiFi.softAPIP();
  snprintf(packet.otaNotify.firmwareUrl, 127, "http://%d.%d.%d.%d/firmware.bin",
           apIP[0], apIP[1], apIP[2], apIP[3]);
  packet.otaNotify.firmwareUrl[127] = '\0';

  packet.otaNotify.firmwareSize = firmwareSize;
  packet.otaNotify.firmwareCrc32 = 0;  // Skip CRC check for now

  Serial.print("OTA: Sending notify - URL: ");
  Serial.println(packet.otaNotify.firmwareUrl);

  if (ESPNowComm::sendPacket(&packet, sizeof(OTANotifyPacket))) {
    Serial.println("OTA: Notify command sent!");
    otaPhase = OTA_IN_PROGRESS;

    // Reset pixel status tracking
    for (int i = 0; i < MAX_PIXELS; i++) {
      otaPixelStatus[i] = OTA_STATUS_IDLE;
      otaPixelProgress[i] = 0;
    }
  } else {
    Serial.println("OTA: Failed to send notify command!");
  }
}

// Draw OTA screen
void drawOTAScreen() {
  tft.fillScreen(COLOR_BG);

  // Title
  tft.setTextColor(TFT_ORANGE, COLOR_BG);
  tft.setTextSize(2);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("OTA Update", 160, 5);
  tft.setTextDatum(TL_DATUM);

  tft.setTextSize(1);

  if (otaPhase == OTA_IDLE) {
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.setCursor(10, 35);
    tft.println("Update pixel firmware wirelessly.");
    tft.setCursor(10, 50);
    tft.println("1. Upload firmware.bin via USB");
    tft.setCursor(10, 65);
    tft.println("2. Start server & trigger update");

    // Start Server button
    tft.fillRoundRect(60, 100, 200, 50, 8, TFT_DARKGREEN);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREEN);
    tft.setTextSize(2);
    tft.setCursor(90, 115);
    tft.println("Start Server");

    // Back button
    tft.fillRoundRect(110, 170, 100, 40, 8, TFT_DARKGREY);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
    tft.setCursor(135, 180);
    tft.println("Back");

  } else if (otaPhase == OTA_READY) {
    tft.setTextColor(TFT_GREEN, COLOR_BG);
    tft.setCursor(10, 35);
    tft.print("Server running! SSID: ");
    tft.println(OTA_AP_SSID);

    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.setCursor(10, 50);
    tft.print("Firmware size: ");
    if (firmwareSize > 0) {
      tft.print(firmwareSize / 1024);
      tft.println(" KB");
    } else {
      tft.setTextColor(TFT_RED, COLOR_BG);
      tft.println("NOT FOUND!");
    }

    // Send Update button (only if firmware exists)
    if (firmwareSize > 0) {
      tft.fillRoundRect(60, 80, 200, 50, 8, TFT_BLUE);
      tft.setTextColor(TFT_WHITE, TFT_BLUE);
      tft.setTextSize(2);
      tft.setCursor(80, 95);
      tft.println("Send Update");
    }

    // Stop Server button
    tft.fillRoundRect(60, 145, 200, 40, 8, TFT_RED);
    tft.setTextColor(TFT_WHITE, TFT_RED);
    tft.setTextSize(2);
    tft.setCursor(95, 155);
    tft.println("Stop Server");

    // Back button
    tft.fillRoundRect(110, 195, 100, 35, 8, TFT_DARKGREY);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
    tft.setTextSize(1);
    tft.setCursor(140, 205);
    tft.println("Back");

  } else if (otaPhase == OTA_IN_PROGRESS) {
    tft.setTextColor(TFT_YELLOW, COLOR_BG);
    tft.setTextSize(2);
    tft.setCursor(50, 35);
    tft.println("Update in progress...");

    // Count pixels by status
    int idle = 0, downloading = 0, success = 0, error = 0;
    for (int i = 0; i < MAX_PIXELS; i++) {
      switch (otaPixelStatus[i]) {
        case OTA_STATUS_IDLE: idle++; break;
        case OTA_STATUS_STARTING:
        case OTA_STATUS_DOWNLOADING:
        case OTA_STATUS_FLASHING: downloading++; break;
        case OTA_STATUS_SUCCESS: success++; break;
        case OTA_STATUS_ERROR: error++; break;
      }
    }

    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.setCursor(10, 70);
    tft.print("Waiting: "); tft.println(idle);
    tft.setCursor(10, 85);
    tft.print("Updating: "); tft.println(downloading);
    tft.setCursor(10, 100);
    tft.setTextColor(TFT_GREEN, COLOR_BG);
    tft.print("Success: "); tft.println(success);
    tft.setCursor(10, 115);
    tft.setTextColor(TFT_RED, COLOR_BG);
    tft.print("Failed: "); tft.println(error);

    // Done button (always available to exit)
    tft.fillRoundRect(110, 180, 100, 40, 8, TFT_DARKGREY);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
    tft.setTextSize(2);
    tft.setCursor(135, 190);
    tft.println("Done");
  }
}

// Handle touch in OTA mode
void handleOTATouch(uint16_t x, uint16_t y) {
  if (otaPhase == OTA_IDLE) {
    // Start Server button (60, 100, 200, 50)
    if (x >= 60 && x <= 260 && y >= 100 && y <= 150) {
      initOTAServer();
      drawOTAScreen();
      return;
    }
    // Back button (110, 170, 100, 40)
    if (x >= 110 && x <= 210 && y >= 170 && y <= 210) {
      currentMode = MODE_MENU;
      drawMenu();
      return;
    }

  } else if (otaPhase == OTA_READY) {
    // Send Update button (60, 80, 200, 50) - only if firmware exists
    if (firmwareSize > 0 && x >= 60 && x <= 260 && y >= 80 && y <= 130) {
      sendOTANotifyCommand();
      drawOTAScreen();
      return;
    }
    // Stop Server button (60, 145, 200, 40)
    if (x >= 60 && x <= 260 && y >= 145 && y <= 185) {
      stopOTAServer();
      drawOTAScreen();
      return;
    }
    // Back button (110, 195, 100, 35)
    if (x >= 110 && x <= 210 && y >= 195 && y <= 230) {
      stopOTAServer();
      currentMode = MODE_MENU;
      drawMenu();
      return;
    }

  } else if (otaPhase == OTA_IN_PROGRESS) {
    // Done button (110, 180, 100, 40)
    if (x >= 110 && x <= 210 && y >= 180 && y <= 220) {
      stopOTAServer();
      currentMode = MODE_MENU;
      drawMenu();
      return;
    }
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

  // Initialize ESP-NOW in sender mode (also enables receiving for discovery responses)
  if (ESPNowComm::initSender(ESPNOW_CHANNEL)) {
    // Register receive callback for discovery responses
    ESPNowComm::setReceiveCallback(onMasterPacketReceived);

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
            lastPingTime = currentTime;  // Initialize ping timer
            break;
          case MODE_PROVISION:
            provisionPhase = PHASE_IDLE;
            drawProvisionScreen();
            break;
          case MODE_MANUAL:
            drawManualScreen();
            lastPingTime = currentTime;  // Initialize ping timer
            break;
          case MODE_OTA:
            otaPhase = OTA_IDLE;
            drawOTAScreen();
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
    } else if (currentMode == MODE_PROVISION) {
      // Provision mode has its own touch handler
      handleProvisionTouch(tx, ty);
    } else if (currentMode == MODE_OTA) {
      // OTA mode has its own touch handler
      handleOTATouch(tx, ty);
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
      // Send periodic pings to keep pixels alive
      if (currentTime - lastPingTime >= 3000) {  // Ping every 3 seconds
        sendPing();
        lastPingTime = currentTime;
      }
      
      // Handle auto-cycle mode (cycles 00-99 on two-digit display)
      if (autoCycleEnabled) {
        // Calculate total wait time: animation duration + 3 seconds
        unsigned long totalWaitTime = (unsigned long)(currentDigitSpeed * 1000) + 3000;

        if (currentTime - lastAutoCycleTime >= totalWaitTime) {
          // Send current two-digit number
          uint8_t leftDigit = autoCycleNumber / 10;   // Tens digit
          uint8_t rightDigit = autoCycleNumber % 10;  // Ones digit
          sendTwoDigitPattern(leftDigit, rightDigit);

          // Update number for next cycle (bounces 0->99->0)
          if (autoCycleDirection) {
            // Going 0->99
            autoCycleNumber++;
            if (autoCycleNumber > 99) {
              autoCycleNumber = 98;  // Go to 98 next
              autoCycleDirection = false;
            }
          } else {
            // Going 99->0
            if (autoCycleNumber == 0) {
              autoCycleNumber = 1;  // Go to 1 next
              autoCycleDirection = true;
            } else {
              autoCycleNumber--;
            }
          }

          lastAutoCycleTime = currentTime;
        }
      }
      break;
    }

    case MODE_PROVISION: {
      // Handle periodic discovery during PHASE_DISCOVERING
      if (provisionPhase == PHASE_DISCOVERING) {
        // Send discovery command periodically and update display
        if (currentTime - lastDiscoveryTime >= DISCOVERY_INTERVAL) {
          sendDiscoveryCommand();
          lastDiscoveryTime = currentTime;
          // Redraw to update count
          drawProvisionScreen();
        }
      }
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

    case MODE_OTA: {
      // Handle HTTP server requests when running
      if (otaServerRunning) {
        otaServer.handleClient();
      }
      break;
    }
  }

  // Small delay to avoid busy-waiting
  delay(10);
}

