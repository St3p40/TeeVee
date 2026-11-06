#pragma once
#include <stdint.h>
#include <string.h>
#include "ba_image.h"
#include "canvas.h"

enum AppOp : uint8_t { OP_CLEAR, OP_RECT, OP_RRECT, OP_CIRCLE, OP_LINE, OP_TEXT, OP_FACE, OP_IMAGE };

struct AppCmd {
    uint8_t op;
    uint8_t a;
    int16_t x, y, w, h;
    uint16_t color;
    uint16_t text;
};

enum AppAlign : uint8_t { AL_LEFT, AL_CENTER, AL_RIGHT };
enum AppTouch : uint8_t { TOUCH_DOWN, TOUCH_MOVE, TOUCH_UP };
inline const char* const APP_BUTTONS[] = {"up", "down", "left", "right", "a", "b", "c", "d"};
constexpr int APP_BUTTON_COUNT = 8;

static constexpr int APP_W = 320, APP_H = 240;
static constexpr int APP_MAX_CMDS = 256, APP_TEXT_POOL = 1024;
static constexpr int APP_TILE_W = 80, APP_TILE_H = 48, APP_TILES_X = APP_W / APP_TILE_W,
                     APP_TILES_Y = APP_H / APP_TILE_H;
static constexpr int APP_TILES = APP_TILES_X * APP_TILES_Y;

struct AppFrame {
    uint32_t version = 0;
    uint16_t count = 0, textLen = 0;
    bool overflow = false;
    AppCmd cmds[APP_MAX_CMDS];
    char text[APP_TEXT_POOL];

    void reset() {
      count = textLen = 0;
      overflow = false;
    }
    AppCmd* add() {
      if (count >= APP_MAX_CMDS) {
        overflow = true;
        return nullptr;
      }
      AppCmd* c = &cmds[count++];
      memset(c, 0, sizeof *c);
      return c;
    }
    int addText(const char* s, size_t n) {
      if (textLen + n + 1 > APP_TEXT_POOL) {
        overflow = true;
        return -1;
      }
      int off = textLen;
      memcpy(text + off, s, n);
      text[off + n] = 0;
      textLen = (uint16_t)(textLen + n + 1);
      return off;
    }
};

static inline const BitmapFont& appFont(uint8_t a) { return (a & 0x0F) >= 2 ? FONT_LARGE : FONT_SMALL; }

static inline int appTextLeft(const AppFrame& f, const AppCmd& c, int tw) {
  switch (c.a >> 4) {
    case AL_CENTER: return c.x - tw / 2;
    case AL_RIGHT: return c.x - tw;
    default: return c.x;
  }
}

static inline void appBounds(const AppFrame& f, const AppCmd& c, int& x0, int& y0, int& x1, int& y1) {
  switch (c.op) {
    case OP_CLEAR:
      x0 = y0 = 0;
      x1 = APP_W;
      y1 = APP_H;
      break;
    case OP_RECT:
    case OP_RRECT:
      x0 = c.x - 1;
      y0 = c.y - 1;
      x1 = c.x + c.w + 1;
      y1 = c.y + c.h + 1;
      break;
    case OP_CIRCLE:
      x0 = c.x - c.w - 2;
      y0 = c.y - c.w - 2;
      x1 = c.x + c.w + 3;
      y1 = c.y + c.w + 3;
      break;
    case OP_LINE: {
      int m = c.a / 2 + 3;
      x0 = (c.x < c.w ? c.x : c.w) - m;
      x1 = (c.x > c.w ? c.x : c.w) + m;
      y0 = (c.y < c.h ? c.y : c.h) - m;
      y1 = (c.y > c.h ? c.y : c.h) + m;
      break;
    }
    case OP_IMAGE:
      x0 = c.x;
      y0 = c.y;
      x1 = c.x + c.w;
      y1 = c.y + c.h;
      break;
    case OP_FACE: {
      int hw = c.w / 2 + 6, hh = c.w * 11 / 40 + 6;
      x0 = c.x - hw;
      x1 = c.x + hw;
      y0 = c.y - hh;
      y1 = c.y + hh;
      break;
    }
    default: {
      const BitmapFont& font = appFont(c.a);
      int tw = Canvas::textWidth(f.text + c.text, font), left = appTextLeft(f, c, tw);
      x0 = left - 2;
      x1 = left + tw + 2;
      y0 = c.y - font.height / 2 - 2;
      y1 = c.y + font.height / 2 + 3;
    }
  }
}

struct AppImageSource {
    virtual bool info(const char* name, BaInfo& b) = 0;
    virtual bool read(const char* name, uint32_t offset, void* buf, size_t len) = 0;
};
inline AppImageSource* appImages = nullptr;

static inline void appDrawImage(const char* name, const AppCmd& c, Canvas& s, int ox, int oy) {
  BaInfo b;
  if (!appImages || !appImages->info(name, b)) return;
  const int left = c.x - ox, top = c.y - oy;
  const int x0 = left < 0 ? 0 : left, y0 = top < 0 ? 0 : top;
  const int x1 = left + c.w < s.width() ? left + c.w : s.width(),
            y1 = top + c.h < s.height() ? top + c.h : s.height();
  if (x1 <= x0) return;
  const auto read = [&](uint32_t off, void* buf, size_t len) { return appImages->read(name, off, buf, len); };
  for (int y = y0; y < y1; y++)
    for (int x = x0; x < x1;) {
      const int n = x1 - x > 128 ? 128 : x1 - x;
      if (!baRow(b, c.color, x - left, y - top, n, s.buffer() + y * s.width() + x, read)) return;
      x += n;
    }
}

using AppFaceDraw = void (*)(Canvas& s, int ox, int oy, const AppCmd& c, void* ctx);
static inline void appReplay(const AppFrame& f, Canvas& s, int ox, int oy, AppFaceDraw face = nullptr,
                             void* ctx = nullptr) {
  for (int i = 0; i < f.count; i++) {
    const AppCmd& c = f.cmds[i];
    switch (c.op) {
      case OP_CLEAR: s.fill(c.color); break;
      case OP_RECT: s.fillRect(c.x - ox, c.y - oy, c.w, c.h, c.color); break;
      case OP_RRECT: s.fillRoundRectAA(c.x - ox, c.y - oy, c.w, c.h, c.a, c.color); break;
      case OP_CIRCLE: s.fillCircleAA((float)(c.x - ox), (float)(c.y - oy), (float)c.w, c.color); break;
      case OP_LINE:
        s.fillCapsule((float)(c.x - ox), (float)(c.y - oy), (float)(c.w - ox), (float)(c.h - oy), c.a * 0.5f,
                      c.color);
        break;
      case OP_IMAGE: appDrawImage(f.text + c.text, c, s, ox, oy); break;
      case OP_FACE:
        if (face)
          face(s, ox, oy, c, ctx);
        else
          s.fillCircleAA((float)(c.x - ox), (float)(c.y - oy), c.w * 0.25f, Canvas::rgb(242, 82, 89));
        break;
      case OP_TEXT: {
        const BitmapFont& font = appFont(c.a);
        const char* str = f.text + c.text;
        int tw = Canvas::textWidth(str, font), left = appTextLeft(f, c, tw);
        s.drawText(str, left + tw / 2 - ox, c.y - oy, font, c.color);
        break;
      }
    }
  }
}

// Hash of what the tile would show: the commands that reach it, in order. Equal hash = same pixels.
// `faceKey` stands for how the face looks right now: a face command in the tile changes the tile's hash whenever the face changes.
static inline uint32_t appTileHash(const AppFrame& f, int tx, int ty, uint32_t faceKey = 0) {
  const int x0 = tx * APP_TILE_W, y0 = ty * APP_TILE_H, x1 = x0 + APP_TILE_W, y1 = y0 + APP_TILE_H;
  uint32_t h = 2166136261u;
  auto mix = [&](const void* p, size_t n) {
    for (size_t i = 0; i < n; i++) h = (h ^ ((const uint8_t*)p)[i]) * 16777619u;
  };
  for (int i = 0; i < f.count; i++) {
    const AppCmd& c = f.cmds[i];
    int bx0, by0, bx1, by1;
    appBounds(f, c, bx0, by0, bx1, by1);
    if (bx1 <= x0 || bx0 >= x1 || by1 <= y0 || by0 >= y1) continue;
    mix(&c, sizeof c - sizeof c.text);
    if (c.op == OP_TEXT || c.op == OP_IMAGE) mix(f.text + c.text, strlen(f.text + c.text) + 1);
    if (c.op == OP_FACE) mix(&faceKey, sizeof faceKey);
  }
  return h;
}
