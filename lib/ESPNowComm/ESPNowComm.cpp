#include "ESPNowComm.h"
#include <esp_wifi.h>

// Static member initialization
PacketReceivedCallback ESPNowComm::receiveCallback = nullptr;

// Initialize ESP-NOW in receiver mode (for pixels)
// Also adds broadcast peer so pixels can send discovery responses
bool ESPNowComm::initReceiver(uint8_t channel) {
  // Set device as a Wi-Fi Station on specified channel
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  // Set WiFi channel
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  // Print MAC address for debugging
  Serial.print("Pixel MAC Address: ");
  Serial.println(getMacAddress());
  Serial.print("WiFi Channel: ");
  Serial.println(channel);

  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return false;
  }

  // Register receive callback
  esp_now_register_recv_cb(onDataRecv);

  // Register send callback (for discovery responses)
  esp_now_register_send_cb(onDataSent);

  // Add broadcast peer so pixel can send responses
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, BROADCAST_MAC, 6);
  peerInfo.channel = channel;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Warning: Failed to add broadcast peer (sending disabled)");
    // Continue anyway - receiving still works
  }

  Serial.println("ESP-NOW receiver initialized (with send capability)");
  return true;
}

// Initialize ESP-NOW in sender mode (for master)
// Also registers receive callback so master can receive discovery responses
bool ESPNowComm::initSender(uint8_t channel) {
  // Set device as a Wi-Fi Station
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  // Set WiFi channel
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  // Print MAC address for debugging
  Serial.print("Master MAC Address: ");
  Serial.println(getMacAddress());
  Serial.print("WiFi Channel: ");
  Serial.println(channel);

  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return false;
  }

  // Register send callback
  esp_now_register_send_cb(onDataSent);

  // Register receive callback (for discovery responses from pixels)
  esp_now_register_recv_cb(onDataRecv);

  // Add broadcast peer
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, BROADCAST_MAC, 6);
  peerInfo.channel = channel;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add broadcast peer");
    return false;
  }

  Serial.println("ESP-NOW sender initialized (with receive capability)");
  return true;
}

// Send a packet (master only)
bool ESPNowComm::sendPacket(const ESPNowPacket* packet, size_t len) {
  esp_err_t result = esp_now_send(BROADCAST_MAC, (uint8_t*)packet, len);
  return (result == ESP_OK);
}

// Set callback for received packets
void ESPNowComm::setReceiveCallback(PacketReceivedCallback callback) {
  receiveCallback = callback;
}

// Get MAC address as string
String ESPNowComm::getMacAddress() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(macStr);
}

// ESP-NOW receive callback
void ESPNowComm::onDataRecv(const uint8_t* mac, const uint8_t* data, int len) {
  if (receiveCallback != nullptr && len <= sizeof(ESPNowPacket)) {
    ESPNowPacket packet;
    memcpy(&packet, data, len);
    receiveCallback(&packet, len);
  }
}

// ESP-NOW send callback
void ESPNowComm::onDataSent(const uint8_t* mac, esp_now_send_status_t status) {
  // Optional: Add logging or statistics here
  // For now, we'll keep it silent to avoid serial spam
}

