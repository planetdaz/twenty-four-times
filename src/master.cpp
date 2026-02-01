#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <WiFi.h>
#include <TFT_eSPI.h>
#include <ESPNowComm.h>
#include "animations/unity.h"
// fluid_time.h included later after DigitPattern definition

// ===== FIRMWARE VERSION =====
#define FIRMWARE_VERSION_MAJOR 1
#define FIRMWARE_VERSION_MINOR 35

// ===== WIFI & TIME CONFIGURATION =====
const char* WIFI_SSID = "Frontier5664";
const char* WIFI_PASSWORD = "8854950591";
const char* NTP_SERVER = "pool.ntp.org";
const long GMT_OFFSET_SEC = -6 * 3600;  // UTC-6 (CST)
const int DAYLIGHT_OFFSET_SEC = 0;      // Set to 3600 if DST is active
bool wifiConnected = false;

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
  MODE_ANIMATIONS,  // Animations menu - select animation
  MODE_UNITY,       // Unity animation - all pixels move in unison
  MODE_FLUID_TIME,  // Fluid Time animation - staggered wave effect
  MODE_ORBIT_TIME,  // Orbit Time animation - continuous orbital rotation
  MODE_DIGITS,      // Display digits 0-9 with animations
  MODE_PROVISION,   // Discovery and provisioning of pixels
  MODE_OTA,         // OTA firmware update for pixels
  MODE_VERSION      // Display firmware versions
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
unsigned long lastMenuTimeUpdate = 0;  // Track last time display update on menu
const unsigned long IDENTIFY_DURATION = 5000;    // Identify phase duration
const unsigned long PING_INTERVAL = 5000;        // 5 seconds between pings

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

// Dev machine OTA server configuration
// Master creates WiFi AP, dev machine joins it and runs Node.js OTA server
// All 24 pixels download in parallel from dev machine (~15 seconds total)
const char* OTA_DEV_SERVER_IP = "192.168.4.2";
const uint16_t OTA_DEV_SERVER_PORT = 3000;

// OTA state tracking
enum OTAPhase {
  OTA_IDLE,           // Waiting for user to start
  OTA_READY,          // WiFi AP running, ready to send update
  OTA_IN_PROGRESS,    // Updating all pixels in parallel
  OTA_COMPLETE        // All done
};

// Special pixel ID for broadcast (send to all pixels)
const uint8_t BROADCAST_PIXEL_ID = 0xFF;

OTAPhase otaPhase = OTA_IDLE;
uint32_t firmwareSize = 0;
uint8_t otaPixelStatus[MAX_PIXELS];  // Status of each pixel (OTAStatus enum)
uint8_t otaPixelProgress[MAX_PIXELS]; // Progress of each pixel (0-100)
bool otaPixelSelected[MAX_PIXELS];  // true = pixel selected for update
bool otaPixelUpdated[MAX_PIXELS];   // true = pixel has been updated (green)

// Parallel OTA orchestration
unsigned long otaStartTime = 0;      // When OTA broadcast was sent
const unsigned long OTA_TOTAL_TIMEOUT = 120000; // 120 seconds max for all pixels
// IMPORTANT: Pixels disable ESP-NOW during the actual WiFi download.
// Master just broadcasts the command and lets all pixels update independently.

// Flag to request OTA screen redraw from the main loop (never draw from ESP-NOW callbacks)
volatile bool otaScreenNeedsRedraw = false;

// ===== VERSION TRACKING =====
// Stores version info received from pixels
struct PixelVersionInfo {
  bool received;
  uint8_t major;
  uint8_t minor;
};
PixelVersionInfo pixelVersions[MAX_PIXELS];
unsigned long versionRequestTime = 0;
bool versionScreenNeedsRedraw = false;  // Flag to redraw from main loop, not callback

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

// Include animation headers after DigitPattern definition
#include "animations/digit_display.h"  // Shared digit display helper
#include "animations/fluid_time.h"
#include "animations/orbit_time.h"

// Auto-cycle mode variables (cycles 00-99 for two-digit display)
bool autoCycleEnabled = false;
uint8_t autoCycleNumber = 0;        // Current number (0-99)
bool autoCycleDirection = true;     // true = 0->99, false = 99->0
unsigned long lastAutoCycleTime = 0;

// Manual digit entry variables
uint8_t pendingDigits[2] = {255, 255};  // 255 = not set, 0-9 = digit, 10 = colon, 11 = space
uint8_t pendingCount = 0;               // How many digits entered (0, 1, or 2)
uint8_t lastSentLeft = 11;              // Last sent left digit (11 = space)
uint8_t lastSentRight = 11;             // Last sent right digit (11 = space)

// ===== FUNCTION DECLARATIONS =====
void sendPing();
void sendReset();
// Animation functions
void drawAnimationsScreen();
void handleAnimationsTouch(uint16_t x, uint16_t y);
// Digits functions
void drawDigitsScreen();
void handleDigitsTouch(uint16_t x, uint16_t y);
void sendTwoDigitPattern(uint8_t leftDigit, uint8_t rightDigit);
// Provisioning functions
void onMasterPacketReceived(const ESPNowPacket* packet, size_t len);
void drawProvisionScreen();
void handleProvisionTouch(uint16_t x, uint16_t y);
void sendDiscoveryCommand();
void sendHighlightCommand(uint8_t* targetMac, HighlightState state);
void sendHighlightToAll(HighlightState state);
void sendAssignIdCommand(uint8_t* targetMac, uint8_t newId);
void sendFactoryResetIds();
// OTA functions
void initOTAServer();
void stopOTAServer();
void drawOTAScreen();
void handleOTATouch(uint16_t x, uint16_t y);
void sendOTAUpdate();
void handleOTAAck(const OTAAckPacket& ack);
// Version functions
void drawVersionScreen();
void handleVersionTouch(uint16_t x, uint16_t y);
void sendGetVersionCommand();
void handleVersionResponse(const VersionResponsePacket& resp);

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

  // Button 1: Animations (top left)
  tft.fillRoundRect(10, 90, 150, 60, 8, TFT_DARKGREEN);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREEN);
  tft.setTextSize(2);
  tft.setCursor(20, 105);
  tft.println("Animations");
  tft.setTextSize(1);
  tft.setCursor(25, 125);
  tft.println("Visual patterns");

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

  // Button 4: OTA Update (bottom right - expanded)
  tft.fillRoundRect(170, 160, 140, 60, 8, TFT_CYAN);
  tft.setTextColor(TFT_BLACK, TFT_CYAN);
  tft.setTextSize(2);
  tft.setCursor(207, 175);
  tft.println("OTA");
  tft.setTextSize(1);
  tft.setCursor(180, 200);
  tft.println("Firmware Update");

  // Version button (small, top right corner)
  tft.fillRoundRect(270, 5, 45, 25, 4, TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.setTextSize(1);
  tft.setCursor(275, 12);
  tft.print("v");
  tft.print(FIRMWARE_VERSION_MAJOR);
  tft.print(".");
  tft.print(FIRMWARE_VERSION_MINOR);

  // Current time display (bottom center)
  tft.setTextSize(1);
  tft.setTextColor(TFT_CYAN, COLOR_BG);
  tft.setTextDatum(BC_DATUM);  // Bottom center alignment
  tft.drawString(getCurrentTimeString(), 160, 235);
  tft.setTextDatum(TL_DATUM);  // Reset to top-left
}

// Check which menu button was pressed
ControlMode checkMenuTouch(uint16_t x, uint16_t y) {
  // Version button (270, 5, 45, 25) - check first since it's small
  if (x >= 270 && x <= 315 && y >= 5 && y <= 30) {
    return MODE_VERSION;
  }
  // Button 1: Animations (10, 90, 150, 60)
  if (x >= 10 && x <= 160 && y >= 90 && y <= 150) {
    return MODE_ANIMATIONS;
  }
  // Button 2: Digits (170, 90, 140, 60)
  if (x >= 170 && x <= 310 && y >= 90 && y <= 150) {
    return MODE_DIGITS;
  }
  // Button 3: Provision (10, 160, 150, 60)
  if (x >= 10 && x <= 160 && y >= 160 && y <= 220) {
    return MODE_PROVISION;
  }
  // Button 4: OTA (170, 160, 140, 60)
  if (x >= 170 && x <= 310 && y >= 160 && y <= 220) {
    return MODE_OTA;
  }

  return MODE_MENU;  // No button pressed
}

// ===== ANIMATIONS MENU =====

// Draw animations selection menu
void drawAnimationsScreen() {
  tft.fillScreen(COLOR_BG);

  // Title - centered for landscape mode (320x240)
  tft.setTextColor(COLOR_ACCENT, COLOR_BG);
  tft.setTextSize(3);
  tft.setTextDatum(TC_DATUM);  // Top center alignment
  tft.drawString("Animations", 160, 10);
  tft.setTextDatum(TL_DATUM);  // Reset to top-left for buttons

  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY, COLOR_BG);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("Select Animation:", 160, 45);
  tft.setTextDatum(TL_DATUM);

  // Unity animation button (left)
  tft.fillRoundRect(10, 70, 145, 80, 8, TFT_DARKGREEN);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREEN);
  tft.setTextSize(2);
  tft.setCursor(40, 85);
  tft.println("Unity");
  tft.setTextSize(1);
  tft.setCursor(20, 110);
  tft.println("Synchronized");
  tft.setCursor(25, 125);
  tft.println("All together");

  // Fluid Time animation button (right)
  tft.fillRoundRect(165, 70, 145, 80, 8, TFT_PURPLE);
  tft.setTextColor(TFT_WHITE, TFT_PURPLE);
  tft.setTextSize(2);
  tft.setCursor(175, 85);
  tft.println("Fluid Time");
  tft.setTextSize(1);
  tft.setCursor(175, 110);
  tft.println("Staggered wave");
  tft.setCursor(185, 125);
  tft.println("Left to right");

  // Orbit Time animation button (bottom center)
  tft.fillRoundRect(85, 160, 150, 50, 8, TFT_ORANGE);
  tft.setTextColor(TFT_WHITE, TFT_ORANGE);
  tft.setTextSize(2);
  tft.setCursor(95, 170);
  tft.println("Orbit Time");
  tft.setTextSize(1);
  tft.setCursor(90, 190);
  tft.println("Continuous orbit");

  // Back button (bottom right)
  tft.fillRoundRect(245, 160, 65, 50, 8, TFT_RED);
  tft.setTextColor(TFT_WHITE, TFT_RED);
  tft.setTextSize(2);
  tft.setCursor(253, 178);
  tft.print("Back");
}

// Handle touch on animations screen
void handleAnimationsTouch(uint16_t x, uint16_t y) {
  // Unity button (10, 70, 145, 80)
  if (x >= 10 && x <= 155 && y >= 70 && y <= 150) {
    currentMode = MODE_UNITY;
    sendUnityPattern();  // Send first pattern immediately
    lastCommandTime = millis();
    return;
  }

  // Fluid Time button (165, 70, 145, 80)
  if (x >= 165 && x <= 310 && y >= 70 && y <= 150) {
    currentMode = MODE_FLUID_TIME;
    // Fluid Time will start automatically in the loop
    return;
  }

  // Orbit Time button (85, 160, 150, 50)
  if (x >= 85 && x <= 235 && y >= 160 && y <= 210) {
    currentMode = MODE_ORBIT_TIME;
    // Orbit Time will start automatically in the loop
    return;
  }

  // Back button (245, 160, 65, 50)
  if (x >= 245 && x <= 310 && y >= 160 && y <= 210) {
    currentMode = MODE_MENU;
    drawMenu();
    return;
  }
}

// ===== PROVISIONING FUNCTIONS =====

// Receive callback for discovery responses and OTA acks from pixels
void onMasterPacketReceived(const ESPNowPacket* packet, size_t len) {
  if (packet->command == CMD_DISCOVERY_RESPONSE) {
    // CRITICAL: Only process discovery responses if we're STILL in discovery phase
    // This prevents race condition where responses arrive after user exits provision mode
    if (provisionPhase != PHASE_DISCOVERING) {
      Serial.println("Discovery response ignored - not in discovery phase");
      return;
    }

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

      // Send HIGHLIGHT_DISCOVERY_FOUND to show "!" on the pixel
      uint8_t mac[6];
      memcpy(mac, resp.mac, 6);
      sendHighlightCommand(mac, HIGHLIGHT_DISCOVERY_FOUND);
      Serial.println("Sent HIGHLIGHT_DISCOVERY_FOUND to pixel");
    }
  } else if (packet->command == CMD_OTA_ACK) {
    handleOTAAck(packet->otaAck);
  } else if (packet->command == CMD_VERSION_RESPONSE) {
    handleVersionResponse(packet->versionResponse);
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

// Send highlight command to all discovered pixels
void sendHighlightToAll(HighlightState state) {
  for (uint8_t i = 0; i < discoveredCount; i++) {
    sendHighlightCommand(discoveredMacs[i], state);
    delay(5);  // Small delay to avoid flooding
  }
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

// Factory reset all pixel IDs (broadcast unprovisioned state)
void sendFactoryResetIds() {
  ESPNowPacket packet;
  packet.setPixelId.command = CMD_SET_PIXEL_ID;
  // Use broadcast MAC to target all pixels
  memcpy(packet.setPixelId.targetMac, BROADCAST_MAC, 6);
  packet.setPixelId.pixelId = PIXEL_ID_UNPROVISIONED;

  if (ESPNowComm::sendPacket(&packet, sizeof(SetPixelIdPacket))) {
    Serial.println("Factory reset broadcast sent - all pixel IDs reset to unprovisioned");
  } else {
    Serial.println("Failed to send factory reset");
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
    tft.println("Pixels will display ? then ! when found.");

    // Start Discovery button
    tft.fillRoundRect(60, 90, 200, 50, 8, TFT_DARKGREEN);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREEN);
    tft.setTextSize(2);
    tft.setCursor(75, 105);
    tft.println("Start Discovery");

    // Factory Reset button (small, red, for testing)
    tft.fillRoundRect(10, 150, 150, 35, 4, TFT_RED);
    tft.setTextColor(TFT_WHITE, TFT_RED);
    tft.setTextSize(1);
    tft.setCursor(15, 160);
    tft.println("Reset All IDs");
    tft.setCursor(15, 172);
    tft.println("(for testing)");

    // Back button
    tft.fillRoundRect(200, 150, 110, 35, 4, TFT_DARKGREY);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
    tft.setTextSize(2);
    tft.setCursor(230, 158);
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

    // Show next ID to assign with +/- buttons
    tft.setTextColor(TFT_CYAN, COLOR_BG);
    tft.setTextSize(2);
    tft.setCursor(10, 95);
    tft.print("Assign ID:");

    // +/- buttons for adjusting ID
    tft.fillRoundRect(130, 92, 30, 25, 4, TFT_DARKGREY);
    tft.fillRoundRect(165, 92, 30, 25, 4, TFT_DARKGREY);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
    tft.setTextSize(2);
    tft.setCursor(140, 97);
    tft.print("-");
    tft.setCursor(175, 97);
    tft.print("+");

    // Display ID after buttons
    tft.setTextColor(TFT_CYAN, COLOR_BG);
    tft.setTextSize(3);
    tft.setCursor(210, 92);
    tft.print(nextIdToAssign);

    // Prev/Next buttons
    tft.fillRoundRect(10, 140, 60, 35, 4, TFT_DARKBLUE);
    tft.setTextColor(TFT_WHITE, TFT_DARKBLUE);
    tft.setTextSize(2);
    tft.setCursor(20, 148);
    tft.println("Prev");

    tft.fillRoundRect(80, 140, 60, 35, 4, TFT_DARKBLUE);
    tft.setCursor(90, 148);
    tft.println("Next");

    // Assign button (expanded to fill the space)
    tft.fillRoundRect(160, 140, 150, 35, 4, TFT_DARKGREEN);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREEN);
    tft.setCursor(205, 148);
    tft.println("Assign");

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

    // Factory Reset button (10, 150, 150, 35)
    if (x >= 10 && x <= 160 && y >= 150 && y <= 185) {
      // Show confirmation on screen
      tft.fillScreen(TFT_RED);
      tft.setTextColor(TFT_WHITE, TFT_RED);
      tft.setTextSize(2);
      tft.setCursor(40, 100);
      tft.println("Resetting IDs...");

      // Send factory reset command
      sendFactoryResetIds();

      // Wait a moment
      delay(1000);

      // Show confirmation
      tft.fillScreen(TFT_GREEN);
      tft.setTextColor(TFT_WHITE, TFT_GREEN);
      tft.setCursor(60, 100);
      tft.println("IDs Reset!");
      delay(1000);

      // Redraw provision screen
      drawProvisionScreen();
      return;
    }

    // Back button (200, 150, 110, 35)
    if (x >= 200 && x <= 310 && y >= 150 && y <= 185) {
      // Send reset to clear any lingering highlight modes
      sendReset();
      currentMode = MODE_MENU;
      drawMenu();
      return;
    }

  } else if (provisionPhase == PHASE_DISCOVERING) {
    // Stop button (20, 160, 130, 50)
    if (x >= 20 && x <= 150 && y >= 160 && y <= 210) {
      // Send reset to clear highlight mode on all pixels
      sendReset();
      provisionPhase = PHASE_IDLE;
      drawProvisionScreen();
      return;
    }

    // Assign button (170, 160, 130, 50)
    if (x >= 170 && x <= 300 && y >= 160 && y <= 210 && discoveredCount > 0) {
      // Sort discovered pixels by current ID (low to high, treating unprovisioned as 0)
      // Simple bubble sort since we only have max 24 items
      for (uint8_t i = 0; i < discoveredCount - 1; i++) {
        for (uint8_t j = 0; j < discoveredCount - i - 1; j++) {
          // Treat PIXEL_ID_UNPROVISIONED (255) as 0 for sorting
          uint8_t id1 = (discoveredIds[j] == PIXEL_ID_UNPROVISIONED) ? 0 : discoveredIds[j];
          uint8_t id2 = (discoveredIds[j + 1] == PIXEL_ID_UNPROVISIONED) ? 0 : discoveredIds[j + 1];

          if (id1 > id2) {
            // Swap MACs
            uint8_t tempMac[6];
            memcpy(tempMac, discoveredMacs[j], 6);
            memcpy(discoveredMacs[j], discoveredMacs[j + 1], 6);
            memcpy(discoveredMacs[j + 1], tempMac, 6);

            // Swap IDs
            uint8_t tempId = discoveredIds[j];
            discoveredIds[j] = discoveredIds[j + 1];
            discoveredIds[j + 1] = tempId;
          }
        }
      }
      Serial.println("Sorted discovered pixels by ID");

      provisionPhase = PHASE_ASSIGNING;
      selectedMacIndex = 0;
      nextIdToAssign = 0;
      // Initialize all pixels: send IDLE to all, then SELECTED to the first
      for (uint8_t i = 0; i < discoveredCount; i++) {
        sendHighlightCommand(discoveredMacs[i], HIGHLIGHT_IDLE);
        delay(5);  // Small delay to avoid flooding
      }
      // Highlight the selected pixel
      sendHighlightCommand(discoveredMacs[selectedMacIndex], HIGHLIGHT_SELECTED);
      drawProvisionScreen();
      return;
    }

  } else if (provisionPhase == PHASE_ASSIGNING) {
    // ID decrement button (130, 92, 30, 25)
    if (x >= 130 && x <= 160 && y >= 92 && y <= 117) {
      if (nextIdToAssign > 0) {
        nextIdToAssign--;
        drawProvisionScreen();
      }
      return;
    }

    // ID increment button (165, 92, 30, 25)
    if (x >= 165 && x <= 195 && y >= 92 && y <= 117) {
      if (nextIdToAssign < 23) {
        nextIdToAssign++;
        drawProvisionScreen();
      }
      return;
    }

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

    // Assign button (160, 140, 150, 35)
    if (x >= 160 && x <= 310 && y >= 140 && y <= 175) {
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

    // Back button (10, 190, 80, 35)
    if (x >= 10 && x <= 90 && y >= 190 && y <= 225) {
      // Send reset to clear highlight modes, then send discovery found to all
      sendReset();
      delay(50);  // Give pixels time to process reset
      // Re-show "!" on all discovered pixels
      sendHighlightToAll(HIGHLIGHT_DISCOVERY_FOUND);
      provisionPhase = PHASE_DISCOVERING;
      drawProvisionScreen();
      return;
    }

    // Done button (230, 190, 80, 35)
    if (x >= 230 && x <= 310 && y >= 190 && y <= 225) {
      // Send reset to clear all highlight modes
      sendReset();
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

  // Show pending digit entry and last sent
  tft.setTextSize(2);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setCursor(10, 28);
  tft.print("Number: ");

  // Show pending first digit (or underscore)
  if (pendingCount == 0) {
    tft.print("_");
  } else if (pendingDigits[0] == 10) {
    tft.print(":");
  } else if (pendingDigits[0] == 11) {
    tft.print(" ");
  } else {
    tft.print(pendingDigits[0]);
  }

  tft.print(" ");

  // Show pending second digit (or underscore)
  if (pendingCount < 2) {
    tft.print("_");
  } else if (pendingDigits[1] == 10) {
    tft.print(":");
  } else if (pendingDigits[1] == 11) {
    tft.print(" ");
  } else {
    tft.print(pendingDigits[1]);
  }

  // Show last sent number
  tft.setTextColor(TFT_CYAN, COLOR_BG);
  tft.setCursor(180, 28);
  tft.print("Last: ");
  if (lastSentLeft == 10) {
    tft.print(":");
  } else if (lastSentLeft == 11) {
    tft.print(" ");
  } else {
    tft.print(lastSentLeft);
  }
  tft.print(" ");
  if (lastSentRight == 10) {
    tft.print(":");
  } else if (lastSentRight == 11) {
    tft.print(" ");
  } else {
    tft.print(lastSentRight);
  }

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
  // Helper function to add a digit and maybe send
  auto addDigit = [](uint8_t digit) {
    pendingDigits[pendingCount] = digit;
    pendingCount++;

    if (pendingCount == 2) {
      // Send the two-digit pattern
      sendTwoDigitPattern(pendingDigits[0], pendingDigits[1]);
      lastSentLeft = pendingDigits[0];
      lastSentRight = pendingDigits[1];
      // Reset for next entry
      pendingDigits[0] = 255;
      pendingDigits[1] = 255;
      pendingCount = 0;
    }

    drawDigitsScreen();
  };

  // Check digit buttons (0-4, top row)
  if (y >= 45 && y <= 85) {
    for (int i = 0; i <= 4; i++) {
      int buttonX = 10 + i * 60;
      if (x >= buttonX && x <= buttonX + 50) {
        addDigit(i);
        return;
      }
    }
  }

  // Check digit buttons (5-9, bottom row)
  if (y >= 95 && y <= 135) {
    for (int i = 5; i <= 9; i++) {
      int buttonX = 10 + (i - 5) * 60;
      if (x >= buttonX && x <= buttonX + 50) {
        addDigit(i);
        return;
      }
    }
  }

  // Check special character buttons
  if (y >= 145 && y <= 185) {
    // Colon button
    if (x >= 10 && x <= 60) {
      addDigit(10);  // 10 = colon
      return;
    }
    // Space button
    if (x >= 70 && x <= 120) {
      addDigit(11);  // 11 = space
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
        // Clear any pending manual entry
        pendingDigits[0] = 255;
        pendingDigits[1] = 255;
        pendingCount = 0;
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

  // Set left digit (digit 1) pattern (with right-align for "1")
  DigitPattern& leftPattern = digitPatterns[leftDigit];
  DigitPattern& spacePattern = digitPatterns[11];  // Space pattern for right-aligning "1"
  for (int i = 0; i < 6; i++) {
    uint8_t pixelId = digit1PixelIds[i];

    // Random directions for each hand
    RotationDirection dir1 = (random(2) == 0) ? DIR_CW : DIR_CCW;
    RotationDirection dir2 = (random(2) == 0) ? DIR_CW : DIR_CCW;
    RotationDirection dir3 = (random(2) == 0) ? DIR_CW : DIR_CCW;

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
        packet.angleCmd.setPixelStyle(pixelId, currentDigitColor, spacePattern.opacity[i]);
      } else {
        // Column 1: use column 0 from "1" pattern (remap indices)
        // i=1 → use pattern[0], i=3 → use pattern[2], i=5 → use pattern[4]
        uint8_t sourceIdx = i - 1;  // Map column 1 to column 0 of source pattern
        packet.angleCmd.setPixelAngles(pixelId,
          leftPattern.angles[sourceIdx][0],
          leftPattern.angles[sourceIdx][1],
          leftPattern.angles[sourceIdx][2],
          dir1, dir2, dir3);
        packet.angleCmd.setPixelStyle(pixelId, currentDigitColor, leftPattern.opacity[sourceIdx]);
      }
    } else {
      // Other digits: use pattern as-is
      packet.angleCmd.setPixelAngles(pixelId,
        leftPattern.angles[i][0],
        leftPattern.angles[i][1],
        leftPattern.angles[i][2],
        dir1, dir2, dir3);
      packet.angleCmd.setPixelStyle(pixelId, currentDigitColor, leftPattern.opacity[i]);
    }
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

// Send reset command to all pixels (clears special modes)
// Sends multiple times to ensure all pixels receive it
void sendReset() {
  ESPNowPacket packet;
  packet.command = CMD_RESET;

  Serial.println("Sending reset to all pixels (3x for reliability)...");

  // Send reset 3 times with delays to ensure all pixels receive it
  for (int i = 0; i < 3; i++) {
    if (ESPNowComm::sendPacket(&packet, sizeof(CommandType))) {
      Serial.print("Reset sent (attempt ");
      Serial.print(i + 1);
      Serial.println("/3)");
    } else {
      Serial.print("Failed to send reset (attempt ");
      Serial.print(i + 1);
      Serial.println("/3)");
    }
    if (i < 2) delay(50);  // 50ms delay between sends
  }

  Serial.println("Reset sequence complete");
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
      otaScreenNeedsRedraw = true;
    }
  }
}

// Initialize WiFi AP for OTA (dev machine will serve firmware)
void initOTAServer() {
  if (otaPhase != OTA_IDLE) return;

  Serial.println("OTA: Starting WiFi AP...");

  // Start WiFi in AP+STA mode (allows ESP-NOW to continue working)
  WiFi.mode(WIFI_AP_STA);

  // IMPORTANT: Start AP on same channel as ESP-NOW (channel 1)
  // Parameters: ssid, password, channel, ssid_hidden, max_connection
  WiFi.softAP(OTA_AP_SSID, OTA_AP_PASSWORD, ESPNOW_CHANNEL, 0, 30);

  IPAddress apIP = WiFi.softAPIP();
  Serial.print("OTA: AP started on channel ");
  Serial.println(ESPNOW_CHANNEL);
  Serial.print("OTA: AP IP: ");
  Serial.println(apIP);
  Serial.print("OTA: Dev server URL: http://");
  Serial.print(OTA_DEV_SERVER_IP);
  Serial.print(":");
  Serial.print(OTA_DEV_SERVER_PORT);
  Serial.println("/firmware.bin");
  Serial.println();
  Serial.println("=== OTA SETUP INSTRUCTIONS ===");
  Serial.println("1. Connect dev machine to WiFi AP:");
  Serial.print("   SSID: ");
  Serial.println(OTA_AP_SSID);
  Serial.print("   Password: ");
  Serial.println(OTA_AP_PASSWORD);
  Serial.println("2. Run on dev machine: npm run ota:server");
  Serial.println("3. Tap 'Send Update' on master screen");
  Serial.println("===============================");

  // Set placeholder firmware size (actual size from HTTP headers)
  firmwareSize = 1000000;

  otaPhase = OTA_READY;

  // ESP-NOW remains active in AP_STA mode on the same channel
  Serial.println("OTA: ESP-NOW remains active in AP+STA mode");
}

// Stop OTA WiFi AP and return to normal operation
void stopOTAServer() {
  if (otaPhase == OTA_IDLE) return;

  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  otaPhase = OTA_IDLE;

  // Re-initialize ESP-NOW
  ESPNowComm::initSender(ESPNOW_CHANNEL);
  ESPNowComm::setReceiveCallback(onMasterPacketReceived);

  Serial.println("OTA: WiFi AP stopped");
}

// Send OTA start command to all selected pixels
void sendOTAUpdate() {
  // Count selected pixels
  int selectedCount = 0;
  for (int i = 0; i < MAX_PIXELS; i++) {
    if (otaPixelSelected[i]) selectedCount++;
  }

  if (selectedCount == 0) {
    Serial.println("OTA: No pixels selected for update");
    return;
  }

  Serial.print("OTA: Sending updates to ");
  Serial.print(selectedCount);
  Serial.println(" selected pixel(s)");

  // Loop through selected pixels and send individual OTA commands
  for (int i = 0; i < MAX_PIXELS; i++) {
    if (!otaPixelSelected[i]) continue;

    // Clear status for this pixel
    otaPixelStatus[i] = OTA_STATUS_IDLE;
    otaPixelProgress[i] = 0;

    ESPNowPacket packet;
    packet.otaStart.command = CMD_OTA_START;
    packet.otaStart.targetPixelId = i;

    // Copy WiFi credentials
    strncpy(packet.otaStart.ssid, OTA_AP_SSID, 31);
    packet.otaStart.ssid[31] = '\0';
    strncpy(packet.otaStart.password, OTA_AP_PASSWORD, 31);
    packet.otaStart.password[31] = '\0';

    // Build firmware URL (dev machine OTA server)
    snprintf(packet.otaStart.firmwareUrl, 127, "http://%s:%d/firmware.bin",
             OTA_DEV_SERVER_IP, OTA_DEV_SERVER_PORT);
    packet.otaStart.firmwareUrl[127] = '\0';

    packet.otaStart.firmwareSize = firmwareSize;
    packet.otaStart.firmwareCrc32 = 0;  // Skip CRC check for now

    Serial.print("OTA: Sending START to pixel ");
    Serial.println(i);

    if (ESPNowComm::sendPacket(&packet, sizeof(OTAStartPacket))) {
      Serial.print("OTA: Update sent to pixel ");
      Serial.println(i);

      // Mark as updated and deselect
      otaPixelUpdated[i] = true;
      otaPixelSelected[i] = false;
    } else {
      Serial.print("OTA: Failed to send update to pixel ");
      Serial.println(i);
    }

    // Small delay between sends to avoid flooding
    delay(50);
  }

  Serial.println("OTA: All selected pixels updated");

  // Redraw screen to show green (updated) pixels
  drawOTAScreen();
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
    tft.println("Update pixel firmware wirelessly");
    tft.println();
    tft.setCursor(10, 60);
    tft.setTextColor(TFT_YELLOW, COLOR_BG);
    tft.println("Workflow:");
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.setCursor(10, 75);
    tft.println("1. Tap 'Start Server' below");
    tft.setCursor(10, 90);
    tft.println("2. Connect dev PC to WiFi AP");
    tft.setCursor(10, 105);
    tft.println("3. Run: npm run ota:server");
    tft.setCursor(10, 120);
    tft.println("4. Tap 'Send Update'");

    // Start Server button
    tft.fillRoundRect(60, 145, 200, 45, 8, TFT_DARKGREEN);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREEN);
    tft.setTextSize(2);
    tft.setCursor(80, 158);
    tft.println("Start Server");

    // Back button
    tft.fillRoundRect(110, 200, 100, 30, 8, TFT_DARKGREY);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
    tft.setTextSize(1);
    tft.setCursor(140, 208);
    tft.println("Back");

  } else if (otaPhase == OTA_READY) {
    tft.setTextColor(TFT_GREEN, COLOR_BG);
    tft.setTextSize(2);
    tft.setCursor(40, 30);
    tft.println("WiFi AP Ready!");

    // Condensed WiFi credentials and instructions
    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.setCursor(10, 55);
    tft.print("WiFi: ");
    tft.setTextColor(TFT_CYAN, COLOR_BG);
    tft.print(OTA_AP_SSID);
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.print(" / ");
    tft.setTextColor(TFT_CYAN, COLOR_BG);
    tft.println(OTA_AP_PASSWORD);

    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.setCursor(10, 70);
    tft.println("Run: npm run ota:server");

    tft.setCursor(10, 85);
    tft.setTextColor(TFT_YELLOW, COLOR_BG);
    tft.println("Select pixels, then tap Send Update");

    // Draw pixel grid (6 columns × 4 rows)
    int cellW = 50;
    int cellH = 22;
    int startX = 5;
    int startY = 105;
    int cols = 6;

    for (int i = 0; i < MAX_PIXELS; i++) {
      int col = i % cols;
      int row = i / cols;
      int x = startX + col * cellW;
      int y = startY + row * cellH;

      // Determine cell color based on state
      uint16_t bgColor = TFT_BLACK;
      uint16_t borderColor = TFT_DARKGREY;

      if (otaPixelUpdated[i]) {
        // Green for already updated pixels
        bgColor = TFT_DARKGREEN;
        borderColor = TFT_GREEN;
      } else if (otaPixelSelected[i]) {
        // Blue for selected pixels (pending update)
        bgColor = TFT_DARKBLUE;
        borderColor = TFT_BLUE;
      }

      // Draw cell
      tft.fillRoundRect(x, y, cellW - 2, cellH - 2, 3, bgColor);
      tft.drawRoundRect(x, y, cellW - 2, cellH - 2, 3, borderColor);

      // Draw pixel ID
      tft.setTextColor(TFT_WHITE, bgColor);
      tft.setTextSize(1);
      tft.setCursor(x + (i < 10 ? 20 : 16), y + 7);
      tft.print(i);
    }

    // "Send Update" button (bottom left)
    tft.fillRoundRect(10, 195, 120, 30, 4, TFT_DARKGREEN);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREEN);
    tft.setTextSize(2);
    tft.setCursor(30, 200);
    tft.println("Send");

    // "Clear All" button (middle)
    tft.fillRoundRect(140, 195, 90, 30, 4, TFT_ORANGE);
    tft.setTextColor(TFT_WHITE, TFT_ORANGE);
    tft.setTextSize(1);
    tft.setCursor(155, 205);
    tft.println("Clear All");

    // "Back" button (bottom right)
    tft.fillRoundRect(240, 195, 70, 30, 4, TFT_DARKGREY);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
    tft.setTextSize(1);
    tft.setCursor(260, 205);
    tft.println("Back");

  } else if (otaPhase == OTA_IN_PROGRESS) {
    // Note: This phase is not normally used in the multi-select workflow
    // Kept for potential future use or error handling
    tft.setTextColor(TFT_YELLOW, COLOR_BG);
    tft.setTextSize(3);
    tft.setCursor(10, 50);
    tft.println("UPDATING...");

    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXT, COLOR_BG);

    tft.setCursor(10, 100);
    tft.println("Pixels updating...");

    tft.setCursor(10, 120);
    tft.setTextColor(TFT_CYAN, COLOR_BG);
    tft.println("Check progress:");

    tft.setCursor(10, 140);
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.println("- Dev server terminal");
    tft.setCursor(10, 155);
    tft.println("- Pixel screens");

    tft.setCursor(10, 180);
    tft.setTextColor(TFT_DARKGREY, COLOR_BG);
    tft.println("Takes ~15-20 seconds...");

    // Done button (available to exit early)
    tft.fillRoundRect(110, 200, 100, 30, 8, TFT_DARKGREY);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
    tft.setTextSize(1);
    tft.setCursor(140, 208);
    tft.println("Done");

  } else if (otaPhase == OTA_COMPLETE) {
    // Note: This phase is not normally used in the multi-select workflow
    // Kept for potential future use or error handling
    tft.setTextColor(TFT_GREEN, COLOR_BG);
    tft.setTextSize(3);
    tft.setCursor(30, 60);
    tft.println("COMPLETE!");

    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXT, COLOR_BG);

    tft.setCursor(10, 110);
    tft.println("Updates sent to pixels");

    tft.setCursor(10, 135);
    tft.setTextColor(TFT_CYAN, COLOR_BG);
    tft.println("Check pixel screens to verify");
    tft.setCursor(10, 150);
    tft.println("successful updates");

    tft.setCursor(10, 175);
    tft.setTextColor(TFT_DARKGREY, COLOR_BG);
    tft.println("Tap 'Done' to return to menu");

    // Done button
    tft.fillRoundRect(85, 195, 150, 35, 8, TFT_BLUE);
    tft.setTextColor(TFT_WHITE, TFT_BLUE);
    tft.setTextSize(2);
    tft.setCursor(125, 205);
    tft.println("Done");
  }
}

// Handle touch in OTA mode
void handleOTATouch(uint16_t x, uint16_t y) {
  if (otaPhase == OTA_IDLE) {
    // Start Server button (60, 145, 200, 45)
    if (x >= 60 && x <= 260 && y >= 145 && y <= 190) {
      initOTAServer();
      drawOTAScreen();
      return;
    }
    // Back button (110, 200, 100, 30)
    if (x >= 110 && x <= 210 && y >= 200 && y <= 230) {
      currentMode = MODE_MENU;
      drawMenu();
      return;
    }

  } else if (otaPhase == OTA_READY) {
    // Pixel grid cells (5, 105) to (305, 193)
    if (x >= 5 && x <= 305 && y >= 105 && y <= 193) {
      int cellW = 50;
      int cellH = 22;
      int startX = 5;
      int startY = 105;
      int cols = 6;

      int col = (x - startX) / cellW;
      int row = (y - startY) / cellH;

      if (col >= 0 && col < cols && row >= 0 && row < 4) {
        int pixelId = row * cols + col;
        if (pixelId < MAX_PIXELS) {
          // Toggle selection (if not already updated)
          if (otaPixelUpdated[pixelId]) {
            // Tapping an updated pixel clears it back to unselected
            otaPixelUpdated[pixelId] = false;
          } else {
            // Toggle selection
            otaPixelSelected[pixelId] = !otaPixelSelected[pixelId];
          }
          drawOTAScreen();
        }
      }
      return;
    }

    // "Send Update" button (10, 195, 120, 30)
    if (x >= 10 && x <= 130 && y >= 195 && y <= 225) {
      sendOTAUpdate();
      // sendOTAUpdate will redraw the screen
      return;
    }

    // "Clear All" button (140, 195, 90, 30)
    if (x >= 140 && x <= 230 && y >= 195 && y <= 225) {
      // Clear all selections and updated states
      for (int i = 0; i < MAX_PIXELS; i++) {
        otaPixelSelected[i] = false;
        otaPixelUpdated[i] = false;
      }
      drawOTAScreen();
      return;
    }

    // "Back" button (240, 195, 70, 30)
    if (x >= 240 && x <= 310 && y >= 195 && y <= 225) {
      stopOTAServer();
      currentMode = MODE_MENU;
      drawMenu();
      return;
    }

  } else if (otaPhase == OTA_IN_PROGRESS) {
    // Done button (110, 200, 100, 30)
    if (x >= 110 && x <= 210 && y >= 200 && y <= 230) {
      stopOTAServer();
      currentMode = MODE_MENU;
      drawMenu();
      return;
    }

  } else if (otaPhase == OTA_COMPLETE) {
    // Done button (85, 195, 150, 35)
    if (x >= 85 && x <= 235 && y >= 195 && y <= 230) {
      stopOTAServer();
      currentMode = MODE_MENU;
      drawMenu();
      return;
    }
  }
}

// ===== VERSION FUNCTIONS =====

// Handle version response from pixel
void handleVersionResponse(const VersionResponsePacket& resp) {
  if (resp.pixelId < MAX_PIXELS) {
    pixelVersions[resp.pixelId].received = true;
    pixelVersions[resp.pixelId].major = resp.versionMajor;
    pixelVersions[resp.pixelId].minor = resp.versionMinor;

    Serial.print("Version response from pixel ");
    Serial.print(resp.pixelId);
    Serial.print(": v");
    Serial.print(resp.versionMajor);
    Serial.print(".");
    Serial.println(resp.versionMinor);

    // Set flag to redraw from main loop (don't draw from callback/interrupt)
    if (currentMode == MODE_VERSION) {
      versionScreenNeedsRedraw = true;
    }
  }
}

// Send get version command to all pixels
void sendGetVersionCommand() {
  ESPNowPacket packet;
  packet.getVersion.command = CMD_GET_VERSION;
  packet.getVersion.displayOnScreen = true;

  if (ESPNowComm::sendPacket(&packet, sizeof(GetVersionPacket))) {
    Serial.println("Sent GET_VERSION command to all pixels");
  } else {
    Serial.println("Failed to send GET_VERSION command");
  }
}

// Draw version screen
void drawVersionScreen() {
  tft.fillScreen(COLOR_BG);

  // Title with master version
  tft.setTextColor(TFT_MAGENTA, COLOR_BG);
  tft.setTextSize(2);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("Firmware Versions", 160, 5);
  tft.setTextDatum(TL_DATUM);

  // Master version
  tft.setTextColor(TFT_WHITE, COLOR_BG);
  tft.setTextSize(1);
  tft.setCursor(10, 30);
  tft.print("Master: v");
  tft.print(FIRMWARE_VERSION_MAJOR);
  tft.print(".");
  tft.println(FIRMWARE_VERSION_MINOR);

  // Count responses
  int received = 0;
  for (int i = 0; i < MAX_PIXELS; i++) {
    if (pixelVersions[i].received) received++;
  }

  tft.setCursor(10, 45);
  tft.print("Pixels responding: ");
  tft.print(received);
  tft.print("/");
  tft.println(MAX_PIXELS);

  // Draw pixel version grid (6 columns x 4 rows)
  int startY = 65;
  int cellW = 52;
  int cellH = 35;

  for (int i = 0; i < MAX_PIXELS; i++) {
    int col = i % 6;
    int row = i / 6;
    int x = 5 + col * cellW;
    int y = startY + row * cellH;

    if (pixelVersions[i].received) {
      tft.fillRoundRect(x, y, cellW - 2, cellH - 2, 4, TFT_DARKGREEN);
      tft.setTextColor(TFT_WHITE, TFT_DARKGREEN);
    } else {
      tft.fillRoundRect(x, y, cellW - 2, cellH - 2, 4, TFT_DARKGREY);
      tft.setTextColor(TFT_LIGHTGREY, TFT_DARKGREY);
    }

    tft.setTextSize(1);
    tft.setCursor(x + 3, y + 5);
    tft.print("P");
    tft.print(i);

    if (pixelVersions[i].received) {
      tft.setCursor(x + 3, y + 18);
      tft.print("v");
      tft.print(pixelVersions[i].major);
      tft.print(".");
      tft.print(pixelVersions[i].minor);
    } else {
      tft.setCursor(x + 8, y + 18);
      tft.print("---");
    }
  }

  // Refresh button
  tft.fillRoundRect(60, 210, 90, 25, 4, TFT_BLUE);
  tft.setTextColor(TFT_WHITE, TFT_BLUE);
  tft.setTextSize(1);
  tft.setCursor(75, 215);
  tft.println("Refresh");

  // Back button
  tft.fillRoundRect(170, 210, 90, 25, 4, TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.setCursor(195, 215);
  tft.println("Back");
}

// Handle touch in version mode
void handleVersionTouch(uint16_t x, uint16_t y) {
  // Refresh button (60, 210, 90, 25)
  if (x >= 60 && x <= 150 && y >= 210 && y <= 235) {
    // Clear and re-request
    for (int i = 0; i < MAX_PIXELS; i++) {
      pixelVersions[i].received = false;
    }
    versionRequestTime = millis();
    sendGetVersionCommand();
    drawVersionScreen();
    return;
  }

  // Back button (170, 210, 90, 25)
  if (x >= 170 && x <= 260 && y >= 210 && y <= 235) {
    currentMode = MODE_MENU;
    drawMenu();
    return;
  }
}

// ===== WIFI & TIME FUNCTIONS =====

void connectWiFi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    // Configure NTP
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
    Serial.println("NTP configured, waiting for time sync...");

    // Wait for time to sync (up to 5 seconds)
    struct tm timeinfo;
    int syncAttempts = 0;
    while (!getLocalTime(&timeinfo) && syncAttempts < 10) {
      delay(500);
      syncAttempts++;
    }

    if (getLocalTime(&timeinfo)) {
      Serial.println("Time synchronized!");
      Serial.printf("Current time: %02d:%02d:%02d\n", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    } else {
      Serial.println("Failed to sync time");
    }
  } else {
    Serial.println("\nWiFi connection failed!");
  }
}

// Get current minute from real-time clock (0-59)
uint8_t getCurrentMinute() {
  if (!wifiConnected) {
    return 0;  // Return 0 if no WiFi
  }

  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    return timeinfo.tm_min;  // Returns 0-59
  }

  return 0;  // Return 0 if time not available
}

// Get current time as formatted string (e.g., "12:35 PM")
String getCurrentTimeString() {
  if (!wifiConnected) {
    return "No WiFi";
  }

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "No Time";
  }

  char timeStr[16];
  strftime(timeStr, sizeof(timeStr), "%I:%M %p", &timeinfo);
  return String(timeStr);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n========== MASTER CONTROLLER ==========");
  Serial.println("Twenty-Four Times - ESP-NOW Master");
  Serial.println(BOARD_NAME);
  Serial.println("=======================================\n");

  // Connect to WiFi and sync time
  connectWiFi();
  Serial.println();

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
          case MODE_ANIMATIONS:
            drawAnimationsScreen();
            break;
          case MODE_DIGITS:
            drawDigitsScreen();
            // Send initial pattern to clear any highlight/version modes on pixels
            sendTwoDigitPattern(11, 11);  // Send blank pattern
            lastPingTime = currentTime;  // Initialize ping timer
            break;
          case MODE_PROVISION:
            provisionPhase = PHASE_IDLE;
            drawProvisionScreen();
            break;
          case MODE_OTA:
            otaPhase = OTA_IDLE;
            // Clear all selections and updated states
            for (int i = 0; i < MAX_PIXELS; i++) {
              otaPixelSelected[i] = false;
              otaPixelUpdated[i] = false;
            }
            drawOTAScreen();
            break;
          case MODE_VERSION:
            // Clear version tracking and request versions
            for (int i = 0; i < MAX_PIXELS; i++) {
              pixelVersions[i].received = false;
            }
            versionRequestTime = currentTime;
            sendGetVersionCommand();
            drawVersionScreen();
            break;
          default:
            break;
        }
      }
    } else if (currentMode == MODE_ANIMATIONS) {
      // Animations menu has its own touch handler
      handleAnimationsTouch(tx, ty);
    } else if (currentMode == MODE_DIGITS) {
      // Digits mode has its own touch handler
      handleDigitsTouch(tx, ty);
    } else if (currentMode == MODE_PROVISION) {
      // Provision mode has its own touch handler
      handleProvisionTouch(tx, ty);
    } else if (currentMode == MODE_OTA) {
      // OTA mode has its own touch handler
      handleOTATouch(tx, ty);
    } else if (currentMode == MODE_VERSION) {
      // Version mode has its own touch handler
      handleVersionTouch(tx, ty);
    } else if (currentMode == MODE_ORBIT_TIME) {
      // Orbit Time mode has its own touch handler
      handleOrbitTimeTouch(tx, ty);
    } else {
      // Any touch in other modes returns to animations menu (for animation modes)
      if (currentMode == MODE_UNITY || currentMode == MODE_FLUID_TIME) {
        currentMode = MODE_ANIMATIONS;
        drawAnimationsScreen();
        Serial.println("Returned to animations menu");
      } else {
        // Return to main menu
        currentMode = MODE_MENU;
        drawMenu();
        Serial.println("Returned to menu");
      }
    }
  }

  // Handle mode-specific logic
  switch (currentMode) {
    case MODE_MENU:
      // Update time display every second
      if (currentTime - lastMenuTimeUpdate >= 1000) {
        lastMenuTimeUpdate = currentTime;
        // Redraw just the time at the bottom
        tft.setTextSize(1);
        tft.setTextColor(TFT_CYAN, COLOR_BG);
        tft.setTextDatum(BC_DATUM);  // Bottom center alignment
        tft.drawString(getCurrentTimeString(), 160, 235);
        tft.setTextDatum(TL_DATUM);  // Reset to top-left
      }
      break;

    case MODE_ANIMATIONS:
      // Nothing to do - waiting for touch
      break;

    case MODE_UNITY: {
      // Handle Unity animation loop
      handleUnityLoop(currentTime);
      break;
    }

    case MODE_FLUID_TIME: {
      // Handle Fluid Time animation loop
      handleFluidTimeLoop(currentTime);
      break;
    }

    case MODE_ORBIT_TIME: {
      // Handle Orbit Time animation loop
      handleOrbitTimeLoop(currentTime);
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

          // Update last sent display
          lastSentLeft = leftDigit;
          lastSentRight = rightDigit;
          drawDigitsScreen();  // Update display to show new "Last:" value

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

    case MODE_OTA: {
      // Redraw requested by ESP-NOW callback (safe to draw here in loop context)
      if (otaScreenNeedsRedraw) {
        otaScreenNeedsRedraw = false;
        drawOTAScreen();
      }

      // Simple timeout to automatically show COMPLETE screen after ~30 seconds
      // (gives pixels time to download, flash, and reboot)
      if (otaPhase == OTA_IN_PROGRESS) {
        unsigned long elapsed = currentTime - otaStartTime;

        // Auto-complete after 30 seconds (pixels should be done by then)
        if (elapsed >= 30000) {
          Serial.println();
          Serial.println("===== OTA BROADCAST COMPLETE =====");
          Serial.print("Broadcast sent to all pixels");
          Serial.print("Total time: ");
          Serial.print((millis() - otaStartTime) / 1000);
          Serial.println(" seconds");
          Serial.println("==================================");

          otaPhase = OTA_COMPLETE;
          drawOTAScreen();
        }
      }
      break;
    }

    case MODE_VERSION: {
      // Check if version screen needs redraw (from callback)
      if (versionScreenNeedsRedraw) {
        versionScreenNeedsRedraw = false;
        drawVersionScreen();
      }
      break;
    }
  }

  // Small delay to avoid busy-waiting
  delay(10);
}

