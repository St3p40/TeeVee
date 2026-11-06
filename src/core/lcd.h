#pragma once
#include <stdint.h>

// ILI9341 on the CYD's HSPI pins, driven directly with the ESP-IDF spi_master driver (DMA).
// Landscape 320x240. push() is asynchronous: it waits for the previous transfer, then queues this one,
// so the caller can draw the next strip while the last one is still going out.
// Set while the renderer sends a whole turned frame: a panel that keeps a copy of the screen may then skip keeping it (every
// pixel is sent anyway, and the next normal frame starts with a full fillScreen). Only the AMOLED driver reads it.
inline bool g_lcdFullFrame = false;

class Lcd {
  public:
    static constexpr int W = 320, H = 240;
    void begin(uint8_t madctl = 0xE8);
    void setMadctl(uint8_t madctl);
    uint8_t madctl() const { return _madctl; }
    void setBrightness(uint8_t level);
    void setDisplayOn(bool on);
    void fillScreen(uint16_t rgb565);
    // Buffer holds w*h byte-swapped RGB565 pixels in DMA-capable memory and must stay valid until wait().
    void push(int x, int y, int w, int h, const uint16_t* pixels);
#ifdef TEEVEE_BOARD_AMOLED
    // The full panel width, rows y..y+h-1 in panel coordinates: the bands above and below the face. Queued like push().
    void pushRows(int y, int h, const uint16_t* pixels);
#endif
    void wait();

  private:
    void cmd(uint8_t c, const uint8_t* data = nullptr, int len = 0);
    void window(int x, int y, int w, int h);
    bool _pending = false;
    uint8_t _madctl = 0xE8;
};

// XPT2046 resistive touch, bit-banged on its own pins (core 0 polls it while core 1 drives the display; SPI3 stays free for the SD card).
class TouchPanel {
  public:
    void begin();
    bool read(int& x, int& y, uint8_t madctl);
};
