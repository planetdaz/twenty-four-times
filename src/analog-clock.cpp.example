// Sketch to draw an analogue clock on the screen
// This uses anti-aliased drawing functions that are built into TFT_eSPI

// Anti-aliased lines can be drawn with sub-pixel resolution and permit lines to be
// drawn with less jaggedness.

// Based on a sketch by DavyLandman:
// https://github.com/Bodmer/TFT_eSPI/issues/905


#define WIFI_SSID      "Frontier5664"
#define WIFI_PASSWORD  "8854950591"

// ===== BOARD-SPECIFIC CONFIGURATION =====
#if defined(BOARD_CYD_RESISTIVE)
  // ESP32-2432S028R with XPT2046 Resistive Touch (SPI)
  #define TFT_BACKLIGHT 21
  #define BOARD_NAME "ESP32-2432S028R (Resistive)"

#elif defined(BOARD_CYD_CAPACITIVE)
  // JC2432W328C with CST816S Capacitive Touch (I2C)
  #define TFT_BACKLIGHT 27
  #define BOARD_NAME "JC2432W328C (Capacitive)"

#else
  #error "No board defined! Use -DBOARD_CYD_RESISTIVE or -DBOARD_CYD_CAPACITIVE"
#endif

#include <Arduino.h>
#include <TFT_eSPI.h> // Master copy here: https://github.com/Bodmer/TFT_eSPI
#include <SPI.h>

#include "NotoSansBold15.h"

TFT_eSPI tft = TFT_eSPI();  // Invoke library, pins defined in User_Setup.h
TFT_eSprite face = TFT_eSprite(&tft);

// Display dimensions
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240

#define CLOCK_FG   TFT_SKYBLUE
#define CLOCK_BG   TFT_NAVY
#define SECCOND_FG TFT_RED
#define LABEL_FG   TFT_GOLD

#define CLOCK_R       120.0f // Clock face radius (float type) - 240px diameter
#define H_HAND_LENGTH CLOCK_R/2.0f
#define M_HAND_LENGTH CLOCK_R/1.4f
#define S_HAND_LENGTH CLOCK_R/1.3f

#define FACE_W CLOCK_R * 2 + 1
#define FACE_H CLOCK_R * 2 + 1

// Calculate 1 second increment angles. Hours and minute hand angles
// change every second so we see smooth sub-pixel movement
#define SECOND_ANGLE 360.0 / 60.0
#define MINUTE_ANGLE SECOND_ANGLE / 60.0
#define HOUR_ANGLE   MINUTE_ANGLE / 12.0

// Sprite width and height
#define FACE_W CLOCK_R * 2 + 1
#define FACE_H CLOCK_R * 2 + 1

// Time h:m:s
uint8_t h = 0, m = 0, s = 0;

float time_secs = h * 3600 + m * 60 + s;

// Forward declarations
void getCoord(int16_t x, int16_t y, float *xp, float *yp, int16_t r, float a);
static void renderFace(float t);
void syncTime();

// Load header after time_secs global variable has been created so it is in scope
#include "NTP_Time.h" // Attached to this sketch, see that tab for library needs

// Time for next tick
uint32_t targetTime = 0;

// =========================================================================
// Setup
// =========================================================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n========== BOARD DEBUG INFO ==========");
  
  // ESP32 Chip Info
  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);
  Serial.printf("Chip Model: ESP32 with %d CPU cores\n", chip_info.cores);
  Serial.printf("WiFi%s%s\n", 
                (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
                (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");
  Serial.printf("Silicon revision: %d\n", chip_info.revision);
  Serial.printf("Flash size: %dMB %s\n", 
                spi_flash_get_chip_size() / (1024 * 1024),
                (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");
  
  // Memory Info
  Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
  Serial.printf("Total heap: %d bytes\n", ESP.getHeapSize());
  Serial.printf("Free PSRAM: %d bytes\n", ESP.getFreePsram());
  Serial.printf("Total PSRAM: %d bytes\n", ESP.getPsramSize());
  
  // Display info
  Serial.println("\n--- Display Configuration ---");
  Serial.printf("Board: %s\n", BOARD_NAME);
  Serial.printf("TFT_MISO: %d\n", TFT_MISO);
  Serial.printf("TFT_MOSI: %d\n", TFT_MOSI);
  Serial.printf("TFT_SCLK: %d\n", TFT_SCLK);
  Serial.printf("TFT_CS: %d\n", TFT_CS);
  Serial.printf("TFT_DC: %d\n", TFT_DC);
  Serial.printf("TFT_BACKLIGHT: %d\n", TFT_BACKLIGHT);
  Serial.printf("Screen dimensions: %d x %d\n", SCREEN_WIDTH, SCREEN_HEIGHT);
  Serial.printf("Sprite size: %d x %d\n", (int)FACE_W, (int)FACE_H);
  Serial.printf("Sprite memory needed: ~%d bytes\n", (int)(FACE_W * FACE_H / 2));
  
  Serial.println("======================================\n");
  Serial.println("Booting...");

  // Initialize backlight
  pinMode(TFT_BACKLIGHT, OUTPUT);
  digitalWrite(TFT_BACKLIGHT, HIGH);

  // Initialise the screen
  tft.init();

  // Ideally set orientation for good viewing angle range because
  // the anti-aliasing effectiveness varies with screen viewing angle
  // Usually this is when screen ribbon connector is at the bottom
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  // Create the clock face sprite
  // Use 8-bit color depth to fit the large 241x241 sprite in memory
  face.setColorDepth(8);
  face.createSprite(FACE_W, FACE_H);

  // Only 1 font used in the sprite, so can remain loaded
  face.loadFont(NotoSansBold15);

  // Draw the whole clock - NTP time not available yet
  renderFace(time_secs);

  targetTime = millis() + 100;
}

// =========================================================================
// Loop
// =========================================================================
void loop() {
  // Update time periodically
  if (targetTime < millis()) {

    // Update next tick time in 100 milliseconds for smooth movement
    targetTime = millis() + 100;

    // Increment time by 100 milliseconds
    time_secs += 0.100;

    // Midnight roll-over
    if (time_secs >= (60 * 60 * 24)) time_secs = 0;

    // All graphics are drawn in sprite to stop flicker
    renderFace(time_secs);

    // Request time from NTP server and synchronise the local clock
    // (clock may pause since this may take >100ms)
    syncTime();
  }
}

// =========================================================================
// Draw the clock face in the sprite
// =========================================================================
static void renderFace(float t) {
  float h_angle = t * HOUR_ANGLE;
  float m_angle = t * MINUTE_ANGLE;
  float s_angle = t * SECOND_ANGLE;

  // The face is completely redrawn - this can be done quickly
  face.fillSprite(TFT_BLACK);

  // Draw the face circle
  face.fillSmoothCircle( CLOCK_R, CLOCK_R, CLOCK_R, CLOCK_BG );

  // Set text datum to middle centre and the colour
  face.setTextDatum(MC_DATUM);

  // The background colour will be read during the character rendering
  face.setTextColor(CLOCK_FG, CLOCK_BG);

  // Text offset adjustment (scaled for larger clock)
  constexpr uint32_t dialOffset = CLOCK_R - 18;

  float xp = 0.0, yp = 0.0; // Use float pixel position for smooth AA motion

  // Draw digits around clock perimeter
  for (uint32_t h = 1; h <= 12; h++) {
    getCoord(CLOCK_R, CLOCK_R, &xp, &yp, dialOffset, h * 360.0 / 12);
    face.drawNumber(h, xp, 2 + yp);
  }

  // Display digital time and date
  face.setTextColor(LABEL_FG, CLOCK_BG);
  
  // Calculate 12-hour format time
  time_t local = TIMEZONE.toLocal(now(), &tz1_Code);
  int h12 = hour(local);
  int m = minute(local);
  bool isPM = h12 >= 12;
  if (h12 > 12) h12 -= 12;
  if (h12 == 0) h12 = 12;
  
  // Format time string
  String timeStr = "";
  if (h12 < 10) timeStr += " ";
  timeStr += h12;
  timeStr += ":";
  if (m < 10) timeStr += "0";
  timeStr += m;
  timeStr += " ";
  timeStr += isPM ? "PM" : "AM";
  
  // Format date string
  String dateStr = "";
  dateStr += monthShortStr(month(local));
  dateStr += " ";
  dateStr += day(local);
  dateStr += ", ";
  dateStr += year(local);
  
  // Center the clock sprite on the 320x240 display
  int16_t spriteX = (SCREEN_WIDTH - FACE_W) / 2;
  int16_t spriteY = (SCREEN_HEIGHT - FACE_H) / 2;
  
  // Draw second hand (scaled for larger clock)
  getCoord(CLOCK_R, CLOCK_R, &xp, &yp, S_HAND_LENGTH, s_angle);
  face.drawWedgeLine(CLOCK_R, CLOCK_R, xp, yp, 5.0, 2.0, SECCOND_FG);
  
  // Draw minute hand (scaled widths for larger clock)
  getCoord(CLOCK_R, CLOCK_R, &xp, &yp, M_HAND_LENGTH, m_angle);
  face.drawWideLine(CLOCK_R, CLOCK_R, xp, yp, 10.0f, CLOCK_FG);
//  face.drawWideLine(CLOCK_R, CLOCK_R, xp, yp, 4.0f, CLOCK_BG);
  
  // Draw hour hand (scaled widths for larger clock)
  getCoord(CLOCK_R, CLOCK_R, &xp, &yp, H_HAND_LENGTH, h_angle);
  face.drawWideLine(CLOCK_R, CLOCK_R, xp, yp, 12.0f, CLOCK_FG);
  face.drawWideLine(CLOCK_R, CLOCK_R, xp, yp, 4.0f, TFT_TRANSPARENT);
  
  // Draw the central pivot circle (scaled for larger clock)
  face.fillSmoothCircle(CLOCK_R, CLOCK_R, 8, CLOCK_FG);
  
  face.drawString(timeStr, CLOCK_R, CLOCK_R * 0.70);
  face.drawString(dateStr, CLOCK_R, CLOCK_R * 0.85);
  
  face.pushSprite(spriteX, spriteY, TFT_TRANSPARENT);
}

// =========================================================================
// Get coordinates of end of a line, pivot at x,y, length r, angle a
// =========================================================================
// Coordinates are returned to caller via the xp and yp pointers
#define DEG2RAD 0.0174532925
void getCoord(int16_t x, int16_t y, float *xp, float *yp, int16_t r, float a)
{
  float sx1 = cos( (a - 90) * DEG2RAD);
  float sy1 = sin( (a - 90) * DEG2RAD);
  *xp =  sx1 * r + x;
  *yp =  sy1 * r + y;
}