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
  CMD_SET_PIXEL_ID = 0x04     // Assign pixel ID (for provisioning)
};

// Transition/easing types
enum TransitionType : uint8_t {
  TRANSITION_LINEAR = 0,
  TRANSITION_EASE_IN_OUT = 1,
  TRANSITION_ELASTIC = 2,
  TRANSITION_INSTANT = 3
};

// Compact angle representation (0-255 maps to 0-360 degrees)
// This saves bandwidth: 1 byte vs 4 bytes for float
typedef uint8_t angle_t;

// Convert float angle (0-360) to compact representation
inline angle_t floatToAngle(float degrees) {
  while (degrees < 0) degrees += 360.0f;
  while (degrees >= 360.0f) degrees -= 360.0f;
  return (uint8_t)((degrees / 360.0f) * 255.0f);
}

// Convert compact angle to float (0-360)
inline float angleToFloat(angle_t angle) {
  return (angle / 255.0f) * 360.0f;
}

// Command packet for setting angles
// Total size: 1 + 1 + 2 + 72 = 76 bytes (well under ESP-NOW's 250 byte limit)
struct __attribute__((packed)) AngleCommandPacket {
  CommandType command;              // 1 byte: Command type
  TransitionType transition;        // 1 byte: Transition/easing type
  uint16_t duration_ms;             // 2 bytes: Transition duration in milliseconds
  angle_t angles[MAX_PIXELS][HANDS_PER_PIXEL]; // 72 bytes: Angles for all pixels
  
  // Helper to set angles for a specific pixel
  void setPixelAngles(uint8_t pixelIndex, float angle1, float angle2, float angle3) {
    if (pixelIndex < MAX_PIXELS) {
      angles[pixelIndex][0] = floatToAngle(angle1);
      angles[pixelIndex][1] = floatToAngle(angle2);
      angles[pixelIndex][2] = floatToAngle(angle3);
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
};

// Simple ping packet
struct __attribute__((packed)) PingPacket {
  CommandType command;  // CMD_PING
  uint32_t timestamp;   // Sender's millis()
};

// Generic packet union for easy handling
union ESPNowPacket {
  CommandType command;
  AngleCommandPacket angleCmd;
  PingPacket ping;
  uint8_t raw[250];  // ESP-NOW max packet size
};

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

