/*
 * MJPEG Viewer main file
 *
 * @author    Florian Stäblein
 * @date      2025/01/01
 * @copyright © 2025 Florian Stäblein
 * 
 * ==============================================================
 * 
 * Configuration ESP32-C3:
 * - Board: "ESP32C3 Dev Module"
 * - CPU Frequency: "160MHz (WiFi)"
 * - USB CDC On Boot: "Enabled"           <------------ Important!
 * - Core Debug Level: "Verbose"  <-------------------- Important! Only for Debugging
 * - Flash Size: "4Mb (32Mb)"
 * - Partition Scheme: "No OTA (1MB APP/3MB SPIFFS)"
 * - Upload Speed: "921600"
 * 
 * -> Leave everything else on default!
 * 
 * ==============================================================
 */

//===============================================================
// Includes
//===============================================================
#include <Adafruit_GFX.h>
#include <Adafruit_GC9A01A.h>
#include <Fonts/FreeSans9pt7b.h>
#include <FS.h>
#include <SPIFFS.h>
#include "MjpegPlayer.h"

//===============================================================
// Constants
//===============================================================
static const char* TAG = "main";

//===============================================================
// Defines
//===============================================================
// Pins GC9A01A round display
#define PIN_TFT_CS                10  // Chip select
#define PIN_TFT_MOSI              11  // Master out slave in    - Result from FSPI use
#define PIN_TFT_SCK               12  // Clock                  - Result from FSPI use
#define PIN_TFT_RST               8   // Reset
#define PIN_TFT_DC                13  // Data/Control

// Pins button
#define PIN_BUTTON_INPUT          5
#define PIN_BUTTON_OUTPUT         6

// Video playback defines
#define TFT_WIDTH                 240
#define TFT_HEIGHT                240

#define MJPEG_FILENAME_M          "/soul_m_240_20fps.mjpeg"
#define MJPEG_FILENAME_M_FPS      10

#define MJPEG_FILENAME_F          "/soul_f_240_20fps.mjpeg"
#define MJPEG_FILENAME_F_FPS      10

#define MJPEG_FILENAME_INTRO      "/vortex_240_15fps.mjpeg"
#define MJPEG_FILENAME_INTRO_FPS  20

//===============================================================
// Global variables
//===============================================================
// Display
Adafruit_GC9A01A* tft = NULL;

// State machine
int videoNumber = 0;
const int VideoMax = 3;

//===============================================================
// Prototypes
//===============================================================
// Draws a string centered
void DrawCenteredString(const String& text, int16_t x = 120, int16_t y = 120, uint16_t foreGroundColor = GC9A01A_WHITE, uint16_t backGroundColor = GC9A01A_BLACK);

// Shows a video on the screen
void ShowVideoBlocking(FS* fs, const char* path);

//===============================================================
// Setup method
//===============================================================
void setup()
{
  // Initialize Serial
  Serial.begin(115200);
  ESP_LOGI(TAG, "MJPEG Viewer V1.0");

  // Initialize Pins
  ESP_LOGI(TAG, "Initialize Pins");
  pinMode(PIN_BUTTON_INPUT, INPUT_PULLUP);
  pinMode(PIN_BUTTON_OUTPUT, OUTPUT);
  digitalWrite(PIN_BUTTON_OUTPUT, LOW);

  // Initialize hardware SPI (Force FSPI)
  ESP_LOGI(TAG, "Initialize SPI");
  SPIClass* fspi = new SPIClass(FSPI); // FSPI=0 for ESP32C3
  fspi->begin(PIN_TFT_SCK, -1, PIN_TFT_MOSI, PIN_TFT_CS);

  // Initialize display
  ESP_LOGI(TAG, "Initialize display");
  tft = new Adafruit_GC9A01A(fspi, PIN_TFT_DC, PIN_TFT_CS, PIN_TFT_RST);
  tft->begin();
  tft->setRotation(2);
  tft->setTextWrap(false);
  tft->setFont(&FreeSans9pt7b);

  // Initialize Mjpeg player
  ESP_LOGI(TAG, "Initialize Mjpeg player");
  if (!Player.Begin((Adafruit_SPITFT*)tft))
  {
    // Debug information on display
    ESP_LOGE(TAG, "Error: Initializing Mjpeg Player failed");
    DrawCenteredString("Mjpeg Failed");
    delay(3000);
  }

  // Initialize SPIFFS
  ESP_LOGI(TAG, "Initialize SPIFFS");
  if (!SPIFFS.begin())
  {
    Serial.println(F("ERROR: File System Mount Failed!"));
    DrawCenteredString("SPIFFS Failed");
    delay(3000);
  }

  // Show intro video
  ShowVideoBlocking(&SPIFFS, MJPEG_FILENAME_INTRO, 15);
}

//===============================================================
// Loop method
//===============================================================
void loop()
{
  // Determine fps
  unsigned long fps = videoNumber == 0 ? MJPEG_FILENAME_M_FPS : videoNumber == 1 ? MJPEG_FILENAME_F_FPS : MJPEG_FILENAME_INTRO_FPS;

  // Show video
  ShowVideoBlocking(&SPIFFS, (const char*)(videoNumber == 0 ? MJPEG_FILENAME_M : videoNumber == 1 ? MJPEG_FILENAME_F : MJPEG_FILENAME_INTRO), fps);
}

//===============================================================
// Draws a string centered
//===============================================================
void DrawCenteredString(const String& text, int16_t x, int16_t y, uint16_t foreGroundColor, uint16_t backGroundColor)
{
  // Clear screen and set text color
  tft->fillScreen(backGroundColor);
  tft->setTextColor(foreGroundColor);

  // Get text bounds
  int16_t x1, y1;
  uint16_t w, h;
  tft->getTextBounds(text, x, y, &x1, &y1, &w, &h);

  // Calculate cursor position
  int16_t x_text = x - w / 2;
  int16_t y_text = y + h / 2;
  tft->setCursor(x_text, y_text);

  // Print text
  tft->print(text);
}

//===============================================================
// Shows a video on the screen
//===============================================================
void ShowVideoBlocking(FS* fs, const char* path, unsigned long fps)
{
  File vFile = fs->open(path, FILE_READ);
  if (!vFile || vFile.isDirectory())
  {
    ESP_LOGE(TAG, "ERROR: Failed to open %s", path);
    DrawCenteredString(F("ERROR: Failed to open " MJPEG_FILENAME_M));
    return;
  }

  // Setup mjpeg player
  if (!Player.Setup(vFile))
  {
    ESP_LOGE(TAG, "ERROR: %s mot valid", path);
    DrawCenteredString(F("ERROR: " MJPEG_FILENAME_M " not valid"));
    return;
  }

  int skipped_frames = 0;
  unsigned long total_sd_mjpeg = 0;
  unsigned long total_decode_video = 0;
  unsigned long total_remain = 0;
  unsigned long start_ms = millis();
  unsigned long curr_ms = millis();

  int next_frame = 0;
  unsigned long next_frame_ms = start_ms + (++next_frame * 1000 / fps / 2);
  
  // Read video
  while (Player.ReadMjpegBuf())
  {
    total_sd_mjpeg += millis() - curr_ms;
    curr_ms = millis();

    if (millis() < next_frame_ms) // check show frame or skip frame
    {
      // Play video
      Player.DrawJpg();
      total_decode_video += millis() - curr_ms;

      int remain_ms = next_frame_ms - millis();
      if (remain_ms > 0)
      {
        total_remain += remain_ms;
        delay(remain_ms);
      }
    }
    else
    {
      ++skipped_frames;
      Serial.println(F("Skip frame"));
    }

    curr_ms = millis();
    next_frame_ms = start_ms + (++next_frame * 1000 / fps);

    // Check for new Video
    if (digitalRead(PIN_BUTTON_INPUT) == LOW)
    {
      videoNumber++;
      if (videoNumber >= VideoMax)
      {
        videoNumber = 0;
      }
      delay(500);
      break;
    }
  }
  vFile.close();

  // Print debug statistics
  int time_used = millis() - start_ms;
  int played_frames = next_frame - 1 - skipped_frames;
  float fps_actual = 1000.0 * played_frames / time_used;
  Serial.printf("Played frames: %d\n", played_frames);
  Serial.printf("Skipped frames: %d (%0.1f %%)\n", skipped_frames, 100.0 * skipped_frames / played_frames);
  Serial.printf("Time used: %d ms\n", time_used);
  Serial.printf("Expected FPS: %d\n", fps);
  Serial.printf("Actual FPS: %0.1f\n", fps_actual);
  Serial.printf("Read MJPEG: %lu ms (%0.1f %%)\n", total_sd_mjpeg, 100.0 * total_sd_mjpeg / time_used);
  Serial.printf("Play video: %lu ms (%0.1f %%)\n", total_decode_video, 100.0 * total_decode_video / time_used);
  Serial.printf("Remain: %lu ms (%0.1f %%)\n", total_remain, 100.0 * total_remain / time_used);
}
