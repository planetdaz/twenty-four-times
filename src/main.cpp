
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

// ---- Transition/Easing Types ----
enum EasingType {
  EASING_LINEAR = 0,
  EASING_EASE_IN_OUT = 1,
  EASING_ELASTIC = 2,
  EASING_BOUNCE = 3,
  EASING_BACK_IN = 4,
  EASING_BACK_OUT = 5,
  EASING_BACK_IN_OUT = 6
};

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
  EasingType easing;
  bool isActive;
};

TransitionState transition = {0, 0.0, EASING_ELASTIC, false};

// Timing
unsigned long lastUpdateTime = 0;
unsigned long lastTransitionTime = 0;
const unsigned long TRANSITION_INTERVAL = 5000;  // 5 seconds between transitions
bool firstTransition = true;  // Flag for initial boot transition

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
};

const int paletteSize = sizeof(colorPalette) / sizeof(ColorPair);

// ---- Helper Functions ----

// Get a random angle from the allowed set: 0, 90, 180, 270
float getRandomAngle() {
  const float angles[] = {0.0, 90.0, 180.0, 270.0};
  return angles[random(4)];
}

// Get a random color pair from the palette
int getRandomColorPair() {
  return random(paletteSize);
}

// Get a random easing type
EasingType getRandomEasing() {
  return (EasingType)random(7);  // 0-6 for the 7 easing types
}

// Get a random duration between 0.5 and 6.0 seconds
float getRandomDuration() {
  return 0.5 + (random(551) / 100.0);  // 0.5 to 6.0 seconds
}

// Get a random opacity from allowed set: 0 (transparent), 50 (faint), 255 (opaque)
uint8_t getRandomOpacity() {
  const uint8_t opacities[] = {0, 50, 255};
  return opacities[random(3)];
}

// Get easing name for debug output
const char* getEasingName(EasingType easing) {
  switch (easing) {
    case EASING_LINEAR: return "Linear";
    case EASING_EASE_IN_OUT: return "Ease-in-out";
    case EASING_ELASTIC: return "Elastic";
    case EASING_BOUNCE: return "Bounce";
    case EASING_BACK_IN: return "Back-in";
    case EASING_BACK_OUT: return "Back-out";
    case EASING_BACK_IN_OUT: return "Back-in-out";
    default: return "Unknown";
  }
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
float applyEasing(float t, EasingType easing) {
  switch (easing) {
    case EASING_LINEAR: return easeLinear(t);
    case EASING_EASE_IN_OUT: return easeInOut(t);
    case EASING_ELASTIC: return easeElasticOut(t);
    case EASING_BOUNCE: return easeBounceOut(t);
    case EASING_BACK_IN: return easeBackIn(t);
    case EASING_BACK_OUT: return easeBackOut(t);
    case EASING_BACK_IN_OUT: return easeBackInOut(t);
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
                     float durationSeconds, EasingType easing) {
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

  // Set up hand 1
  hand1.startAngle = hand1.currentAngle;
  hand1.targetAngle = target1;
  hand1.direction = (random(2) == 0) ? 1 : -1;  // Random CW or CCW
  // If start == target, do a full 360° rotation in the chosen direction
  if (abs(hand1.currentAngle - target1) < 0.1) {
    hand1.targetAngle = hand1.currentAngle + (360.0 * hand1.direction);
  }

  // Set up hand 2
  hand2.startAngle = hand2.currentAngle;
  hand2.targetAngle = target2;
  hand2.direction = (random(2) == 0) ? 1 : -1;
  if (abs(hand2.currentAngle - target2) < 0.1) {
    hand2.targetAngle = hand2.currentAngle + (360.0 * hand2.direction);
  }

  // Set up hand 3
  hand3.startAngle = hand3.currentAngle;
  hand3.targetAngle = target3;
  hand3.direction = (random(2) == 0) ? 1 : -1;
  if (abs(hand3.currentAngle - target3) < 0.1) {
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
  // For normal transitions, normalize and apply direction
  if (abs(diff) < 359.0) {  // Not a full rotation
    // Normalize to 0-360 range
    while (diff < 0) diff += 360.0;
    while (diff >= 360.0) diff -= 360.0;

    // If going CCW, take the longer path
    if (hand.direction < 0) {
      diff = diff - 360.0;  // Go the other way
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

  // Initialize timing
  lastUpdateTime = millis();
  lastTransitionTime = millis();

  // Initialize random seed
  randomSeed(analogRead(0));

  Serial.println("\n=== Boot Sequence ===");
  Serial.println("Starting with all hands at 0°, opacity 0 (invisible)");
  Serial.println("Black background, white foreground");
  Serial.println("After 5 seconds: fade in to random angles");
  Serial.println("Then: random transitions every 5 seconds");
  Serial.println("  - Normal: random angles, opacity 255");
  Serial.println("  - NOP (< 1 in 5): all hands at 225°, opacity 50");
  Serial.println("  - Color change (1 in 5): random palette with good contrast\n");
}

void loop() {
  unsigned long currentTime = millis();

  // Update hand angles based on transition
  if (transition.isActive) {
    // Calculate elapsed time in seconds
    float elapsed = (currentTime - transition.startTime) / 1000.0;

    // Calculate progress (0.0 to 1.0)
    float t = elapsed / transition.duration;

    if (t >= 1.0) {
      // Transition complete
      hand1.currentAngle = hand1.targetAngle;
      hand2.currentAngle = hand2.targetAngle;
      hand3.currentAngle = hand3.targetAngle;
      opacity.current = opacity.target;
      colors.currentBg = colors.targetBg;
      colors.currentFg = colors.targetFg;
      transition.isActive = false;

      // Start the 5-second timer AFTER transition completes
      lastTransitionTime = currentTime;
    } else {
      // Update all hands with same progress value
      updateHandAngle(hand1, t);
      updateHandAngle(hand2, t);
      updateHandAngle(hand3, t);
      updateOpacity(t);
      updateColors(t);
    }
  }

  // ---- Random Demo Loop: Start new transition 5 seconds after previous one completes ----
  if (!transition.isActive && (currentTime - lastTransitionTime >= TRANSITION_INTERVAL)) {
    float target1, target2, target3;
    uint8_t targetOpacity;
    uint16_t targetBg, targetFg;
    float duration;
    EasingType easing;
    bool isNOP = false;
    bool colorChange = false;

    // Decide if colors should change (1 in 5 chance)
    if (random(5) == 0) {
      int paletteIndex = getRandomColorPair();
      targetBg = colorPalette[paletteIndex].bg;
      targetFg = colorPalette[paletteIndex].fg;
      colorChange = true;
    } else {
      // Keep current colors
      targetBg = colors.currentBg;
      targetFg = colors.currentFg;
    }

    if (firstTransition) {
      // First transition after boot: fade in to random angles at full opacity
      target1 = getRandomAngle();
      target2 = getRandomAngle();
      target3 = getRandomAngle();
      targetOpacity = 255;
      duration = getRandomDuration();
      easing = getRandomEasing();
      firstTransition = false;

      Serial.println("\n=== BOOT: Fading in ===");
    } else {
      // Random chance (less than 1 in 5) for NOP state
      if (random(5) == 0) {
        // NOP state: all hands at 225°, opacity 50
        target1 = 225.0;
        target2 = 225.0;
        target3 = 225.0;
        targetOpacity = 50;
        duration = getRandomDuration();
        easing = getRandomEasing();
        isNOP = true;

        Serial.println("\n=== NOP State ===");
      } else {
        // Normal random transition at full opacity
        target1 = getRandomAngle();
        target2 = getRandomAngle();
        target3 = getRandomAngle();
        targetOpacity = 255;
        duration = getRandomDuration();
        easing = getRandomEasing();
      }
    }

    // Start the transition
    startTransition(target1, target2, target3, targetOpacity, targetBg, targetFg, duration, easing);

    // Print debug info (if not already printed above)
    if (!firstTransition && !isNOP) {
      Serial.println("\n=== New Transition ===");
    }
    Serial.print("Easing: ");
    Serial.println(getEasingName(easing));
    Serial.print("Duration: ");
    Serial.print(duration, 2);
    Serial.println(" seconds");
    Serial.print("Opacity: ");
    Serial.print(opacity.start);
    Serial.print(" -> ");
    Serial.println(targetOpacity);

    // Print color change info if colors are changing
    if (colorChange) {
      Serial.print("Colors: ");
      // Find which palette entry this is
      for (int i = 0; i < paletteSize; i++) {
        if (colorPalette[i].bg == targetBg && colorPalette[i].fg == targetFg) {
          Serial.println(colorPalette[i].name);
          break;
        }
      }
    }

    Serial.print("Hand 1: ");
    Serial.print(hand1.startAngle, 0);
    Serial.print("° -> ");
    Serial.print(target1, 0);
    Serial.print("° (");
    Serial.print(hand1.direction > 0 ? "CW" : "CCW");
    Serial.println(")");
    Serial.print("Hand 2: ");
    Serial.print(hand2.startAngle, 0);
    Serial.print("° -> ");
    Serial.print(target2, 0);
    Serial.print("° (");
    Serial.print(hand2.direction > 0 ? "CW" : "CCW");
    Serial.println(")");
    Serial.print("Hand 3: ");
    Serial.print(hand3.startAngle, 0);
    Serial.print("° -> ");
    Serial.print(target3, 0);
    Serial.print("° (");
    Serial.print(hand3.direction > 0 ? "CW" : "CCW");
    Serial.println(")");
  }

  // Clear canvas with current background color
  canvas.fillScreen(colors.currentBg);

  // Optional: Draw reference circle to show the max radius
  // canvas.drawCircle(CENTER_X, CENTER_Y, MAX_RADIUS - 1, tft.color565(200, 200, 200));

  // Draw the three clock hands with opacity blending
  // Blend foreground color with background based on opacity
  uint16_t handColor = blendColor(colors.currentBg, colors.currentFg, opacity.current);

  // Draw hands 1 and 2 with normal thickness
  drawHand(CENTER_X, CENTER_Y, hand1.currentAngle, HAND_LENGTH_NORMAL, HAND_THICKNESS_NORMAL, handColor);
  drawHand(CENTER_X, CENTER_Y, hand2.currentAngle, HAND_LENGTH_NORMAL, HAND_THICKNESS_NORMAL, handColor);

  // Draw hand 3 with thin thickness
  drawHand(CENTER_X, CENTER_Y, hand3.currentAngle, HAND_LENGTH_NORMAL, HAND_THICKNESS_THIN, handColor);

  // Draw center dot (always full opacity foreground color)
  canvas.fillCircle(CENTER_X, CENTER_Y, 4, colors.currentFg);

  // Present frame to display
  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), DISPLAY_WIDTH, DISPLAY_HEIGHT);

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
