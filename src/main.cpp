
#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_GC9A01A.h>

// Proof of concept: Three rotating clock hands on a 240x240 circular display
// Based on the twenty-four-times simulation

// 240x240 RGB565 buffer (~115 KB)
GFXcanvas16 canvas(240, 240);

// ---- TFT pin names ----
#define tft_rst  4   // D2 / GPIO4 / pin 3
#define tft_cs   5   // D3 / GPIO5 / pin 4
#define tft_dc   6   // D4 / GPIO6 / pin 5
#define tft_scl  8   // D8 / GPIO8 / pin 9
#define tft_sda  10  // D10 / GPIO10 / pin 11

Adafruit_GC9A01A tft(tft_cs, tft_dc, tft_rst);

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

// Hand angles (in degrees, 0 = up/north, increases clockwise)
float hand1Angle = 0.0;
float hand2Angle = 120.0;
float hand3Angle = 240.0;

// Rotation speeds (degrees per frame)
const float HAND1_SPEED = 12;   // Fastest
const float HAND2_SPEED = 0.8;   // Medium
const float HAND3_SPEED = 0.5;   // Slowest

// FPS tracking
unsigned long fpsLastTime = 0;
unsigned long fpsFrames = 0;

// ---- Helper functions ----

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
  canvas.fillTriangle(x1, y1, x2, y2, x3, y3, color);
  canvas.fillTriangle(x2, y2, x3, y3, x4, y4, color);

  // Add rounded caps at both ends
  canvas.fillCircle(cx, cy, (int)halfThick, color);      // Base cap
  canvas.fillCircle(endX, endY, (int)halfThick, color);  // Tip cap
}

void setup() {
  Serial.begin(115200);
  delay(200);

  // ---- Memory Statistics ----
  Serial.println("\n========== MEMORY DEBUG INFO ==========");
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
  
  Serial.print("Free PSRAM: ");
  Serial.print(ESP.getFreePsram());
  Serial.println(" bytes");
  
  Serial.print("Total PSRAM: ");
  Serial.print(ESP.getPsramSize());
  Serial.println(" bytes");
  
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
  // ESP32-C3 doesn't have default SPI pins, so we specify them explicitly
  // SPI.begin(SCK, MISO, MOSI, SS) - MISO=-1 since we don't need it for write-only TFT
  SPI.begin(tft_scl, -1, tft_sda);

  // ---- TFT ----
  Serial.println("Initializing TFT...");
  tft.begin();
  tft.setRotation(0);
  Serial.println("TFT initialized!");

  Serial.print("Free heap after TFT init: ");
  Serial.print(ESP.getFreeHeap());
  Serial.println(" bytes");

  Serial.println("\nNote: Canvas buffer (115,200 bytes) is allocated statically,");
  Serial.println("      not from heap. This is good - no heap fragmentation!");

  // Clear canvas to white
  canvas.fillScreen(GC9A01A_WHITE);

  Serial.println("\nSetup complete!");
}

void loop() {
  // Clear canvas with white background (like the simulation)
  canvas.fillScreen(GC9A01A_WHITE);

  // Optional: Draw reference circle to show the max radius
  // canvas.drawCircle(CENTER_X, CENTER_Y, MAX_RADIUS - 1, tft.color565(200, 200, 200));

  // Draw the three clock hands
  // Hand color: black
  uint16_t handColor = GC9A01A_BLACK;

  // Draw hands 1 and 2 with normal thickness
  drawHand(CENTER_X, CENTER_Y, hand1Angle, HAND_LENGTH_NORMAL, HAND_THICKNESS_NORMAL, handColor);
  drawHand(CENTER_X, CENTER_Y, hand2Angle, HAND_LENGTH_NORMAL, HAND_THICKNESS_NORMAL, handColor);

  // Draw hand 3 with thin thickness
  drawHand(CENTER_X, CENTER_Y, hand3Angle, HAND_LENGTH_NORMAL, HAND_THICKNESS_THIN, handColor);

  // Draw center dot
  canvas.fillCircle(CENTER_X, CENTER_Y, 4, handColor);

  // Present frame to display
  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), DISPLAY_WIDTH, DISPLAY_HEIGHT);

  // Update hand angles
  hand1Angle += HAND1_SPEED;
  hand2Angle += HAND2_SPEED;
  hand3Angle += HAND3_SPEED;

  // Keep angles in 0-360 range
  if (hand1Angle >= 360.0) hand1Angle -= 360.0;
  if (hand2Angle >= 360.0) hand2Angle -= 360.0;
  if (hand3Angle >= 360.0) hand3Angle -= 360.0;

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
