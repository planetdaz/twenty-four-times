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
const float HAND_THICKNESS_NORMAL = 3.0;
const float HAND_THICKNESS_THIN = 2.4;  // 80% of normal

// Hand angles (in degrees, 0 = up/north, increases clockwise)
float hand1Angle = 0.0;
float hand2Angle = 120.0;
float hand3Angle = 240.0;

// Rotation speeds (degrees per frame)
const float HAND1_SPEED = 1.2;   // Fastest
const float HAND2_SPEED = 0.8;   // Medium
const float HAND3_SPEED = 0.5;   // Slowest

// FPS tracking
unsigned long fpsLastTime = 0;
unsigned long fpsFrames = 0;

// ---- Helper functions ----

// Draw a clock hand from center to angle
// angle: degrees, 0 = up, clockwise positive
// length: length of hand in pixels
// thickness: line thickness
// color: RGB565 color
void drawHand(int cx, int cy, float angleDeg, float length, float thickness, uint16_t color) {
  // Convert angle to radians (subtract 90 to make 0 degrees point up)
  float angleRad = (angleDeg - 90.0) * PI / 180.0;

  // Calculate end point
  int x2 = cx + cos(angleRad) * length;
  int y2 = cy + sin(angleRad) * length;

  // Draw thick line by drawing multiple parallel lines
  // For round cap effect, we'll draw the main line and add circles at the end
  int halfThickness = (int)(thickness / 2.0);

  // Draw the main line with thickness
  for (int offset = -halfThickness; offset <= halfThickness; offset++) {
    // Calculate perpendicular offset
    float perpAngle = angleRad + PI / 2.0;
    int cx_offset = cx + cos(perpAngle) * offset;
    int cy_offset = cy + sin(perpAngle) * offset;
    int x2_offset = x2 + cos(perpAngle) * offset;
    int y2_offset = y2 + sin(perpAngle) * offset;

    canvas.drawLine(cx_offset, cy_offset, x2_offset, y2_offset, color);
  }

  // Draw round caps
  canvas.fillCircle(cx, cy, halfThickness, color);
  canvas.fillCircle(x2, y2, halfThickness, color);
}

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println("Twenty-Four Times - Clock Hands Proof of Concept");
  Serial.print("Max radius: ");
  Serial.print(MAX_RADIUS);
  Serial.println(" pixels");
  Serial.print("Hand length: ");
  Serial.print(HAND_LENGTH_NORMAL);
  Serial.println(" pixels");

  // ---- SPI ----
  SPI.begin(tft_scl, -1, tft_sda);

  // ---- TFT ----
  tft.begin();
  tft.setRotation(0);
}

void loop() {
  // Clear canvas with white background (like the simulation)
  canvas.fillScreen(GC9A01A_WHITE);

  // Optional: Draw reference circle to show the max radius
  canvas.drawCircle(CENTER_X, CENTER_Y, MAX_RADIUS - 1, tft.color565(200, 200, 200));

  // Draw the three clock hands
  // Hand color: black (#111 from simulation)
  uint16_t handColor = GC9A01A_BLACK;

  // Draw hands 1 and 2 with normal thickness
  drawHand(CENTER_X, CENTER_Y, hand1Angle, HAND_LENGTH_NORMAL, HAND_THICKNESS_NORMAL, handColor);
  drawHand(CENTER_X, CENTER_Y, hand2Angle, HAND_LENGTH_NORMAL, HAND_THICKNESS_NORMAL, handColor);

  // Draw hand 3 with thin thickness
  drawHand(CENTER_X, CENTER_Y, hand3Angle, HAND_LENGTH_NORMAL, HAND_THICKNESS_THIN, handColor);

  // Draw center dot
  canvas.fillCircle(CENTER_X, CENTER_Y, 3, GC9A01A_BLACK);

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
