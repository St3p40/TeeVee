#include "platform.h"
#include <stdlib.h>
#include "platform_lcd.h"
#include <chrono>
#include "lcd.h"

namespace {
uint16_t g_fb[desktop::W * desktop::H];
const auto g_start = std::chrono::steady_clock::now();
}  // namespace

uint32_t tvMicros() {
  return (uint32_t)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() -
                                                                         g_start)
      .count();
}

uint16_t* tvAllocPixels(size_t bytes) { return (uint16_t*)malloc(bytes); }

namespace desktop {
const uint16_t* framebuffer() { return g_fb; }
static uint16_t g_key = 0;
static bool g_keyOn = false;
void setTransparentKey(uint16_t key, bool on) {
  g_key = key;
  g_keyOn = on;
}
void toRgba(uint32_t* out) {
  for (int i = 0; i < W * H; i++) {
    const uint16_t v = (uint16_t)(g_fb[i] >> 8 | g_fb[i] << 8);
    uint32_t r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
    out[i] = 0xFF000000u | (b << 3 | b >> 2) << 16 | (g << 2 | g >> 4) << 8 |
             (r << 3 | r >> 2);  // little-endian: bytes R G B A
  }
}

void toArgb(uint32_t* out) {
  for (int i = 0; i < W * H; i++) {
    uint16_t v = (uint16_t)(g_fb[i] >> 8 | g_fb[i] << 8);
    if (g_keyOn && v == g_key) {
      out[i] = 0;
      continue;
    }
    uint32_t r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
    r = r << 3 | r >> 2;
    g = g << 2 | g >> 4;
    b = b << 3 | b >> 2;
    out[i] = 0xFF000000u | r << 16 | g << 8 | b;
  }
}
}  // namespace desktop

void Lcd::begin(uint8_t) {}
void Lcd::setMadctl(uint8_t) {}
void Lcd::setBrightness(uint8_t) {}
void Lcd::fillScreen(uint16_t c) {
  const uint16_t s = (uint16_t)(c >> 8 | c << 8);
  for (uint16_t& p : g_fb) p = s;
}
void Lcd::push(int x, int y, int w, int h, const uint16_t* pixels) {
  for (int r = 0; r < h; r++) {
    if (y + r < 0 || y + r >= desktop::H) continue;
    for (int c = 0; c < w; c++)
      if (x + c >= 0 && x + c < desktop::W) g_fb[(y + r) * desktop::W + x + c] = pixels[r * w + c];
  }
}
void Lcd::wait() {}
void Lcd::cmd(uint8_t, const uint8_t*, int) {}
void Lcd::window(int, int, int, int) {}
