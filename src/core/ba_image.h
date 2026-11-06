#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Bitmap Array files (https://github.com/St3p40/BitmapArrayFile): "bA", a 2-letter type, width and height (uint16, little-endian), then the
// pixels, packed with no padding between rows; several frames simply follow each other. Multi-byte pixels are high byte first, as the file's own tools
// write them. Types: b0 1-bit, bw 8-bit grey, b1 RGB332, b2 RGB565, b3 RGB888. Pure logic, no Arduino deps. Host test: test/app_test.cpp.
struct BaInfo {
    char type[3];
    uint8_t bits;
    uint16_t w, h;
    uint32_t frames;
    uint32_t frameBytes() const { return (uint32_t)(((uint64_t)w * h * bits + 7) / 8); }
};

static constexpr size_t BA_HEADER = 8;

static inline bool baParse(const uint8_t* head, size_t fileSize, BaInfo& b) {
  if (fileSize < BA_HEADER || head[0] != 'b' || head[1] != 'A') return false;
  const char t0 = (char)head[2], t1 = (char)head[3];
  b.bits = t0 != 'b' ? 0 : t1 == '0' ? 1 : t1 == 'w' || t1 == '1' ? 8 : t1 == '2' ? 16 : t1 == '3' ? 24 : 0;
  if (!b.bits) return false;
  b.type[0] = t0, b.type[1] = t1, b.type[2] = 0;
  b.w = (uint16_t)(head[4] | head[5] << 8);
  b.h = (uint16_t)(head[6] | head[7] << 8);
  if (!b.w || !b.h) return false;
  b.frames = (uint32_t)((fileSize - BA_HEADER) / b.frameBytes());
  return b.frames > 0;
}

// A pixel as the canvas holds it (RGB565, byte-swapped) from the file's value.
static inline uint16_t baSwap(uint16_t v) { return (uint16_t)(v >> 8 | v << 8); }
static inline uint16_t baPixel(const BaInfo& b, uint32_t v) {
  switch (b.bits) {
    case 1: return v ? 0xFFFF : 0;
    case 8:
      if (b.type[1] == 'w') return baSwap((uint16_t)((v & 0xF8) << 8 | (v & 0xFC) << 3 | v >> 3));
      return baSwap((uint16_t)((v & 0xE0) << 8 | ((v >> 2) & 7) * 9 << 5 | (v & 3) * 10));
    case 16: return baSwap((uint16_t)v);
    default: return baSwap((uint16_t)((v >> 8 & 0xF800) | (v >> 5 & 0x07E0) | (v >> 3 & 0x001F)));
  }
}

// Reads n pixels of a row (x, y of the picture) into out (canvas pixels). read(fileOffset, buf, len) -> false on a failed read. n is up to 128.
template <typename Read>
static inline bool baRow(const BaInfo& b, uint32_t frame, int x, int y, int n, uint16_t* out, Read read) {
  uint8_t raw[128 * 3 + 2];
  if (n <= 0 || n > 128) return false;
  const uint64_t bit = (uint64_t)(frame % b.frames) * b.frameBytes() * 8 + ((uint64_t)y * b.w + x) * b.bits;
  const uint32_t first = (uint32_t)(bit / 8);
  const size_t len = (size_t)((bit % 8 + (uint64_t)n * b.bits + 7) / 8);
  if (!read(BA_HEADER + first, raw, len)) return false;
  if (b.bits == 1) {
    for (int i = 0; i < n; i++) {
      const uint32_t at = (uint32_t)(bit % 8) + i;
      out[i] = baPixel(b, raw[at >> 3] >> (7 - (at & 7)) & 1);
    }
    return true;
  }
  const int size = b.bits / 8;
  for (int i = 0; i < n; i++) {
    uint32_t v = 0;
    for (int k = 0; k < size; k++) v = v << 8 | raw[i * size + k];
    out[i] = baPixel(b, v);
  }
  return true;
}
