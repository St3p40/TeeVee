#pragma once
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "font_data.h"
#include "platform.h"

// 16-bit off-screen canvas (a strip or panel of the screen) with the few primitives the face needs:
// fills, anti-aliased rounded rects and circles, ellipses, triangles, alpha pixels, AA bitmap text.
// Pixels are stored byte-swapped (big-endian RGB565) so the buffer goes straight to the panel by DMA.
// Colours passed in are plain RGB565. Coordinates are canvas-local; everything is clipped. No platform deps (tvAllocPixels comes from platform.h).
class Canvas {
  public:
    static constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
      return (uint16_t)((r & 0xF8) << 8 | (g & 0xFC) << 3 | b >> 3);
    }

    bool create(int w, int h) {
      _buf = tvAllocPixels(w * h * 2);
      _w = w;
      _h = h;
      return _buf != nullptr;
    }
    void attach(uint16_t* buf, int w, int h) {
      _buf = buf;
      _w = w;
      _h = h;
    }
    uint16_t* buffer() { return _buf; }
    int width() const { return _w; }
    int height() const { return _h; }

    void fill(uint16_t c) {
      const uint16_t s = swap(c);
      for (int i = 0, n = _w * _h; i < n; i++) _buf[i] = s;
    }

    void fillRect(int x, int y, int w, int h, uint16_t c) {
      if (!clip(x, y, w, h)) return;
      const uint16_t s = swap(c);
      for (int r = y; r < y + h; r++) {
        uint16_t* p = _buf + r * _w + x;
        for (int i = 0; i < w; i++) p[i] = s;
      }
    }
    void hline(int x, int y, int w, uint16_t c) { fillRect(x, y, w, 1, c); }
    void vline(int x, int y, int h, uint16_t c) { fillRect(x, y, 1, h, c); }

    void blendPixel(int x, int y, uint8_t a, uint16_t c) {
      if (a == 0 || (unsigned)x >= (unsigned)_w || (unsigned)y >= (unsigned)_h) return;
      uint16_t& d = _buf[y * _w + x];
      d = a == 255 ? swap(c) : swap(blend(c, swap(d), a));
    }

    void fillRoundRectAA(int x, int y, int w, int h, int r, uint16_t c) {
      if (w <= 0 || h <= 0) return;
      if (r > (w >> 1)) r = w >> 1;
      if (r > (h >> 1)) r = h >> 1;
      if (r <= 0) return fillRect(x, y, w, h, c);
      const float cl = x + r, cr = x + w - r;
      for (int row = (y < 0 ? 0 : y); row < y + h && row < _h; row++) {
        const float yc = row + 0.5f;
        float cy;
        if (yc < y + r)
          cy = y + r;
        else if (yc > y + h - r)
          cy = y + h - r;
        else {
          hline(x, row, w, c);
          continue;
        }
        const float dy = fabsf(yc - cy);
        roundSpan(row, cl, cr, dy, (float)r, c);
      }
    }

    void fillCircleAA(float cx, float cy, float r, uint16_t c) {
      int y0 = (int)floorf(cy - r - 0.5f), y1 = (int)ceilf(cy + r + 0.5f);
      for (int row = y0 < 0 ? 0 : y0; row <= y1 && row < _h; row++) {
        float dy = fabsf(row + 0.5f - cy);
        if (dy < r + 0.5f) roundSpan(row, cx, cx, dy, r, c);
      }
    }

    void fillEllipse(int cx, int cy, int rx, int ry, uint16_t c) {
      if (rx <= 0 || ry <= 0) return;
      for (int dy = -ry; dy <= ry; dy++) {
        int row = cy + dy;
        if (row < 0 || row >= _h) continue;
        float yy = dy / (ry + 0.5f), half = rx * sqrtf(1.f - yy * yy);
        if (half < 0.5f) continue;
        int dx = (int)lroundf(half - 0.5f);
        hline(cx - dx, row, 2 * dx + 1, c);
      }
    }

    void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c) {
      if (y0 > y1) swapPt(x0, y0, x1, y1);
      if (y1 > y2) swapPt(x1, y1, x2, y2);
      if (y0 > y1) swapPt(x0, y0, x1, y1);
      if (y0 == y2) {
        int a = x0 < x1 ? x0 : x1, b = x0 > x1 ? x0 : x1;
        a = a < x2 ? a : x2;
        b = b > x2 ? b : x2;
        return hline(a, y0, b - a + 1, c);
      }
      for (int row = y0 < 0 ? 0 : y0; row <= y2 && row < _h; row++) {
        float xa = x0 + (float)(x2 - x0) * (row - y0) / (y2 - y0);
        float xb = row < y1 ? (y1 == y0 ? x1 : x0 + (float)(x1 - x0) * (row - y0) / (y1 - y0))
                            : (y2 == y1 ? x1 : x1 + (float)(x2 - x1) * (row - y1) / (y2 - y1));
        if (xa > xb) {
          float t = xa;
          xa = xb;
          xb = t;
        }
        int a = (int)lroundf(xa), b = (int)lroundf(xb);
        hline(a, row, b - a + 1, c);
      }
    }

    void fillCapsule(float x0, float y0, float x1, float y1, float r, uint16_t c) {
      const float dx = x1 - x0, dy = y1 - y0, len2 = dx * dx + dy * dy;
      int ya = (int)floorf(fminf(y0, y1) - r - 1), yb = (int)ceilf(fmaxf(y0, y1) + r + 1);
      int xa = (int)floorf(fminf(x0, x1) - r - 1), xb = (int)ceilf(fmaxf(x0, x1) + r + 1);
      if (ya < 0) ya = 0;
      if (xa < 0) xa = 0;
      if (yb > _h - 1) yb = _h - 1;
      if (xb > _w - 1) xb = _w - 1;
      for (int py = ya; py <= yb; py++)
        for (int px = xa; px <= xb; px++) {
          float qx = px + 0.5f - x0, qy = py + 0.5f - y0;
          float k = len2 > 0 ? (qx * dx + qy * dy) / len2 : 0;
          k = k < 0 ? 0 : (k > 1 ? 1 : k);
          float ex = qx - k * dx, ey = qy - k * dy;
          float cov = r + 0.5f - sqrtf(ex * ex + ey * ey);
          if (cov > 0) blendPixel(px, py, cov >= 1 ? 255 : (uint8_t)(cov * 255), c);
        }
    }

    void fillHeart(float cx, float cy, float size, uint16_t c) {
      const float s = size / 2.28f;
      const float deep = -8.f / s;
      int x0 = (int)floorf(cx - 1.2f * s) - 1, x1 = (int)ceilf(cx + 1.2f * s) + 1;
      int y0 = (int)floorf(cy - 1.3f * s) - 1, y1 = (int)ceilf(cy + 1.1f * s) + 1;
      if (x0 < 0) x0 = 0;
      if (y0 < 0) y0 = 0;
      if (x1 > _w - 1) x1 = _w - 1;
      if (y1 > _h - 1) y1 = _h - 1;
      for (int py = y0; py <= y1; py++) {
        int run = -1;
        for (int px = x0; px <= x1 + 1; px++) {
          float f = 1.f, g = 0.f;
          if (px <= x1) {
            const float u = (px + 0.5f - cx) / s, v = (cy - py - 0.5f) / s + 0.12f;
            const float u2 = u * u, A = u2 + v * v - 1, A2 = A * A;
            f = A2 * A - u2 * v * v * v;
            if (f < deep) {
              if (run < 0) run = px;
              continue;
            }
            if (f <= 0.4f) {
              const float gu = 6 * A2 * u - 2 * u * v * v * v, gv = 6 * A2 * v - 3 * u2 * v * v;
              g = sqrtf(gu * gu + gv * gv);
            }
          }
          if (run >= 0) {
            hline(run, py, px - run, c);
            run = -1;
          }
          if (f > 0.4f) continue;
          const float cov = g < 1e-3f ? (f <= 0 ? 1.f : 0.f) : 0.5f - (f / g) * s;
          if (cov > 0) blendPixel(px, py, cov >= 1 ? 255 : (uint8_t)(cov * 255), c);
        }
      }
    }

    void fillStar(float cx, float cy, float R, float angle, uint16_t c) {
      float px[10], py[10];
      for (int i = 0; i < 10; i++) {
        float a = angle - 1.5708f + i * 0.6283f, rr = (i & 1) ? R * 0.42f : R;
        px[i] = cx + rr * cosf(a);
        py[i] = cy + rr * sinf(a);
      }
      for (int i = 0; i < 10; i++) {
        int j = (i + 1) % 10;
        fillTriangle((int)lroundf(cx), (int)lroundf(cy), (int)lroundf(px[i]), (int)lroundf(py[i]),
                     (int)lroundf(px[j]), (int)lroundf(py[j]), c);
      }
    }

    void shiftRow(int y, int dx, uint16_t c) {
      if ((unsigned)y >= (unsigned)_h || dx == 0) return;
      uint16_t* row = _buf + y * _w;
      const uint16_t s = swap(c);
      if (dx > 0) {
        if (dx > _w) dx = _w;
        memmove(row + dx, row, (_w - dx) * 2);
        for (int i = 0; i < dx; i++) row[i] = s;
      } else {
        dx = -dx > _w ? _w : -dx;
        memmove(row, row + dx, (_w - dx) * 2);
        for (int i = _w - dx; i < _w; i++) row[i] = s;
      }
    }

    static int textWidth(const char* s, const BitmapFont& f) {
      int w = 0;
      for (; *s; s++) {
        unsigned char ch = (unsigned char)*s;
        if (ch >= f.first && ch <= f.last) w += f.widths[ch - f.first];
      }
      return w;
    }

    void drawText(const char* s, int cx, int cy, const BitmapFont& f, uint16_t c) {
      int x = cx - (textWidth(s, f) >> 1), y = cy - (f.height >> 1);
      for (; *s; s++) {
        unsigned char ch = (unsigned char)*s;
        if (ch < f.first || ch > f.last) continue;
        const int gw = f.widths[ch - f.first];
        const uint8_t* g = f.data + f.offsets[ch - f.first];
        for (int i = 0, n = gw * f.height; i < n; i++) {
          uint8_t v = (g[i >> 1] >> ((i & 1) ? 0 : 4)) & 0x0F;
          if (v) blendPixel(x + i % gw, y + i / gw, v * 17, c);
        }
        x += gw;
      }
    }

  private:
    static uint16_t swap(uint16_t v) { return (uint16_t)(v >> 8 | v << 8); }
    static void swapPt(int& xa, int& ya, int& xb, int& yb) {
      int t = xa;
      xa = xb;
      xb = t;
      t = ya;
      ya = yb;
      yb = t;
    }
    static uint16_t blend(uint16_t fg, uint16_t bg, uint8_t a) {
      uint32_t r = ((fg >> 11) * a + (bg >> 11) * (255 - a)) / 255;
      uint32_t g = (((fg >> 5) & 63) * a + ((bg >> 5) & 63) * (255 - a)) / 255;
      uint32_t b = ((fg & 31) * a + (bg & 31) * (255 - a)) / 255;
      return (uint16_t)(r << 11 | g << 5 | b);
    }

    void roundSpan(int row, float cl, float cr, float dy, float r, uint16_t c) {
      const float ro = r + 0.5f, ri = r - 0.5f;
      const float dxOut = sqrtf(ro * ro - dy * dy);
      const float dxIn = dy < ri ? sqrtf(ri * ri - dy * dy) : -1.f;
      const float din = dxIn > 0 ? dxIn : 0;
      int inA = (int)ceilf(cl - din - 0.5f), inB = (int)floorf(cr + din - 0.5f);
      if (dxIn < 0 && cr <= cl) inB = inA - 1;
      if (inB >= inA) hline(inA, row, inB - inA + 1, c);
      int outA = (int)floorf(cl - dxOut - 0.5f), outB = (int)ceilf(cr + dxOut - 0.5f);
      for (int px = outA; px < inA; px++) edge(px, row, cl, dy, r, c);
      for (int px = inB + 1; px <= outB; px++) edge(px, row, cr, dy, r, c);
    }
    void edge(int px, int row, float ccx, float dy, float r, uint16_t c) {
      float dx = px + 0.5f - ccx;
      float cov = r + 0.5f - sqrtf(dx * dx + dy * dy);
      if (cov > 0) blendPixel(px, row, cov >= 1 ? 255 : (uint8_t)(cov * 255), c);
    }

    bool clip(int& x, int& y, int& w, int& h) const {
      if (x < 0) w += x, x = 0;
      if (y < 0) h += y, y = 0;
      if (x + w > _w) w = _w - x;
      if (y + h > _h) h = _h - y;
      return w > 0 && h > 0;
    }

    uint16_t* _buf = nullptr;
    int _w = 0, _h = 0;
};
