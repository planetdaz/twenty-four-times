#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SPI.h>

// Proof of concept: Three rotating clock hands on a 240x240 circular display
// Based on the twenty-four-times simulation
// Now using TFT_eSPI for anti-aliased thick lines

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite sprite = TFT_eSprite(&tft);

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

// Get coordinates of end of a line, pivot at x,y, length r, angle a
void getCoord(float x, float y, float *xp, float *yp, float r, float a) {
  float angleRad = (a - 90.0) * DEG_TO_RAD;
  *xp = cos(angleRad) * r + x;
  *yp = sin(angleRad) * r + y;
}

// Draw a clock hand using TFT_eSPI's drawWideLine for smooth anti-aliased rendering
// angle: degrees, 0 = up, clockwise positive
// length: length of hand in pixels
// thickness: line thickness
// color: RGB565 color
void drawHand(float cx, float cy, float angleDeg, float length, float thickness, uint16_t color) {
  float x2, y2;
  getCoord(cx, cy, &x2, &y2, length, angleDeg);
  sprite.drawWideLine(cx, cy, x2, y2, thickness, color);
}

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println("Twenty-Four Times - Clock Hands Proof of Concept (TFT_eSPI)");
  Serial.print("Max radius: ");
  Serial.print(MAX_RADIUS);
  Serial.println(" pixels");
  Serial.print("Hand length: ");
  Serial.print(HAND_LENGTH_NORMAL);
  Serial.println(" pixels");

  // ---- TFT ----
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_WHITE);

  // ---- Create sprite ----
  // Use 8-bit color to reduce memory usage (240x240x1 = 57,600 bytes vs 115,200 for 16-bit)
  sprite.setColorDepth(8);
  bool spriteCreated = sprite.createSprite(DISPLAY_WIDTH, DISPLAY_HEIGHT);

  if (!spriteCreated) {
    Serial.println("ERROR: Failed to create sprite! Not enough memory.");
    Serial.print("Free heap: ");
    Serial.println(ESP.getFreeHeap());
    while(1) delay(1000);  // Halt
  }

  Serial.print("Sprite created successfully! Free heap: ");
  Serial.println(ESP.getFreeHeap());
  Serial.println("Setup complete!");
}

void loop() {
  // Clear sprite with white background (like the simulation)
  sprite.fillSprite(TFT_WHITE);

  // Optional: Draw reference circle to show the max radius
  // sprite.drawCircle(CENTER_X, CENTER_Y, MAX_RADIUS - 1, TFT_LIGHTGREY);

  // Draw the three clock hands
  // Hand color: black (#111 from simulation)
  uint16_t handColor = TFT_BLACK;

  // Draw hands 1 and 2 with normal thickness
  drawHand(CENTER_X, CENTER_Y, hand1Angle, HAND_LENGTH_NORMAL, HAND_THICKNESS_NORMAL, handColor);
  drawHand(CENTER_X, CENTER_Y, hand2Angle, HAND_LENGTH_NORMAL, HAND_THICKNESS_NORMAL, handColor);

  // Draw hand 3 with thin thickness
  drawHand(CENTER_X, CENTER_Y, hand3Angle, HAND_LENGTH_NORMAL, HAND_THICKNESS_THIN, handColor);

  // Draw center dot
  sprite.fillSmoothCircle(CENTER_X, CENTER_Y, 4, handColor);

  // Push sprite to display (no flicker!)
  sprite.pushSprite(0, 0);

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
