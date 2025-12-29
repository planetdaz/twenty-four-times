#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_GC9A01A.h>

// Just a test program to put an animation on the display

// 240x240 RGB565 buffer (~115 KB)
GFXcanvas16 canvas(240, 240);

// ---- LED ----
#define LED_PIN 2   // D0 / GPIO2 / physical pin 1

// ---- TFT pin names ----
#define tft_rst  4   // D2 / GPIO4 / pin 3
#define tft_cs   5   // D3 / GPIO5 / pin 4
#define tft_dc   6   // D4 / GPIO6 / pin 5
#define tft_scl  8   // D8 / GPIO8 / pin 9
#define tft_sda  10  // D10 / GPIO10 / pin 11

Adafruit_GC9A01A tft(tft_cs, tft_dc, tft_rst);

unsigned long fpsLastTime = 0;
unsigned long fpsFrames = 0;

void flashLed(uint8_t times, uint16_t delayMs) {
  for (uint8_t i = 0; i < times; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(delayMs);
    digitalWrite(LED_PIN, LOW);
    delay(delayMs);
  }
}

void setup() {
  // ---- LED sanity blink ----
  pinMode(LED_PIN, OUTPUT);
  flashLed(5, 80);

  Serial.begin(115200);
  delay(200);

  // ---- SPI ----
  SPI.begin(tft_scl, -1, tft_sda);

  // ---- TFT ----
  tft.begin();
  tft.setRotation(0);
}

void loop() {
  static float angle = 0;
  static float pulse = 0;

  const int cx = 120;
  const int cy = 120;
  const int maxR = 118;

  canvas.fillScreen(GC9A01A_BLACK);

  // ---- pulse shaping ----
  float s = (sin(pulse) + 1.0f) * 0.5f;   // 0..1
  float eased = pow(s, 3.0f);            // sharp attack, slow decay

  int baseR = maxR - 10;
  int pulseR = baseR + eased * 12;       // BIG size change
  int thickness = 2 + eased * 6;         // thick on beat

  // color brightness pulse (blue → cyan)
  uint16_t pulseColor = tft.color565(
    0,
    100 + eased * 155,
    200 + eased * 55
  );

  // ---- main pulse ring ----
  for (int w = 0; w < thickness; w++) {
    canvas.drawCircle(cx, cy, pulseR - w, pulseColor);
  }

  // ---- outer glow ----
  for (int g = 0; g < 6; g++) {
    int r = pulseR + g + 2;
    uint16_t glow = tft.color565(0, 30, 60 - g * 8);
    canvas.drawCircle(cx, cy, r, glow);
  }

  // ---- orbiting dots ----
  for (int i = 0; i < 3; i++) {
    float a = angle + i * TWO_PI / 3;
    int x = cx + cos(a) * (maxR - 14);
    int y = cy + sin(a) * (maxR - 14);
    canvas.fillCircle(x, y, 5, GC9A01A_GREEN);
  }

  // ---- center dot ----
  canvas.fillCircle(cx, cy, 4, GC9A01A_RED);

  // ---- present frame ----
  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), 240, 240);

  angle += 0.045;
  pulse += 0.06;     // slower = calmer, faster = heartbeat

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
