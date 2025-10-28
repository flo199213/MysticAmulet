/*
 * Includes all display functions
 *
 * @author    Florian Stäblein
 * @date      2025/01/01
 * @copyright © 2025 Florian Stäblein
 */
 
#ifndef MJPEGPLAYER_H
#define MJPEGPLAYER_H

#pragma GCC optimize("O3")

//===============================================================
// Includes
//===============================================================
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <FS.h>
#include <Adafruit_SPITFT.h>
#include "tjpgdClass.h"

//===============================================================
// Defines
//===============================================================
#define READ_BUFFER_SIZE      2048
#define MJPEG_BUFFER_SIZE     28800  // Width * Height: (240 * 240 * 2 / 4)

//===============================================================
// Class for playing mjpeg files
//===============================================================
class MjpegPlayer
{
  public:
    // Constructor
    MjpegPlayer();

    // Initializes mjpeg player
    bool Begin(Adafruit_SPITFT *tft);

    // Sets a new video up
    bool Setup(File input);

    // Reads the mjpeg buffer from file
    bool ReadMjpegBuf();

    // Draws the jpg on the display
    bool DrawJpg();

  private:
    // Input file
    File _input;
    
    // Display variables
    Adafruit_SPITFT* _tft;
    int32_t _tft_width = 240;
    int32_t _tft_height = 240;

    // File read buffer variables
    uint8_t* _readBuffer;
    size_t _bufferReadCount = 0;
    size_t _currentReadIndex = 0;
    bool _lastByteWas0xFF = false;

    // Mjpeg result buffer
    uint8_t* _mjpeg_buf;
    size_t _mjpeg_buf_offset = 0;

    // Decoder variables
    TJpgD _jdec;
    bool _multiTask = false;
    int32_t _out_width = 0;
    int32_t _out_height = 0;
    int32_t _off_x = 0;
    int32_t _off_y = 0;
    int32_t _jpg_x = 0;
    int32_t _jpg_y = 0;

    // RGB565 output buffer variables
    uint16_t* _out_bufs[2];
    uint16_t* _out_buf;

    // Play variables
    int32_t _remain = 0;
    uint32_t _fileindex = 0;
    
    // Reads an jpg
    static uint32_t jpgRead(TJpgD* jdec, uint8_t* buffer, uint32_t length);

    // Writes an 16 bit color to the panel buffer
    static uint32_t jpgWrite16(TJpgD* jdec, void* bitmap, TJpgD::JRECT* rectangle);

    // Writes an image row to the display
    static uint32_t jpgWriteRow(TJpgD* jdec, uint32_t y, uint32_t h);
};

//===============================================================
// Global variables
//===============================================================
extern MjpegPlayer Player;

#endif
