/*
 * Includes all mjpeg player  functions
 *
 * @author    Florian Stäblein
 * @date      2025/01/01
 * @copyright © 2025 Florian Stäblein
 */

//===============================================================
// Includes
//===============================================================
#include "MjpegPlayer.h"

//===============================================================
// Constants
//===============================================================
static const char* TAG = "mjpeg";

//===============================================================
// Global variables
//===============================================================
MjpegPlayer Player;

//===============================================================
// Constructor
//===============================================================
MjpegPlayer::MjpegPlayer()
{
}

//===============================================================
// Initializes the mjpeg player
//===============================================================
bool MjpegPlayer::Begin(Adafruit_SPITFT *tft)
{
  // Log info
  ESP_LOGI(TAG, "Begin initializing mjpeg player");

  _tft = tft;
  _tft_width = tft->width();
  _tft_height = tft->height();
  
  _mjpeg_buf = (uint8_t *)malloc(MJPEG_BUFFER_SIZE);
  if (!_mjpeg_buf)
  {
    ESP_LOGE(TAG, "mjpeg_buf malloc failed!");
    return false;
  }

  // Log info
  ESP_LOGI(TAG, "Finished initializing mjpeg player");
  return true;
}

//===============================================================
// Sets a new video up
//===============================================================
bool MjpegPlayer::Setup(File input)
{
  // Set input video file
  _input = input;

  // Reset all variables
  _mjpeg_buf_offset = 0;
  _bufferReadCount = 0;
  _currentReadIndex = 0;
  _lastByteWas0xFF = false;
  _remain = 0;
  _fileindex = 0;
  _out_width = 0;
  _out_height = 0;
  _off_x = 0;
  _off_y = 0;
  _jpg_x = 0;
  _jpg_y = 0;
  
  // Allocate read buffer
  if (!_readBuffer)
  {
    _readBuffer = (uint8_t*)malloc(READ_BUFFER_SIZE);
  }

  // Get file name
  String fileName = String(_input.name());
  
  if (!fileName.endsWith(".mjpeg") &&
    !fileName.endsWith(".enc"))
  {
    return false;
  }

  // Allocate out buffers
  for (int i = 0; i < 2; ++i)
  {
    if (!_out_bufs[i])
    {
      _out_bufs[i] = (uint16_t *)heap_caps_malloc(_tft_width * 48, MALLOC_CAP_DMA);
    }
  }

  // Set inital out buffer
  _out_buf = _out_bufs[0];

  return true;
}

//===============================================================
// Reads the mjpeg buffer from file
//===============================================================
bool MjpegPlayer::ReadMjpegBuf()
{
  bool jpegStarted = false; // 0xFFD8 found
  bool jpegEnded = false;   // 0xFFD9 found

  // Reset mjpeg decomp buffer
  _mjpeg_buf_offset = 0;

  uint32_t saveCounter = 0;
  while (saveCounter++ <= MJPEG_BUFFER_SIZE)
  {
    // Check if new data must be read
    if (_currentReadIndex >= _bufferReadCount)
    {
      _currentReadIndex = 0;

      // Read buffer with or without decryption
      _bufferReadCount = _input.read(_readBuffer, READ_BUFFER_SIZE);

      if (_bufferReadCount == 0)
      {
        // EOF
        return false;
      }
    }
    
    // Search for JPEG start
    if (_lastByteWas0xFF && _readBuffer[_currentReadIndex] == 0xD8)
    {
      jpegStarted = true;
      if (_mjpeg_buf_offset < MJPEG_BUFFER_SIZE)
      {
        _mjpeg_buf[_mjpeg_buf_offset++] = 0xFF;
      }
      else
      {
        Serial.println("MJPEG Buffer Overflow!");
        return false;
      }
    }

    // Copy data
    if (jpegStarted)
    {
      if (_mjpeg_buf_offset < MJPEG_BUFFER_SIZE)
      {
        _mjpeg_buf[_mjpeg_buf_offset++] = _readBuffer[_currentReadIndex];
      }
      else
      {
        Serial.println("MJPEG Buffer Overflow!");
        return false;
      }
    }

    // Search for JPEG end
    if (_lastByteWas0xFF && _readBuffer[_currentReadIndex] == 0xD9)
    {
      jpegEnded = true;
    }

    _lastByteWas0xFF = _readBuffer[_currentReadIndex] == 0xFF;
    _currentReadIndex++;

    if (jpegEnded)
    {
      return true;
    }
  }

  return false;
}

//===============================================================
// Draws the jpg on the display
//===============================================================
bool MjpegPlayer::DrawJpg()
{
  _fileindex = 0;
  _remain = _mjpeg_buf_offset;

  // Prepare jpeg
  TJpgD::JRESULT jres = _jdec.prepare(jpgRead, this);
  if (jres != TJpgD::JDR_OK)
  {
    Serial.printf("prepare failed! %d\r\n", jres);
    return false;
  }

  _out_width = std::min<int32_t>(_jdec.width, _tft_width);
  _jpg_x = (_tft_width - _jdec.width) >> 1;
  if (0 > _jpg_x)
  {
    _off_x = -_jpg_x;
    _jpg_x = 0;
  }
  else
  {
    _off_x = 0;
  }
  _out_height = std::min<int32_t>(_jdec.height, _tft_height);
  _jpg_y = (_tft_height - _jdec.height) >> 1;
  if (0 > _jpg_y)
  {
    _off_y = -_jpg_y;
    _jpg_y = 0;
  }
  else
  {
    _off_y = 0;
  }

  // Decompile jpeg
  jres = _jdec.decomp(jpgWrite16, jpgWriteRow);
  if (jres != TJpgD::JDR_OK)
  {
    Serial.printf("decomp failed! %d\r\n", jres);
    return false;
  }
  return true;
}


//===============================================================
// Reads an jpg and writes it to the mjpeg buffer
//===============================================================
uint32_t MjpegPlayer::jpgRead(TJpgD* jdec, uint8_t* buffer, uint32_t length)
{
  MjpegPlayer* me = (MjpegPlayer*)jdec->device;
  
  // Limit to remaining length
  length = length > me->_remain ? me->_remain : length;
  
  if (buffer)
  {
    memcpy(buffer, (const uint8_t*)me->_mjpeg_buf + me->_fileindex, length);
  }

  me->_fileindex += length;
  me->_remain -= length;
  
  return length;
}

//===============================================================
// Writes an 16 bit color to the panel buffer
//===============================================================
uint32_t MjpegPlayer::jpgWrite16(TJpgD *jdec, void *bitmap, TJpgD::JRECT *rectangle)
{
  MjpegPlayer* me = (MjpegPlayer*)jdec->device;

  uint16_t *dst = me->_out_buf;

  uint_fast16_t x = rectangle->left;
  uint_fast16_t y = rectangle->top;
  uint_fast16_t w = rectangle->right + 1 - x;
  uint_fast16_t h = rectangle->bottom + 1 - y;
  uint_fast16_t outWidth = me->_out_width;
  uint_fast16_t outHeight = me->_out_height;

  uint8_t *src = (uint8_t*)bitmap;
  uint_fast16_t oL = 0, oR = 0;

  if (rectangle->right < me->_off_x ||
    rectangle->bottom < me->_off_y ||
    x >= (me->_off_x + outWidth) ||
    y >= (me->_off_y + outHeight))
  {
    return 1;
  }
  
  if (me->_off_y > y)
  {
    uint_fast16_t linesToSkip = me->_off_y - y;
    src += linesToSkip * w * 3;
    h -= linesToSkip;
  }

  if (me->_off_x > x)
  {
    oL = me->_off_x - x;
  }
  if (rectangle->right >= (me->_off_x + outWidth))
  {
    oR = (rectangle->right + 1) - (me->_off_x + outWidth);
  }

  int_fast16_t line = (w - (oL + oR));
  dst += oL + x - me->_off_x;
  src += oL * 3;
  do
  {
    int i = 0;
    do
    {
      uint_fast8_t r = src[i * 3 + 0];
      uint_fast8_t g = src[i * 3 + 1];
      uint_fast8_t b = src[i * 3 + 2];
      dst[i] = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);

    } while (++i != line);

    dst += outWidth;
    src += w * 3;

  } while (--h);

  return 1;
}

//===============================================================
// Writes an image row to the display
//===============================================================
uint32_t MjpegPlayer::jpgWriteRow(TJpgD *jdec, uint32_t y, uint32_t h)
{
  static int flip = 0;
  MjpegPlayer* me = (MjpegPlayer*)jdec->device;
  
  me->_tft->startWrite();
  if (y == 0)
  {
    me->_tft->setAddrWindow(me->_jpg_x, me->_jpg_y, jdec->width, jdec->height);
  }
  me->_tft->writePixels(me->_out_buf, jdec->width * h);
  me->_tft->endWrite();

  flip = !flip;
  me->_out_buf = me->_out_bufs[flip];

  return 1;
}
