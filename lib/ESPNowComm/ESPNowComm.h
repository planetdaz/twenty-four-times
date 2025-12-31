#ifndef ESPNOW_COMM_H
#define ESPNOW_COMM_H

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>

// ===== PROTOCOL CONSTANTS =====

// Maximum number of pixels in the system
#define MAX_PIXELS 24

// Number of hands per pixel
#define HANDS_PER_PIXEL 3

// Broadcast MAC address (all pixels listen to this)
static const uint8_t BROADCAST_MAC[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// WiFi channel for ESP-NOW communication (must match on all devices)
#define ESPNOW_CHANNEL 1

// ===== PACKET STRUCTURE =====

// Command types
enum CommandType : uint8_t {
  CMD_SET_ANGLES = 0x01,      // Set target angles for all pixels
  CMD_PING = 0x02,            // Heartbeat/connectivity test
  CMD_RESET = 0x03,           // Reset all pixels to default state
  CMD_SET_PIXEL_ID = 0x04,    // Assign pixel ID (for provisioning)
  CMD_IDENTIFY = 0x05         // Show pixel ID on screen (for identification)
};

// Transition/easing types (matches pixel's EasingType enum)
enum TransitionType : uint8_t {
  TRANSITION_LINEAR = 0,
  TRANSITION_EASE_IN_OUT = 1,
  TRANSITION_ELASTIC = 2,
  TRANSITION_BOUNCE = 3,
  TRANSITION_BACK_IN = 4,
  TRANSITION_BACK_OUT = 5,
  TRANSITION_BACK_IN_OUT = 6,
  TRANSITION_INSTANT = 7
};

// Direction for hand rotation
enum RotationDirection : uint8_t {
  DIR_SHORTEST = 0,  // Choose shortest path (default)
  DIR_CW = 1,        // Clockwise
  DIR_CCW = 2        // Counter-clockwise
};

// Compact angle representation (0-255 maps to 0-360 degrees)
// This saves bandwidth: 1 byte vs 4 bytes for float
typedef uint8_t angle_t;

// Convert float angle (0-360) to compact representation
// Fixed rounding to ensure 90° -> 90°, 180° -> 180°, etc.
inline angle_t floatToAngle(float degrees) {
  while (degrees < 0) degrees += 360.0f;
  while (degrees >= 360.0f) degrees -= 360.0f;
  return (uint8_t)((degrees / 360.0f) * 256.0f + 0.5f);  // Round to nearest
}

// Convert compact angle to float (0-360)
inline float angleToFloat(angle_t angle) {
  return (angle / 256.0f) * 360.0f;
}

// Compact duration representation (0-255 maps to 0-60 seconds)
typedef uint8_t duration_t;

// Convert float duration (0-60 seconds) to compact representation
inline duration_t floatToDuration(float seconds) {
  if (seconds < 0) seconds = 0;
  if (seconds > 60.0f) seconds = 60.0f;
  return (uint8_t)((seconds / 60.0f) * 255.0f + 0.5f);  // Round to nearest
}

// Convert compact duration to float (0-60 seconds)
inline float durationToFloat(duration_t duration) {
  return (duration / 255.0f) * 60.0f;
}

// Command packet for setting angles
// Total size: 1 + 1 + 1 + 72 + 72 + 24 + 24 + 24 = 219 bytes (under ESP-NOW's 250 byte limit)
struct __attribute__((packed)) AngleCommandPacket {
  CommandType command;              // 1 byte: Command type (CMD_SET_ANGLES)
  TransitionType transition;        // 1 byte: Transition/easing type
  duration_t duration;              // 1 byte: Transition duration (0-60 seconds)
  angle_t angles[MAX_PIXELS][HANDS_PER_PIXEL];  // 72 bytes: Target angles for all pixels
  RotationDirection directions[MAX_PIXELS][HANDS_PER_PIXEL]; // 72 bytes: Rotation directions
  uint8_t colorIndices[MAX_PIXELS]; // 24 bytes: Color palette index for each pixel
  uint8_t opacities[MAX_PIXELS];    // 24 bytes: Opacity for each pixel (0-255)
  uint8_t reserved[24];             // 24 bytes: Reserved for future use

  // Helper to set angles for a specific pixel
  void setPixelAngles(uint8_t pixelIndex, float angle1, float angle2, float angle3,
                      RotationDirection dir1 = DIR_SHORTEST,
                      RotationDirection dir2 = DIR_SHORTEST,
                      RotationDirection dir3 = DIR_SHORTEST) {
    if (pixelIndex < MAX_PIXELS) {
      angles[pixelIndex][0] = floatToAngle(angle1);
      angles[pixelIndex][1] = floatToAngle(angle2);
      angles[pixelIndex][2] = floatToAngle(angle3);
      directions[pixelIndex][0] = dir1;
      directions[pixelIndex][1] = dir2;
      directions[pixelIndex][2] = dir3;
    }
  }

  // Helper to get angles for a specific pixel
  void getPixelAngles(uint8_t pixelIndex, float &angle1, float &angle2, float &angle3) const {
    if (pixelIndex < MAX_PIXELS) {
      angle1 = angleToFloat(angles[pixelIndex][0]);
      angle2 = angleToFloat(angles[pixelIndex][1]);
      angle3 = angleToFloat(angles[pixelIndex][2]);
    }
  }

  // Helper to get directions for a specific pixel
  void getPixelDirections(uint8_t pixelIndex, RotationDirection &dir1, RotationDirection &dir2, RotationDirection &dir3) const {
    if (pixelIndex < MAX_PIXELS) {
      dir1 = directions[pixelIndex][0];
      dir2 = directions[pixelIndex][1];
      dir3 = directions[pixelIndex][2];
    }
  }

  // Helper to set color and opacity for a specific pixel
  void setPixelStyle(uint8_t pixelIndex, uint8_t colorIndex, uint8_t opacity) {
    if (pixelIndex < MAX_PIXELS) {
      colorIndices[pixelIndex] = colorIndex;
      opacities[pixelIndex] = opacity;
    }
  }
};

// Simple ping packet
struct __attribute__((packed)) PingPacket {
  CommandType command;  // CMD_PING
  uint32_t timestamp;   // Sender's millis()
};

// Identify packet - tells a pixel to show its ID
struct __attribute__((packed)) IdentifyPacket {
  CommandType command;  // CMD_IDENTIFY
  uint8_t pixelId;      // Which pixel to identify (0-23, or 255 for all)
};

// Generic packet union for easy handling
union ESPNowPacket {
  CommandType command;
  AngleCommandPacket angleCmd;
  PingPacket ping;
  IdentifyPacket identify;
  uint8_t raw[250];  // ESP-NOW max packet size
};

// ===== COLOR PALETTE =====
// Shared color palette between master and pixels
// Each entry has a name for display purposes

struct ColorPaletteEntry {
  const char* name;
};

// Color palette names (pixels will use indices to look up actual RGB565 colors)
const ColorPaletteEntry COLOR_PALETTE[] = {
  {"White on Black"},
  {"Black on White"},
  {"Dark Brown on Cream"},
  {"Cream on Dark Brown"},
  {"Wheat on Dark Slate"},
  {"Dark Slate on Wheat"},
  {"Cornsilk on Saddle Brown"},
  {"Light Gray on Navy"},
  {"Light Yellow on Red-Orange"},
  {"Dark Magenta on Gold"},
  {"White on Deep Sky Blue"},
  {"Ivory on Deep Pink"},
  {"Midnight Blue on Lime Green"},
  {"Lemon Chiffon on Blue Violet"},
  {"Midnight Blue on Dark Orange"},
  {"Dark Red on Turquoise"}
};

const uint8_t COLOR_PALETTE_SIZE = sizeof(COLOR_PALETTE) / sizeof(ColorPaletteEntry);

// ===== HELPER FUNCTIONS FOR MASTER =====
// These functions are used by the master to generate random values
// Pixels no longer generate random values - they only follow commands

// Get a random angle from the allowed set: 0, 90, 180, 270
inline float getRandomAngle() {
  const float angles[] = {0.0f, 90.0f, 180.0f, 270.0f};
  return angles[random(4)];
}

// Get a random color pair index from the palette
inline uint8_t getRandomColorIndex() {
  return random(COLOR_PALETTE_SIZE);
}

// Get a random easing type
inline TransitionType getRandomTransition() {
  return (TransitionType)random(7);  // 0-6 for the 7 transition types (excluding INSTANT)
}

// Get a random duration (0.5 to 9.0 seconds) with weighted distribution
// Favors longer durations - most animations look better when slower
inline float getRandomDuration() {
  // Weighted random: higher numbers = longer durations more likely
  // Using triangular distribution: pick 2 random numbers, use the max
  // This biases toward higher values (longer durations)
  float r1 = random(851) / 100.0f;  // 0.0 to 8.5
  float r2 = random(851) / 100.0f;  // 0.0 to 8.5
  float duration = max(r1, r2) + 0.5f;  // 0.5 to 9.0 seconds, biased toward longer
  return duration;
}

// Get a random opacity from allowed set: 0 (transparent), 50 (faint), 255 (opaque)
inline uint8_t getRandomOpacity() {
  const uint8_t opacities[] = {0, 50, 255};
  return opacities[random(3)];
}

// Get transition name for display
inline const char* getTransitionName(TransitionType transition) {
  switch (transition) {
    case TRANSITION_LINEAR: return "Linear";
    case TRANSITION_EASE_IN_OUT: return "Ease In-Out";
    case TRANSITION_ELASTIC: return "Elastic";
    case TRANSITION_BOUNCE: return "Bounce";
    case TRANSITION_BACK_IN: return "Back In";
    case TRANSITION_BACK_OUT: return "Back Out";
    case TRANSITION_BACK_IN_OUT: return "Back In-Out";
    case TRANSITION_INSTANT: return "Instant";
    default: return "Unknown";
  }
}

// ===== CALLBACK TYPES =====

// Callback for when a packet is received
typedef void (*PacketReceivedCallback)(const ESPNowPacket* packet, size_t len);

// ===== ESP-NOW HELPER CLASS =====

class ESPNowComm {
public:
  // Initialize ESP-NOW in receiver mode (for pixels)
  static bool initReceiver(uint8_t channel = ESPNOW_CHANNEL);
  
  // Initialize ESP-NOW in sender mode (for master)
  static bool initSender(uint8_t channel = ESPNOW_CHANNEL);
  
  // Send a packet (master only)
  static bool sendPacket(const ESPNowPacket* packet, size_t len);
  
  // Set callback for received packets
  static void setReceiveCallback(PacketReceivedCallback callback);
  
  // Get MAC address as string (for debugging)
  static String getMacAddress();
  
private:
  static PacketReceivedCallback receiveCallback;
  static void onDataRecv(const uint8_t* mac, const uint8_t* data, int len);
  static void onDataSent(const uint8_t* mac, esp_now_send_status_t status);
};

#endif // ESPNOW_COMM_H

