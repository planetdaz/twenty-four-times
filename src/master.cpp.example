#include <Arduino.h>
#include <ESPNowComm.h>

// ===== MASTER CONTROLLER =====
// This firmware broadcasts synchronized commands to all pixels via ESP-NOW
// For initial testing, it sends simple test patterns

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
  uint16_t duration_ms;
};

// Pattern 1: All hands pointing up (0°)
TestPattern pattern_all_up = {
  "All Up",
  {
    {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0},
    {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0},
    {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0},
    {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}
  },
  TRANSITION_ELASTIC,
  3000
};

// Pattern 2: All hands pointing right (90°)
TestPattern pattern_all_right = {
  "All Right",
  {
    {90, 90, 90}, {90, 90, 90}, {90, 90, 90}, {90, 90, 90}, {90, 90, 90}, {90, 90, 90},
    {90, 90, 90}, {90, 90, 90}, {90, 90, 90}, {90, 90, 90}, {90, 90, 90}, {90, 90, 90},
    {90, 90, 90}, {90, 90, 90}, {90, 90, 90}, {90, 90, 90}, {90, 90, 90}, {90, 90, 90},
    {90, 90, 90}, {90, 90, 90}, {90, 90, 90}, {90, 90, 90}, {90, 90, 90}, {90, 90, 90}
  },
  TRANSITION_EASE_IN_OUT,
  2000
};

// Pattern 3: All hands pointing down (180°)
TestPattern pattern_all_down = {
  "All Down",
  {
    {180, 180, 180}, {180, 180, 180}, {180, 180, 180}, {180, 180, 180}, {180, 180, 180}, {180, 180, 180},
    {180, 180, 180}, {180, 180, 180}, {180, 180, 180}, {180, 180, 180}, {180, 180, 180}, {180, 180, 180},
    {180, 180, 180}, {180, 180, 180}, {180, 180, 180}, {180, 180, 180}, {180, 180, 180}, {180, 180, 180},
    {180, 180, 180}, {180, 180, 180}, {180, 180, 180}, {180, 180, 180}, {180, 180, 180}, {180, 180, 180}
  },
  TRANSITION_LINEAR,
  2500
};

// Pattern 4: All hands pointing left (270°)
TestPattern pattern_all_left = {
  "All Left",
  {
    {270, 270, 270}, {270, 270, 270}, {270, 270, 270}, {270, 270, 270}, {270, 270, 270}, {270, 270, 270},
    {270, 270, 270}, {270, 270, 270}, {270, 270, 270}, {270, 270, 270}, {270, 270, 270}, {270, 270, 270},
    {270, 270, 270}, {270, 270, 270}, {270, 270, 270}, {270, 270, 270}, {270, 270, 270}, {270, 270, 270},
    {270, 270, 270}, {270, 270, 270}, {270, 270, 270}, {270, 270, 270}, {270, 270, 270}, {270, 270, 270}
  },
  TRANSITION_ELASTIC,
  3500
};

// Pattern 5: Staggered - each pixel different
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
  TRANSITION_EASE_IN_OUT,
  4000
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

void sendPattern(TestPattern* pattern) {
  ESPNowPacket packet;
  packet.angleCmd.command = CMD_SET_ANGLES;
  packet.angleCmd.transition = pattern->transition;
  packet.angleCmd.duration_ms = pattern->duration_ms;
  
  // Set angles for all pixels
  for (int i = 0; i < MAX_PIXELS; i++) {
    packet.angleCmd.setPixelAngles(i, pattern->angles[i][0], pattern->angles[i][1], pattern->angles[i][2]);
  }
  
  // Send the packet
  if (ESPNowComm::sendPacket(&packet, sizeof(AngleCommandPacket))) {
    Serial.print("Sent pattern: ");
    Serial.print(pattern->name);
    Serial.print(" (duration: ");
    Serial.print(pattern->duration_ms);
    Serial.println("ms)");
  } else {
    Serial.println("Failed to send packet!");
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n========== MASTER CONTROLLER ==========");
  Serial.println("Twenty-Four Times - ESP-NOW Master");
  Serial.println("=======================================\n");
  
  // Initialize ESP-NOW in sender mode
  if (ESPNowComm::initSender(ESPNOW_CHANNEL)) {
    Serial.println("ESP-NOW sender initialized!");
    Serial.println("Broadcasting test patterns every 5 seconds...\n");
  } else {
    Serial.println("ESP-NOW initialization failed!");
    while (1) delay(1000);  // Halt
  }
  
  lastCommandTime = millis();
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

