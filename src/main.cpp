#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_GC9A01A.h>
#include <ESPNowComm.h>
#include <Preferences.h>
#include <HTTPUpdate.h>
#include <WiFiClient.h>
#include <esp_now.h>
#include <esp_wifi.h>

// Proof of concept: Three rotating clock hands on a 240x240 circular display
// Based on the twenty-four-times simulation
// Now with ESP-NOW communication for synchronized multi-pixel operation

// ===== FIRMWARE VERSION =====
#define FIRMWARE_VERSION_MAJOR 1
#define FIRMWARE_VERSION_MINOR 38

// ===== PIXEL CONFIGURATION =====
// Pixel ID is loaded from NVS (non-volatile storage) on startup.
// Use CMD_SET_PIXEL_ID command from master to provision each pixel.
// Value of 255 (PIXEL_ID_UNPROVISIONED) indicates unprovisionied state.

// NVS storage
Preferences preferences;
const char* NVS_NAMESPACE = "pixel";
const char* NVS_KEY_PIXEL_ID = "id";

// Pixel ID (loaded from NVS in setup, or PIXEL_ID_UNPROVISIONED if not set)
uint8_t pixelId = PIXEL_ID_UNPROVISIONED;

// 240x240 RGB565 buffer (~115 KB) - allocated in setup() to avoid boot crash
GFXcanvas16* canvas = nullptr;

// ===== BOARD-SPECIFIC PIN CONFIGURATION =====
#if defined(CONFIG_IDF_TARGET_ESP32C3) || defined(ARDUINO_XIAO_ESP32C3)
  // ----- XIAO ESP32-C3 -----
  // Uses software SPI on custom pins (initialized manually in setup)
  // Performance: ~30 FPS
  #define BOARD_NAME "XIAO ESP32-C3"
  #define tft_rst  4   // D2 / GPIO4 / pin 3
  #define tft_cs   5   // D3 / GPIO5 / pin 4
  #define tft_dc   6   // D4 / GPIO6 / pin 5
  #define tft_scl  8   // D8 / GPIO8 / pin 9 (strapping pin - safe for SPI CLK)
  #define tft_sda  10  // D10 / GPIO10 / pin 11

  // 3-parameter constructor - SPI pins set manually in setup()
  Adafruit_GC9A01A tft(tft_cs, tft_dc, tft_rst);

#elif defined(CONFIG_IDF_TARGET_ESP32S3) || defined(ARDUINO_ESP32S3_DEV)
  // ----- ESP32-S3-Zero (Waveshare) -----
  // Uses HARDWARE SPI2 (FSPI) with DEFAULT pins
  // ESP32-S3 default FSPI: MOSI=11, MISO=13, CLK=12, CS=10
  // We'll use CLK=12 (default) instead of 13 to avoid remapping issues
  #define BOARD_NAME "ESP32-S3-Zero"
  #define tft_rst  4   // GPIO4 / GP4 / left header pin 7 (any GPIO)
  #define tft_cs   10  // GPIO10 / GP10 / right header pin 13 - FSPI_CS (default)
  #define tft_dc   6   // GPIO6 / GP6 / left header pin 9 (any GPIO)
  #define tft_scl  12  // GPIO12 / GP12 / right header pin 15 - FSPI_CLK (default!)
  #define tft_sda  11  // GPIO11 / GP11 / right header pin 14 - FSPI_MOSI (default)

  #define USE_HARDWARE_SPI 1

  // 3-parameter constructor for hardware SPI - uses default SPI pins
  Adafruit_GC9A01A tft(tft_cs, tft_dc, tft_rst);

#else
  #error "Unsupported board! Please use ESP32-C3 or ESP32-S3."
#endif

// ---- Display geometry ----
const int DISPLAY_WIDTH = 240;
const int DISPLAY_HEIGHT = 240;
const int CENTER_X = 120;
const int CENTER_Y = 120;

// Maximum visible radius (adjustable if needed to account for bezel)
const int MAX_RADIUS = 120;

// Hand parameters
// Normal hands: 92% of max radius
// Thin hand (3rd hand): 80% thickness of normal
const float HAND_LENGTH_NORMAL = MAX_RADIUS * 0.92;
const float HAND_THICKNESS_NORMAL = 13.0;
const float HAND_THICKNESS_THIN = 9;  // 80% of normal

// ---- Transition/Easing Types ----
// Use TransitionType from ESPNowComm.h (shared between master and pixels)

// ---- Hand State ----
struct HandState {
  float currentAngle;
  float targetAngle;
  float startAngle;
  int direction;  // 1 for CW, -1 for CCW
};

HandState hand1 = {0.0, 0.0, 0.0, 1};
HandState hand2 = {0.0, 0.0, 0.0, 1};
HandState hand3 = {0.0, 0.0, 0.0, 1};

// ---- Opacity State (shared by all hands) ----
struct OpacityState {
  uint8_t current;
  uint8_t target;
  uint8_t start;
};

OpacityState opacity = {0, 0, 0};  // Start invisible

// ---- Color State (foreground and background) ----
struct ColorState {
  uint16_t currentBg;
  uint16_t targetBg;
  uint16_t startBg;
  uint16_t currentFg;
  uint16_t targetFg;
  uint16_t startFg;
};

// Start with black background, white foreground
ColorState colors = {
  GC9A01A_BLACK, GC9A01A_BLACK, GC9A01A_BLACK,
  GC9A01A_WHITE, GC9A01A_WHITE, GC9A01A_WHITE
};

// ---- Transition State (shared by all hands) ----
struct TransitionState {
  unsigned long startTime;
  float duration;  // in seconds
  TransitionType easing;
  bool isActive;
};

TransitionState transition = {0, 0.0, TRANSITION_ELASTIC, false};

// Timing
unsigned long lastUpdateTime = 0;

// ===== ESP-NOW STATE =====
bool espnowEnabled = false;  // Set to true when ESP-NOW is initialized
bool errorState = false;     // If true, display error screen (red bg with "!")
unsigned long lastPacketTime = 0;
const unsigned long PACKET_TIMEOUT = 10000;  // 10 seconds without packet = show error

// ===== OTA UPDATE STATE =====
bool otaInProgress = false;        // True when performing OTA update
OTAStatus currentOTAStatus = OTA_STATUS_IDLE;
uint8_t currentOTAProgress = 0;    // 0-100

// OTA requests are received via ESP-NOW callback (WiFi task context).
// IMPORTANT: Do NOT run WiFi.mode()/scan/begin/update inside the receive callback.
// Instead, latch the request and perform OTA from loop() context.
static portMUX_TYPE otaRequestMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool otaRequestPending = false;
OTAStartPacket otaPendingStart;  // Changed from OTANotifyPacket to OTAStartPacket

// Forward declarations for OTA
void sendOTAAck(OTAStatus status, uint8_t progress, uint16_t errorCode = 0);
void performOTAUpdate(const OTAStartPacket& start);

// FPS tracking
unsigned long fpsLastTime = 0;
unsigned long fpsFrames = 0;

// ---- Color Palette ----
// Each palette entry has {background, foreground} with good contrast
struct ColorPair {
  uint16_t bg;
  uint16_t fg;
  const char* name;
};

const ColorPair colorPalette[] = {
  // Classic high contrast
  {GC9A01A_BLACK, GC9A01A_WHITE, "White on Black"},
  {GC9A01A_WHITE, GC9A01A_BLACK, "Black on White"},

  // Earthy tones
  {tft.color565(245, 235, 220), tft.color565(101, 67, 33), "Dark Brown on Cream"},  // Cream bg, dark brown fg
  {tft.color565(101, 67, 33), tft.color565(245, 235, 220), "Cream on Dark Brown"},  // Dark brown bg, cream fg
  {tft.color565(47, 79, 79), tft.color565(245, 222, 179), "Wheat on Dark Slate"},   // Dark slate bg, wheat fg
  {tft.color565(245, 222, 179), tft.color565(47, 79, 79), "Dark Slate on Wheat"},   // Wheat bg, dark slate fg
  {tft.color565(139, 69, 19), tft.color565(255, 248, 220), "Cornsilk on Saddle Brown"},  // Saddle brown bg, cornsilk fg
  {tft.color565(34, 49, 63), tft.color565(236, 240, 241), "Light Gray on Navy"},    // Navy bg, light gray fg

  // Bright vibrant colors
  {tft.color565(255, 69, 0), tft.color565(255, 255, 224), "Light Yellow on Red-Orange"},     // Red-orange bg, light yellow fg
  {tft.color565(255, 215, 0), tft.color565(139, 0, 139), "Dark Magenta on Gold"},            // Gold bg, dark magenta fg
  {tft.color565(0, 191, 255), tft.color565(255, 255, 255), "White on Deep Sky Blue"},        // Deep sky blue bg, white fg
  {tft.color565(255, 20, 147), tft.color565(255, 255, 240), "Ivory on Deep Pink"},           // Deep pink bg, ivory fg
  {tft.color565(50, 205, 50), tft.color565(25, 25, 112), "Midnight Blue on Lime Green"},     // Lime green bg, midnight blue fg
  {tft.color565(138, 43, 226), tft.color565(255, 250, 205), "Lemon Chiffon on Blue Violet"}, // Blue violet bg, lemon chiffon fg
  {tft.color565(255, 140, 0), tft.color565(25, 25, 112), "Midnight Blue on Dark Orange"},    // Dark orange bg, midnight blue fg
  {tft.color565(0, 206, 209), tft.color565(139, 0, 0), "Dark Red on Turquoise"},             // Turquoise bg, dark red fg
};

const int paletteSize = sizeof(colorPalette) / sizeof(ColorPair);

// ---- Helper Functions ----

// Get easing name for debug output (use library function)
// This is just a wrapper for compatibility
const char* getEasingName(TransitionType easing) {
  return getTransitionName(easing);
}

// ---- Easing Functions ----
// All easing functions take t in range [0, 1] and return eased value in range [0, 1]

float easeLinear(float t) {
  return t;
}

float easeInOut(float t) {
  // Smoothstep (S-curve)
  return t < 0.5 ? 2 * t * t : 1 - pow(-2 * t + 2, 2) / 2;
}

float easeElasticOut(float t) {
  if (t == 0 || t == 1) return t;
  const float c4 = (2 * PI) / 3;
  return pow(2, -10 * t) * sin((t * 10 - 0.75) * c4) + 1;
}

float easeBounceOut(float t) {
  // Robert Penner's bounce ease out
  if (t < (1.0 / 2.75)) {
    return 7.5625 * t * t;
  } else if (t < (2.0 / 2.75)) {
    t -= (1.5 / 2.75);
    return 7.5625 * t * t + 0.75;
  } else if (t < (2.5 / 2.75)) {
    t -= (2.25 / 2.75);
    return 7.5625 * t * t + 0.9375;
  } else {
    t -= (2.625 / 2.75);
    return 7.5625 * t * t + 0.984375;
  }
}

float easeBackIn(float t) {
  // Robert Penner's back ease in
  const float c1 = 1.70158;
  const float c3 = c1 + 1;
  return c3 * t * t * t - c1 * t * t;
}

float easeBackOut(float t) {
  // Robert Penner's back ease out
  const float c1 = 1.70158;
  const float c3 = c1 + 1;
  return 1 + c3 * pow(t - 1, 3) + c1 * pow(t - 1, 2);
}

float easeBackInOut(float t) {
  // Robert Penner's back ease in-out
  const float c1 = 1.70158 * 1.525;
  return t < 0.5
    ? (pow(2 * t, 2) * ((c1 + 1) * 2 * t - c1)) / 2
    : (pow(2 * t - 2, 2) * ((c1 + 1) * (t * 2 - 2) + c1) + 2) / 2;
}

// Apply the current easing function
float applyEasing(float t, TransitionType easing) {
  switch (easing) {
    case TRANSITION_LINEAR: return easeLinear(t);
    case TRANSITION_EASE_IN_OUT: return easeInOut(t);
    case TRANSITION_ELASTIC: return easeElasticOut(t);
    case TRANSITION_BOUNCE: return easeBounceOut(t);
    case TRANSITION_BACK_IN: return easeBackIn(t);
    case TRANSITION_BACK_OUT: return easeBackOut(t);
    case TRANSITION_BACK_IN_OUT: return easeBackInOut(t);
    case TRANSITION_INSTANT: return 1.0;  // Jump immediately to target
    default: return t;
  }
}

// ---- Transition Control Functions ----

// Start a transition for all hands (synchronized)
// All hands transition together with shared opacity and colors
// durationSeconds: transition duration in seconds
// easing: easing type to use (for angles; opacity and colors always use ease-in-out)
void startTransition(float target1, float target2, float target3,
                     uint8_t targetOpacity,
                     uint16_t targetBg, uint16_t targetFg,
                     float durationSeconds, TransitionType easing,
                     int8_t dir1, int8_t dir2, int8_t dir3) {
  // Set up transition state
  transition.startTime = millis();
  transition.duration = durationSeconds;
  transition.easing = easing;
  transition.isActive = true;

  // Set up shared opacity
  opacity.start = opacity.current;
  opacity.target = targetOpacity;

  // Set up color transitions
  colors.startBg = colors.currentBg;
  colors.targetBg = targetBg;
  colors.startFg = colors.currentFg;
  colors.targetFg = targetFg;

  // Normalize current angles to 0-360 range before starting new transition
  while (hand1.currentAngle < 0) hand1.currentAngle += 360.0;
  while (hand1.currentAngle >= 360.0) hand1.currentAngle -= 360.0;
  while (hand2.currentAngle < 0) hand2.currentAngle += 360.0;
  while (hand2.currentAngle >= 360.0) hand2.currentAngle -= 360.0;
  while (hand3.currentAngle < 0) hand3.currentAngle += 360.0;
  while (hand3.currentAngle >= 360.0) hand3.currentAngle -= 360.0;

  // Set up hand 1
  hand1.startAngle = hand1.currentAngle;
  hand1.targetAngle = target1;
  hand1.direction = dir1;
  // If start == target (accounting for 360° wrap), do a full 360° rotation
  float diff1 = hand1.currentAngle - target1;
  while (diff1 > 180.0) diff1 -= 360.0;
  while (diff1 < -180.0) diff1 += 360.0;
  if (abs(diff1) < 0.1) {
    hand1.targetAngle = hand1.currentAngle + (360.0 * hand1.direction);
  }

  // Set up hand 2
  hand2.startAngle = hand2.currentAngle;
  hand2.targetAngle = target2;
  hand2.direction = dir2;
  float diff2 = hand2.currentAngle - target2;
  while (diff2 > 180.0) diff2 -= 360.0;
  while (diff2 < -180.0) diff2 += 360.0;
  if (abs(diff2) < 0.1) {
    hand2.targetAngle = hand2.currentAngle + (360.0 * hand2.direction);
  }

  // Set up hand 3
  hand3.startAngle = hand3.currentAngle;
  hand3.targetAngle = target3;
  hand3.direction = dir3;
  float diff3 = hand3.currentAngle - target3;
  while (diff3 > 180.0) diff3 -= 360.0;
  while (diff3 < -180.0) diff3 += 360.0;
  if (abs(diff3) < 0.1) {
    hand3.targetAngle = hand3.currentAngle + (360.0 * hand3.direction);
  }
}

// Update a hand's angle based on transition state
void updateHandAngle(HandState &hand, float t) {
  // Apply easing function for angle (uses transition easing)
  float easedT = applyEasing(t, transition.easing);

  // Calculate angle difference
  float diff = hand.targetAngle - hand.startAngle;

  // For full 360° rotations, diff is already set correctly (±360)
  // For normal transitions, apply direction consistently
  if (abs(diff) < 359.0) {  // Not a full rotation
    // Normalize diff to 0-360 range first
    while (diff < 0) diff += 360.0;
    while (diff >= 360.0) diff -= 360.0;

    // Now apply the specified direction
    if (hand.direction > 0) {
      // Clockwise (positive direction): keep diff as-is (0-360)
      // This means we always rotate CW by the calculated amount
    } else {
      // Counter-clockwise (negative direction): go the other way
      // If diff is 90, we want to go -270 (CCW 270°)
      diff = diff - 360.0;
    }
  }

  // Interpolate angle
  hand.currentAngle = hand.startAngle + diff * easedT;

  // Keep in 0-360 range
  while (hand.currentAngle < 0) hand.currentAngle += 360.0;
  while (hand.currentAngle >= 360.0) hand.currentAngle -= 360.0;
}

// Update shared opacity based on transition state
void updateOpacity(float t) {
  // Interpolate opacity (always uses ease-in-out)
  float opacityT = easeInOut(t);
  opacity.current = opacity.start + (opacity.target - opacity.start) * opacityT;
}

// Interpolate between two RGB565 colors
uint16_t lerpColor(uint16_t color1, uint16_t color2, float t) {
  // Extract RGB components from RGB565
  uint8_t r1 = (color1 >> 11) & 0x1F;
  uint8_t g1 = (color1 >> 5) & 0x3F;
  uint8_t b1 = color1 & 0x1F;

  uint8_t r2 = (color2 >> 11) & 0x1F;
  uint8_t g2 = (color2 >> 5) & 0x3F;
  uint8_t b2 = color2 & 0x1F;

  // Interpolate each component
  uint8_t r = r1 + (r2 - r1) * t;
  uint8_t g = g1 + (g2 - g1) * t;
  uint8_t b = b1 + (b2 - b1) * t;

  // Pack back to RGB565
  return (r << 11) | (g << 5) | b;
}

// Update colors based on transition state
void updateColors(float t) {
  // Interpolate colors (always uses ease-in-out)
  float colorT = easeInOut(t);
  colors.currentBg = lerpColor(colors.startBg, colors.targetBg, colorT);
  colors.currentFg = lerpColor(colors.startFg, colors.targetFg, colorT);
}

// ---- Helper functions ----

// Blend a color with background based on opacity (0-255)
// bgColor and fgColor are RGB565 format
uint16_t blendColor(uint16_t bgColor, uint16_t fgColor, uint8_t opacity) {
  if (opacity == 255) return fgColor;
  if (opacity == 0) return bgColor;

  // Extract RGB components from RGB565
  uint8_t bgR = (bgColor >> 11) & 0x1F;
  uint8_t bgG = (bgColor >> 5) & 0x3F;
  uint8_t bgB = bgColor & 0x1F;

  uint8_t fgR = (fgColor >> 11) & 0x1F;
  uint8_t fgG = (fgColor >> 5) & 0x3F;
  uint8_t fgB = fgColor & 0x1F;

  // Blend (alpha blend formula)
  uint8_t outR = ((fgR * opacity) + (bgR * (255 - opacity))) / 255;
  uint8_t outG = ((fgG * opacity) + (bgG * (255 - opacity))) / 255;
  uint8_t outB = ((fgB * opacity) + (bgB * (255 - opacity))) / 255;

  // Pack back to RGB565
  return (outR << 11) | (outG << 5) | outB;
}

// ---- Version Mode State ----
bool versionMode = false;  // If true, show version info on screen

// ---- Highlight Mode State ----
bool highlightMode = false;  // If true, show highlight state on screen
HighlightState currentHighlightState = HIGHLIGHT_IDLE;

// ---- ESP-NOW Packet Handler ----

// Called when an ESP-NOW packet is received
void onPacketReceived(const ESPNowPacket* packet, size_t len) {
  lastPacketTime = millis();

  // Clear error state when we receive a packet
  if (errorState) {
    errorState = false;
    Serial.println("ESP-NOW: Connection restored!");
  }

  // Handle different command types
  switch (packet->command) {
    case CMD_SET_ANGLES: {
      const AngleCommandPacket& cmd = packet->angleCmd;

      // Check if this pixel is targeted by this command
      if (!cmd.isPixelTargeted(pixelId)) {
        Serial.print("ESP-NOW: Pixel ");
        Serial.print(pixelId);
        Serial.println(" not targeted, ignoring command");
        break;
      }

      // Exit version/highlight mode when we receive a new command
      versionMode = false;
      highlightMode = false;

      // Extract angles for this pixel
      float target1, target2, target3;
      cmd.getPixelAngles(pixelId, target1, target2, target3);

      // Extract directions for this pixel
      RotationDirection dir1, dir2, dir3;
      cmd.getPixelDirections(pixelId, dir1, dir2, dir3);

      // Extract color and opacity for this pixel
      uint8_t colorIndex = cmd.colorIndices[pixelId];
      uint8_t targetOpacity = cmd.opacities[pixelId];

      // Get transition type directly (no mapping needed - they're the same now!)
      TransitionType easing = cmd.transition;

      // Convert duration from compact format to seconds
      float durationSec = durationToFloat(cmd.duration);

      // Get colors from palette
      uint16_t targetBg, targetFg;
      if (colorIndex < paletteSize) {
        targetBg = colorPalette[colorIndex].bg;
        targetFg = colorPalette[colorIndex].fg;
      } else {
        // Invalid index, use current colors
        targetBg = colors.currentBg;
        targetFg = colors.currentFg;
      }

      // Convert rotation directions to int8_t for startTransition
      // DIR_SHORTEST (0) = choose shortest path based on angle difference
      // DIR_CW (1) = clockwise (1)
      // DIR_CCW (2) = counter-clockwise (-1)
      int8_t direction1, direction2, direction3;

      if (dir1 == DIR_SHORTEST) {
        // Choose shortest path
        float diff = target1 - hand1.currentAngle;
        while (diff > 180.0) diff -= 360.0;
        while (diff < -180.0) diff += 360.0;
        direction1 = (diff >= 0) ? 1 : -1;
      } else {
        direction1 = (dir1 == DIR_CW) ? 1 : -1;
      }

      if (dir2 == DIR_SHORTEST) {
        float diff = target2 - hand2.currentAngle;
        while (diff > 180.0) diff -= 360.0;
        while (diff < -180.0) diff += 360.0;
        direction2 = (diff >= 0) ? 1 : -1;
      } else {
        direction2 = (dir2 == DIR_CW) ? 1 : -1;
      }

      if (dir3 == DIR_SHORTEST) {
        float diff = target3 - hand3.currentAngle;
        while (diff > 180.0) diff -= 360.0;
        while (diff < -180.0) diff += 360.0;
        direction3 = (diff >= 0) ? 1 : -1;
      } else {
        direction3 = (dir3 == DIR_CW) ? 1 : -1;
      }

      // Debug output
      Serial.print("Pixel ");
      Serial.print(pixelId);
      Serial.print(": Targets=(");
      Serial.print(target1, 0);
      Serial.print(",");
      Serial.print(target2, 0);
      Serial.print(",");
      Serial.print(target3, 0);
      Serial.print(") Dirs=(");
      Serial.print(dir1);
      Serial.print(",");
      Serial.print(dir2);
      Serial.print(",");
      Serial.print(dir3);
      Serial.print(") -> (");
      Serial.print(direction1);
      Serial.print(",");
      Serial.print(direction2);
      Serial.print(",");
      Serial.print(direction3);
      Serial.print(") Current=(");
      Serial.print(hand1.currentAngle, 0);
      Serial.print(",");
      Serial.print(hand2.currentAngle, 0);
      Serial.print(",");
      Serial.print(hand3.currentAngle, 0);
      Serial.println(")");

      // Start the transition with specified directions
      startTransition(target1, target2, target3, targetOpacity, targetBg, targetFg, durationSec, easing,
                      direction1, direction2, direction3);

      Serial.print("ESP-NOW: Angles [");
      Serial.print(target1, 0);
      Serial.print("°, ");
      Serial.print(target2, 0);
      Serial.print("°, ");
      Serial.print(target3, 0);
      Serial.print("°] dur=");
      Serial.print(durationSec, 2);
      Serial.print("s ease=");
      Serial.print(getEasingName(easing));
      Serial.print(" color=");
      Serial.print(colorIndex);
      Serial.print(" opacity=");
      Serial.println(targetOpacity);
      break;
    }

    case CMD_PING:
      Serial.println("ESP-NOW: Ping received");
      break;

    case CMD_RESET:
      Serial.println("ESP-NOW: Reset command received");
      // Clear all special display modes
      versionMode = false;
      highlightMode = false;
      errorState = false;
      Serial.println("ESP-NOW: All display modes cleared");
      break;

    case CMD_SET_PIXEL_ID: {
      const SetPixelIdPacket& cmd = packet->setPixelId;

      // Get this device's MAC address
      uint8_t myMac[6];
      WiFi.macAddress(myMac);

      // Check if this command is for us (MAC match or broadcast)
      bool isBroadcast = (cmd.targetMac[0] == 0xFF && cmd.targetMac[1] == 0xFF &&
                          cmd.targetMac[2] == 0xFF && cmd.targetMac[3] == 0xFF &&
                          cmd.targetMac[4] == 0xFF && cmd.targetMac[5] == 0xFF);
      bool macMatches = (memcmp(cmd.targetMac, myMac, 6) == 0);

      if (isBroadcast || macMatches) {
        // Store the new pixel ID in NVS
        preferences.begin(NVS_NAMESPACE, false);  // Read-write mode
        preferences.putUChar(NVS_KEY_PIXEL_ID, cmd.pixelId);
        preferences.end();

        // Update runtime variable
        uint8_t oldId = pixelId;
        pixelId = cmd.pixelId;

        Serial.print("ESP-NOW: Pixel ID assigned: ");
        Serial.print(oldId);
        Serial.print(" -> ");
        Serial.println(pixelId);
        Serial.println("ID stored in NVS (persists across reboots)");

        // Show visual confirmation - briefly flash green
        canvas->fillScreen(0x07E0);  // Green
        canvas->setTextColor(0x0000);  // Black text
        canvas->setTextSize(8);
        canvas->setCursor(pixelId < 10 ? 95 : 65, 85);
        canvas->print(pixelId);
        tft.drawRGBBitmap(0, 0, canvas->getBuffer(), DISPLAY_WIDTH, DISPLAY_HEIGHT);
        delay(500);
      }
      break;
    }

    case CMD_DISCOVERY: {
      const DiscoveryCommandPacket& cmd = packet->discovery;

      // Exit version mode when entering provision mode
      versionMode = false;

      // Get this device's MAC address
      uint8_t myMac[6];
      WiFi.macAddress(myMac);

      // Check if we're in the exclude list
      bool excluded = false;
      for (uint8_t i = 0; i < cmd.excludeCount && i < 20; i++) {
        if (memcmp(cmd.excludeMacs[i], myMac, 6) == 0) {
          excluded = true;
          break;
        }
      }

      if (!excluded) {
        // Enter discovery waiting mode - show "?"
        highlightMode = true;
        currentHighlightState = HIGHLIGHT_DISCOVERY_WAITING;
        Serial.println("ESP-NOW: Entering discovery waiting mode (showing ?)");

        // Random delay (0-2000ms) to avoid packet collisions
        uint16_t delayMs = random(2000);
        Serial.print("ESP-NOW: Discovery received, responding in ");
        Serial.print(delayMs);
        Serial.println("ms");
        delay(delayMs);

        // Send response with our MAC and current ID
        ESPNowPacket response;
        response.discoveryResponse.command = CMD_DISCOVERY_RESPONSE;  // CRITICAL: Use separate command to prevent infinite loop!
        memcpy(response.discoveryResponse.mac, myMac, 6);
        response.discoveryResponse.currentId = pixelId;

        if (ESPNowComm::sendPacket(&response, sizeof(DiscoveryResponsePacket))) {
          Serial.println("ESP-NOW: Discovery response sent");
        } else {
          Serial.println("ESP-NOW: Discovery response FAILED");
        }
      } else {
        Serial.println("ESP-NOW: Discovery received but we're excluded (already discovered)");
      }
      break;
    }

    case CMD_HIGHLIGHT: {
      const HighlightPacket& cmd = packet->highlight;

      // Get this device's MAC address
      uint8_t myMac[6];
      WiFi.macAddress(myMac);

      // Check if this command is for us
      if (memcmp(cmd.targetMac, myMac, 6) == 0) {
        Serial.print("ESP-NOW: Highlight state ");
        Serial.println(cmd.state);

        // Enter highlight mode and store the state
        highlightMode = true;
        currentHighlightState = cmd.state;
      }
      break;
    }

    case CMD_OTA_START: {
      const OTAStartPacket& start = packet->otaStart;

      // Only respond if this command is for us or broadcast to all (0xFF)
      if (start.targetPixelId != pixelId && start.targetPixelId != 0xFF) {
        break;  // Ignore - not for this pixel
      }

      Serial.println("ESP-NOW: OTA START received!");
      Serial.print("  Target: Pixel ");
      Serial.println(start.targetPixelId);
      Serial.print("  SSID: ");
      Serial.println(start.ssid);
      Serial.print("  URL: ");
      Serial.println(start.firmwareUrl);
      Serial.print("  Size: ");
      Serial.println(start.firmwareSize);

      // Latch OTA request; perform it later from loop() (safe context)
      portENTER_CRITICAL(&otaRequestMux);
      otaPendingStart = start;
      otaRequestPending = true;
      portEXIT_CRITICAL(&otaRequestMux);
      break;
    }

    case CMD_GET_VERSION: {
      const GetVersionPacket& cmd = packet->getVersion;
      Serial.println("ESP-NOW: Get version command received");

      // Send version response back to master
      ESPNowPacket response;
      response.versionResponse.command = CMD_VERSION_RESPONSE;
      response.versionResponse.pixelId = pixelId;
      response.versionResponse.versionMajor = FIRMWARE_VERSION_MAJOR;
      response.versionResponse.versionMinor = FIRMWARE_VERSION_MINOR;
      ESPNowComm::sendPacket(&response, sizeof(VersionResponsePacket));

      // Display version on screen if requested
      if (cmd.displayOnScreen) {
        versionMode = true;  // Enter version mode to persist display
        Serial.print("ESP-NOW: Version mode activated for pixel ");
        Serial.println(pixelId);
      }
      break;
    }

    default:
      Serial.print("ESP-NOW: Unknown command: ");
      Serial.println(packet->command);
  }
}

// ===== OTA UPDATE FUNCTIONS =====

// Send OTA status acknowledgment back to master
void sendOTAAck(OTAStatus status, uint8_t progress, uint16_t errorCode) {
  ESPNowPacket packet;
  packet.otaAck.command = CMD_OTA_ACK;
  packet.otaAck.pixelId = pixelId;
  packet.otaAck.status = status;
  packet.otaAck.progress = progress;
  packet.otaAck.errorCode = errorCode;

  ESPNowComm::sendPacket(&packet, sizeof(OTAAckPacket));
}

// Display OTA progress on screen
void displayOTAProgress(const char* status, int progress) {
  canvas->fillScreen(GC9A01A_BLUE);
  canvas->setTextColor(GC9A01A_WHITE);

  // Status text
  canvas->setTextSize(2);
  canvas->setCursor(30, 80);
  canvas->print(status);

  // Progress bar background
  canvas->fillRect(30, 120, 180, 20, GC9A01A_BLACK);

  // Progress bar fill
  if (progress > 0) {
    int fillWidth = (180 * progress) / 100;
    canvas->fillRect(30, 120, fillWidth, 20, GC9A01A_GREEN);
  }

  // Progress text
  canvas->setTextSize(2);
  canvas->setCursor(90, 150);
  canvas->print(progress);
  canvas->print("%");

  tft.drawRGBBitmap(0, 0, canvas->getBuffer(), DISPLAY_WIDTH, DISPLAY_HEIGHT);
}

// Perform OTA update - connects to WiFi and downloads firmware
void performOTAUpdate(const OTAStartPacket& start) {
  otaInProgress = true;
  currentOTAStatus = OTA_STATUS_STARTING;
  sendOTAAck(OTA_STATUS_STARTING, 0);

  displayOTAProgress("Connecting", 0);

  Serial.println("OTA: Connecting to WiFi...");
  Serial.print("OTA: SSID: ");
  Serial.println(start.ssid);
  Serial.print("OTA: URL: ");
  Serial.println(start.firmwareUrl);

  // Disconnect ESP-NOW temporarily
  Serial.println("OTA: Deinitializing ESP-NOW...");
  esp_now_deinit();

	// Reconfigure WiFi in a safe context (we are running from loop(), not callback)
	// NOTE: Avoid WiFi.mode(WIFI_MODE_NULL) here; it has been observed to hang on ESP32-S3.
	Serial.println("OTA: Preparing WiFi STA...");
	WiFi.disconnect(true);
	delay(200);
	WiFi.mode(WIFI_STA);
	delay(200);

	// Scan for the AP first to verify it's visible (best-effort)
	Serial.println("OTA: Scanning for networks...");
	int n = WiFi.scanNetworks();
	Serial.print("OTA: Found ");
	Serial.print(n);
	Serial.println(" networks");
	if (n >= 0) {
	  bool apFound = false;
	  for (int i = 0; i < n; i++) {
	    Serial.print("  ");
	    Serial.print(i);
	    Serial.print(": ");
	    Serial.print(WiFi.SSID(i));
	    Serial.print(" (Ch ");
	    Serial.print(WiFi.channel(i));
	    Serial.print(", RSSI ");
	    Serial.print(WiFi.RSSI(i));
	    Serial.println(")");

	    if (WiFi.SSID(i) == String(start.ssid)) {
	      apFound = true;
	      Serial.println("  ^^ TARGET AP FOUND!");
	    }
	  }
	  if (!apFound) {
	    Serial.println("OTA: WARNING - Target AP not found in scan!");
	  }
	} else {
	  Serial.println("OTA: WARNING - scanNetworks failed; continuing anyway");
	}

  Serial.println("OTA: Starting WiFi connection...");
  Serial.flush();
  WiFi.begin(start.ssid, start.password);

  // Wait for connection (timeout after 30 seconds)
  int timeout = 60;  // 30 seconds (500ms per iteration)
  while (WiFi.status() != WL_CONNECTED && timeout > 0) {
    delay(500);
    Serial.print(".");
    timeout--;
    displayOTAProgress("Connecting", (60 - timeout) * 100 / 60);
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nOTA: WiFi connection failed!");
    Serial.print("OTA: WiFi status: ");
    Serial.println(WiFi.status());
    Serial.println("OTA: Status codes: 0=IDLE, 1=NO_SSID, 3=CONNECTED, 4=CONNECT_FAILED, 6=DISCONNECTED");
    displayOTAProgress("WiFi Failed", 0);
    currentOTAStatus = OTA_STATUS_ERROR;

    // Restore ESP-NOW
    delay(2000);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);
    ESPNowComm::initReceiver(ESPNOW_CHANNEL);
    ESPNowComm::setReceiveCallback(onPacketReceived);
    lastPacketTime = millis();  // Reset timeout to avoid immediate error
    otaInProgress = false;
    return;
  }

  Serial.println("\nOTA: WiFi connected!");
  Serial.print("OTA: IP address: ");
  Serial.println(WiFi.localIP());

  currentOTAStatus = OTA_STATUS_DOWNLOADING;
  sendOTAAck(OTA_STATUS_DOWNLOADING, 0);
  displayOTAProgress("Downloading", 0);

  // Set up progress callback
  httpUpdate.onProgress([](int cur, int total) {
    int progress = (cur * 100) / total;
    currentOTAProgress = progress;
    displayOTAProgress("Updating", progress);
    Serial.printf("OTA Progress: %d%%\n", progress);
  });

  // Perform the update
  WiFiClient client;
  Serial.print("OTA: Downloading from ");
  Serial.println(start.firmwareUrl);

  t_httpUpdate_return ret = httpUpdate.update(client, start.firmwareUrl);

  switch (ret) {
    case HTTP_UPDATE_FAILED:
      Serial.printf("OTA: Update failed! Error (%d): %s\n",
                    httpUpdate.getLastError(),
                    httpUpdate.getLastErrorString().c_str());
      displayOTAProgress("FAILED!", 0);
      canvas->setTextSize(1);
      canvas->setCursor(20, 180);
      canvas->print(httpUpdate.getLastErrorString().c_str());
      tft.drawRGBBitmap(0, 0, canvas->getBuffer(), DISPLAY_WIDTH, DISPLAY_HEIGHT);

      currentOTAStatus = OTA_STATUS_ERROR;
      sendOTAAck(OTA_STATUS_ERROR, 0, httpUpdate.getLastError());

      // Restore ESP-NOW
      delay(5000);  // Show error for 5 seconds
      Serial.println("OTA: Restoring ESP-NOW...");
      WiFi.disconnect(true);
      WiFi.mode(WIFI_STA);
      ESPNowComm::initReceiver(ESPNOW_CHANNEL);
      ESPNowComm::setReceiveCallback(onPacketReceived);
      lastPacketTime = millis();  // Reset timeout to avoid immediate error

      // Clear the OTA screen
      canvas->fillScreen(GC9A01A_BLACK);
      tft.drawRGBBitmap(0, 0, canvas->getBuffer(), DISPLAY_WIDTH, DISPLAY_HEIGHT);
      Serial.println("OTA: Returned to normal operation");
      break;

    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("OTA: No updates available (same firmware)");
      displayOTAProgress("Same Version", 0);
      delay(3000);  // Show message for 3 seconds

      // Restore ESP-NOW
      Serial.println("OTA: Restoring ESP-NOW...");
      WiFi.disconnect(true);
      WiFi.mode(WIFI_STA);
      ESPNowComm::initReceiver(ESPNOW_CHANNEL);
      ESPNowComm::setReceiveCallback(onPacketReceived);
      lastPacketTime = millis();  // Reset timeout to avoid immediate error

      // Clear the OTA screen
      canvas->fillScreen(GC9A01A_BLACK);
      tft.drawRGBBitmap(0, 0, canvas->getBuffer(), DISPLAY_WIDTH, DISPLAY_HEIGHT);
      Serial.println("OTA: Returned to normal operation");
      break;

    case HTTP_UPDATE_OK:
      Serial.println("OTA: Update successful! Rebooting...");
      displayOTAProgress("SUCCESS!", 100);
      currentOTAStatus = OTA_STATUS_SUCCESS;
      sendOTAAck(OTA_STATUS_SUCCESS, 100);
      delay(1000);
      // Device will reboot automatically
      ESP.restart();
      break;
  }

  otaInProgress = false;
}

// Draw a thick clock hand using 2 filled triangles (forming a rectangle) + rounded caps
// This is more efficient than drawing many circles along the line
void drawHand(float cx, float cy, float angleDeg, float length, float thickness, uint16_t color) {
  // Convert angle to radians (subtract 90 to make 0 degrees point up)
  float angleRad = (angleDeg - 90.0) * PI / 180.0;
  float perpRad = angleRad + PI / 2.0;  // Perpendicular angle for width

  float halfThick = thickness / 2.0;

  // Calculate 4 corners of the rectangle
  // Base corners (at center)
  float x1 = cx + cos(perpRad) * halfThick;
  float y1 = cy + sin(perpRad) * halfThick;
  float x2 = cx - cos(perpRad) * halfThick;
  float y2 = cy - sin(perpRad) * halfThick;

  // End point
  float endX = cx + cos(angleRad) * length;
  float endY = cy + sin(angleRad) * length;

  // Tip corners (at end of hand)
  float x3 = endX + cos(perpRad) * halfThick;
  float y3 = endY + sin(perpRad) * halfThick;
  float x4 = endX - cos(perpRad) * halfThick;
  float y4 = endY - sin(perpRad) * halfThick;

  // Draw two triangles to form rectangle
  canvas->fillTriangle(x1, y1, x2, y2, x3, y3, color);
  canvas->fillTriangle(x2, y2, x3, y3, x4, y4, color);

  // Add rounded caps at both ends
  canvas->fillCircle(cx, cy, (int)halfThick, color);      // Base cap
  canvas->fillCircle(endX, endY, (int)halfThick, color);  // Tip cap
}

void setup() {
  Serial.begin(115200);
  delay(200);

  // ---- Load Pixel ID from NVS ----
  preferences.begin(NVS_NAMESPACE, true);  // Read-only mode
  pixelId = preferences.getUChar(NVS_KEY_PIXEL_ID, PIXEL_ID_UNPROVISIONED);
  preferences.end();

  // ---- Board Identification ----
  Serial.println("\n========== TWENTY-FOUR TIMES - PIXEL NODE ==========");
  Serial.print("Board: ");
  Serial.println(BOARD_NAME);
  Serial.print("Pixel ID: ");
  if (pixelId == PIXEL_ID_UNPROVISIONED) {
    Serial.println("UNPROVISIONED (255)");
  } else {
    Serial.println(pixelId);
  }
  Serial.println("====================================================\n");

  // ---- Memory Statistics ----
  Serial.println("========== MEMORY DEBUG INFO ==========");
  Serial.print("Free heap: ");
  Serial.print(ESP.getFreeHeap());
  Serial.println(" bytes");
  
  Serial.print("Total heap: ");
  Serial.print(ESP.getHeapSize());
  Serial.println(" bytes");
  
  Serial.print("Min free heap (since boot): ");
  Serial.print(ESP.getMinFreeHeap());
  Serial.println(" bytes");
  
  Serial.print("Max alloc heap: ");
  Serial.print(ESP.getMaxAllocHeap());
  Serial.println(" bytes");
  
  #ifdef BOARD_HAS_PSRAM
    Serial.print("Free PSRAM: ");
    Serial.print(ESP.getFreePsram());
    Serial.println(" bytes");

    Serial.print("Total PSRAM: ");
    Serial.print(ESP.getPsramSize());
    Serial.println(" bytes");
  #else
    Serial.println("PSRAM: Not available");
  #endif
  
  Serial.print("Chip model: ");
  Serial.println(ESP.getChipModel());
  
  Serial.print("Chip cores: ");
  Serial.println(ESP.getChipCores());
  
  Serial.print("CPU frequency: ");
  Serial.print(ESP.getCpuFreqMHz());
  Serial.println(" MHz");
  Serial.println("=======================================\n");

  Serial.println("Twenty-Four Times - Clock Hands Proof of Concept (Adafruit GFX)");
  Serial.print("Max radius: ");
  Serial.print(MAX_RADIUS);
  Serial.println(" pixels");
  Serial.print("Hand length: ");
  Serial.print(HAND_LENGTH_NORMAL);
  Serial.println(" pixels");

  Serial.print("Canvas buffer size: ");
  Serial.print(DISPLAY_WIDTH * DISPLAY_HEIGHT * 2);
  Serial.println(" bytes (115,200 bytes)");

  // ---- SPI ----
  #ifdef USE_HARDWARE_SPI
    // ESP32-S3: Use default FSPI pins (no remapping needed)
    SPI.begin();  // Uses default pins: CLK=12, MOSI=11, MISO=13, CS=10
    Serial.println("SPI initialized with default FSPI pins (hardware SPI)");
  #else
    // Software SPI
    SPI.begin(tft_scl, -1, tft_sda);
    Serial.println("SPI initialized with custom pins (software SPI)");
  #endif

  // ---- Canvas ----
  Serial.println("Allocating canvas buffer (115,200 bytes)...");
  canvas = new GFXcanvas16(240, 240);
  if (!canvas) {
    Serial.println("ERROR: Failed to allocate canvas!");
    while(1) delay(1000);
  }
  Serial.print("Canvas allocated! Free heap: ");
  Serial.print(ESP.getFreeHeap());
  Serial.println(" bytes");

  // ---- TFT ----
  Serial.println("Initializing TFT...");
  #ifdef USE_HARDWARE_SPI
    // Hardware SPI: Use high frequency (80MHz max for ESP32-S3 FSPI)
    tft.begin(80000000);  // 80 MHz
    Serial.println("TFT initialized at 80 MHz (hardware SPI)");
  #else
    // Software SPI: frequency parameter is ignored
    tft.begin();
    Serial.println("TFT initialized (software SPI)");
  #endif
  tft.setRotation(1);

  Serial.print("Free heap after TFT init: ");
  Serial.print(ESP.getFreeHeap());
  Serial.println(" bytes");

  // Clear canvas to white
  canvas->fillScreen(GC9A01A_WHITE);

  Serial.println("\nSetup complete!");

  // Initialize timing
  lastUpdateTime = millis();

  // ---- ESP-NOW ----
  Serial.println("\n========== ESP-NOW INIT ==========");
  Serial.print("Pixel ID: ");
  Serial.println(pixelId);

  if (ESPNowComm::initReceiver(ESPNOW_CHANNEL)) {
    ESPNowComm::setReceiveCallback(onPacketReceived);
    espnowEnabled = true;
    lastPacketTime = millis();  // Initialize packet time to avoid immediate error
    Serial.println("ESP-NOW initialized successfully!");
    Serial.println("Mode: Waiting for master commands");
    Serial.println("Will show error screen if no commands received within 10s");
  } else {
    Serial.println("ESP-NOW initialization failed!");
    Serial.println("ERROR: Cannot operate without ESP-NOW");
    errorState = true;  // Show error immediately
  }
  Serial.println("==================================\n");
}

void loop() {
  unsigned long currentTime = millis();

  // ---- Skip normal loop during OTA ----
  // OTA update handles its own display and WiFi, so skip everything else
  if (otaInProgress) {
    delay(10);
    return;
  }

  // ---- Start OTA if requested (must NOT run from ESP-NOW receive callback) ----
  if (otaRequestPending) {
    OTAStartPacket startCmd;
    bool shouldStart = false;
    portENTER_CRITICAL(&otaRequestMux);
    if (otaRequestPending) {
      startCmd = otaPendingStart;
      otaRequestPending = false;
      shouldStart = true;
    }
    portEXIT_CRITICAL(&otaRequestMux);

    if (shouldStart) {
      performOTAUpdate(startCmd);
      return;
    }
  }

  // ---- ESP-NOW Timeout Check ----
  // If we haven't received a packet in PACKET_TIMEOUT ms, show error state
  // Skip this check during OTA since ESP-NOW is disabled
  if (espnowEnabled && !errorState && !otaInProgress && (currentTime - lastPacketTime > PACKET_TIMEOUT)) {
    Serial.println("\n!!! ESP-NOW TIMEOUT - NO MASTER SIGNAL !!!\n");
    errorState = true;
  }

  // ---- Unprovisioned State Display ----
  // If pixel has no assigned ID, show green screen with "?" and wait for provisioning
  if (pixelId == PIXEL_ID_UNPROVISIONED) {
    canvas->fillScreen(0x07E0);  // Green background

    // Draw large white question mark in the center
    canvas->setTextColor(GC9A01A_WHITE);
    canvas->setTextSize(15);
    canvas->setCursor(85, 90);
    canvas->print("?");

    // Present unprovisioned frame to display
    tft.drawRGBBitmap(0, 0, canvas->getBuffer(), DISPLAY_WIDTH, DISPLAY_HEIGHT);

    // Slow update rate - just waiting for provisioning
    delay(100);
    return;
  }

  // ---- Version Mode Display ----
  // If in version mode, show version info and skip normal rendering
  if (versionMode) {
    canvas->fillScreen(GC9A01A_MAGENTA);
    canvas->setTextColor(GC9A01A_WHITE);
    canvas->setTextSize(3);
    canvas->setCursor(60, 80);
    canvas->print("Pixel ");
    canvas->println(pixelId);
    canvas->setCursor(80, 130);
    canvas->print("v");
    canvas->print(FIRMWARE_VERSION_MAJOR);
    canvas->print(".");
    canvas->println(FIRMWARE_VERSION_MINOR);

    // Present version frame to display
    tft.drawRGBBitmap(0, 0, canvas->getBuffer(), DISPLAY_WIDTH, DISPLAY_HEIGHT);

    // Small delay and return (skip normal rendering)
    delay(100);
    return;
  }

  // ---- Highlight Mode Display ----
  // If in highlight mode, show highlight state and skip normal rendering
  if (highlightMode) {
    // Get MAC address for display
    uint8_t myMac[6];
    WiFi.macAddress(myMac);
    char macStr[18];
    sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X",
            myMac[0], myMac[1], myMac[2], myMac[3], myMac[4], myMac[5]);

    switch (currentHighlightState) {
      case HIGHLIGHT_IDLE:
        // Blue border, black bg, show MAC and current ID
        canvas->fillScreen(GC9A01A_BLACK);
        // Draw blue border (thick circle outline)
        for (int r = 115; r < 120; r++) {
          canvas->drawCircle(CENTER_X, CENTER_Y, r, 0x001F);  // Blue
        }
        canvas->setTextColor(GC9A01A_WHITE);
        canvas->setTextSize(2);
        canvas->setCursor(20, 60);
        canvas->print("MAC:");
        canvas->setCursor(10, 85);
        canvas->print(macStr);
        canvas->setCursor(50, 130);
        canvas->print("ID: ");
        if (pixelId == PIXEL_ID_UNPROVISIONED) {
          canvas->print("?");
        } else {
          canvas->print(pixelId);
        }
        break;

      case HIGHLIGHT_SELECTED:
        // Bright green bg with black text, show MAC and current ID
        canvas->fillScreen(0x07E0);  // Green
        canvas->setTextColor(GC9A01A_BLACK);
        canvas->setTextSize(2);
        canvas->setCursor(20, 60);
        canvas->print("MAC:");
        canvas->setCursor(10, 85);
        canvas->print(macStr);
        canvas->setCursor(50, 130);
        canvas->print("ID: ");
        if (pixelId == PIXEL_ID_UNPROVISIONED) {
          canvas->print("?");
        } else {
          canvas->print(pixelId);
        }
        break;

      case HIGHLIGHT_ASSIGNED:
        // Black bg, show OK and assigned ID
        canvas->fillScreen(GC9A01A_BLACK);
        canvas->setTextColor(0x07E0);  // Green
        canvas->setTextSize(4);
        canvas->setCursor(80, 60);
        canvas->print("OK");
        canvas->setTextSize(2);
        canvas->setCursor(20, 110);
        canvas->print("MAC:");
        canvas->setCursor(10, 135);
        canvas->print(macStr);
        canvas->setCursor(40, 180);
        canvas->print("ID: ");
        canvas->print(pixelId);
        break;

      case HIGHLIGHT_DISCOVERY_WAITING:
        // Black bg with white "?" - waiting to be discovered
        canvas->fillScreen(GC9A01A_BLACK);
        canvas->setTextColor(GC9A01A_WHITE);
        canvas->setTextSize(15);
        canvas->setCursor(85, 90);
        canvas->print("?");
        break;

      case HIGHLIGHT_DISCOVERY_FOUND:
        // Black bg with white "!" - discovered, waiting for assignment
        canvas->fillScreen(GC9A01A_BLACK);
        canvas->setTextColor(GC9A01A_WHITE);
        canvas->setTextSize(15);
        canvas->setCursor(95, 90);
        canvas->print("!");
        break;
    }

    // Present highlight frame to display
    tft.drawRGBBitmap(0, 0, canvas->getBuffer(), DISPLAY_WIDTH, DISPLAY_HEIGHT);

    // Small delay and return (skip normal rendering)
    delay(100);
    return;
  }

  // ---- Error State Display ----
  // If in error state, just show red screen with "!" and skip normal rendering
  if (errorState) {
    canvas->fillScreen(GC9A01A_RED);

    // Draw large "!" in the center
    // We'll draw it manually since we want it large and centered
    canvas->setTextColor(GC9A01A_WHITE);
    canvas->setTextSize(10);  // Large text
    canvas->setCursor(95, 90);  // Roughly centered for "!"
    canvas->print("!");

    // Present error frame to display
    tft.drawRGBBitmap(0, 0, canvas->getBuffer(), DISPLAY_WIDTH, DISPLAY_HEIGHT);

    // Small delay and return (skip normal rendering)
    delay(100);
    return;
  }

  // Update hand angles based on transition
  if (transition.isActive) {
    // Calculate elapsed time in seconds
    float elapsed = (currentTime - transition.startTime) / 1000.0;

    // Calculate progress (0.0 to 1.0)
    float t = elapsed / transition.duration;

    if (t >= 1.0) {
      // Transition complete - set to target and normalize
      hand1.currentAngle = hand1.targetAngle;
      hand2.currentAngle = hand2.targetAngle;
      hand3.currentAngle = hand3.targetAngle;

      // Normalize angles to 0-360 range
      while (hand1.currentAngle < 0) hand1.currentAngle += 360.0;
      while (hand1.currentAngle >= 360.0) hand1.currentAngle -= 360.0;
      while (hand2.currentAngle < 0) hand2.currentAngle += 360.0;
      while (hand2.currentAngle >= 360.0) hand2.currentAngle -= 360.0;
      while (hand3.currentAngle < 0) hand3.currentAngle += 360.0;
      while (hand3.currentAngle >= 360.0) hand3.currentAngle -= 360.0;

      opacity.current = opacity.target;
      colors.currentBg = colors.targetBg;
      colors.currentFg = colors.targetFg;
      transition.isActive = false;
    } else {
      // Update all hands with same progress value
      updateHandAngle(hand1, t);
      updateHandAngle(hand2, t);
      updateHandAngle(hand3, t);
      updateOpacity(t);
      updateColors(t);
    }
  }

  // ---- Rendering ----
  // Clear canvas with current background color
  canvas->fillScreen(colors.currentBg);

  // Optional: Draw reference circle to show the max radius
  // canvas->drawCircle(CENTER_X, CENTER_Y, MAX_RADIUS - 1, tft.color565(200, 200, 200));

  // Draw the three clock hands with opacity blending
  // Blend foreground color with background based on opacity
  uint16_t handColor = blendColor(colors.currentBg, colors.currentFg, opacity.current);

  // Draw hands 1 and 2 with normal thickness
  drawHand(CENTER_X, CENTER_Y, hand1.currentAngle, HAND_LENGTH_NORMAL, HAND_THICKNESS_NORMAL, handColor);
  drawHand(CENTER_X, CENTER_Y, hand2.currentAngle, HAND_LENGTH_NORMAL, HAND_THICKNESS_NORMAL, handColor);

  // Draw hand 3 with thin thickness
  drawHand(CENTER_X, CENTER_Y, hand3.currentAngle, HAND_LENGTH_NORMAL, HAND_THICKNESS_THIN, handColor);

  // Draw center dot (always full opacity foreground color)
  canvas->fillCircle(CENTER_X, CENTER_Y, 4, colors.currentFg);

  // Present frame to display
  tft.drawRGBBitmap(0, 0, canvas->getBuffer(), DISPLAY_WIDTH, DISPLAY_HEIGHT);

  // ---- FPS tracking ----
  fpsFrames++;
  unsigned long now = millis();

  if (now - fpsLastTime >= 1000) {
    float fps = fpsFrames * 1000.0f / (now - fpsLastTime);
    Serial.print("FPS: ");
    Serial.println(fps, 1);

    fpsFrames = 0;
    fpsLastTime = now;
  }
}
