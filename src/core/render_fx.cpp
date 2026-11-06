#include "render_common.h"

using namespace rd;

namespace {
int t0Frame(float t) { return (int)(t * 36); }
const uint16_t TEAR = Canvas::rgb(120, 185, 255);

void drop(Canvas& s, float x, float y, float r, uint16_t c) {
  s.fillCircleAA(x, y, r, c);
  s.fillTriangle((int)lroundf(x - r * 0.9f), (int)lroundf(y - r * 0.3f), (int)lroundf(x + r * 0.9f),
                 (int)lroundf(y - r * 0.3f), (int)lroundf(x), (int)lroundf(y - r * 2.2f), c);
}
}  // namespace

namespace rd {

bool drawEyeFx(Canvas& s, int ox, int oy, const Fx& fx, const FaceParams& p, const EyeBox& e, bool left) {
  if (fx.eyes == EF_NONE) return false;
  const float dir = left ? -1.f : 1.f;
  const uint16_t fg = faceColor(p);
  const float ecx = e.gx - ox, ecy = e.bottom - e.hFull / 2 - oy,
              sz = clampf(fmaxf(e.w, e.hFull * 0.75f), 36, 70);
  const float wob = sinf(fx.t * 6.2832f * 3);

  if (fx.eyes == EF_HEART) {
    s.fillHeart(ecx, ecy, sz * 1.05f * (1 + 0.12f * wob), Canvas::rgb(255, 80, 120));
  } else if (fx.eyes == EF_STAR) {
    s.fillStar(ecx, ecy, sz * 0.55f * (1 + 0.08f * wob), fx.t * 2.5f * dir, Canvas::rgb(255, 210, 60));
  } else if (fx.eyes == EF_COIN) {
    const float R = sz * 0.5f;
    s.fillCircleAA(ecx, ecy, R, Canvas::rgb(255, 200, 40));
    s.fillCircleAA(ecx, ecy, R * 0.78f, Canvas::rgb(225, 165, 20));
    s.drawText("$", (int)ecx, (int)ecy, FONT_LARGE, Canvas::rgb(255, 235, 120));
  } else if (fx.eyes == EF_XWOB || fx.eyes == EF_X) {
    float ang = fx.eyes == EF_X ? 0 : 0.35f * wob, c = cosf(ang), sn = sinf(ang), d = sz * 0.32f;
    for (int k = 0; k < 2; k++) {
      float dx = (k ? d : -d), dy = d;
      s.fillCapsule(ecx - (dx * c - dy * sn), ecy - (dx * sn + dy * c), ecx + (dx * c - dy * sn),
                    ecy + (dx * sn + dy * c), S.line_r + 2, fg);
    }
  } else {
    return false;
  }
  return true;
}

void drawGlitchFx(Canvas& s, int ox, int oy, const Fx& fx, const EyeBox& e, bool left) {
  if (fx.deco == DF_GLITCH) {
    uint32_t h = (uint32_t)(t0Frame(fx.t) * 2654435761u) ^ (left ? 0x9E37u : 0x7F4Au);
    auto rnd = [&](int n) {
      h ^= h << 13;
      h ^= h >> 17;
      h ^= h << 5;
      return (int)(h % (uint32_t)n);
    };
    for (int k = 0; k < 4; k++) {
      int y0 = e.y + rnd(e.h > 1 ? e.h : 1), hh = 2 + rnd(5), dx = rnd(21) - 10;
      for (int y = y0; y < y0 + hh; y++) s.shiftRow(y - oy, dx, BG);
    }
    for (int k = 0; k < 2; k++)
      s.fillRect(e.x - 8 + rnd(e.w + 16) - ox, e.y + rnd(e.h > 1 ? e.h : 1) - oy, 4 + rnd(14), 2,
                 k ? Canvas::rgb(0, 255, 255) : Canvas::rgb(255, 0, 200));
  }
}

void drawDecoFx(Canvas& s, int ox, int oy, const Fx& fx, const FaceParams& p, const EyeBox& e, bool left) {
  const int cx = left ? EYE_CX_L : EYE_CX_R;
  const float dir = left ? -1.f : 1.f;
  const uint16_t fg = faceColor(p);
  const float wob = sinf(fx.t * 6.2832f * 3);
  const float t = fx.t;
  if (fx.deco == DF_TEARS) {
    for (int k = 0; k < 3; k++) {
      float ph = fmodf(t * 2.5f + k / 3.f, 1.f);
      drop(s, e.gx + dir * e.w * 0.3f - ox, e.bottom + 6 + ph * 22 - oy, 3.5f, TEAR);
    }
  }
  if (fx.deco == DF_HEARTS) {
    for (int k = 0; k < 2; k++) {
      float ph = fmodf(t * 0.8f + k * 0.5f + (left ? 0.25f : 0), 1.f);
      s.fillHeart(e.gx + dir * e.w * 0.6f + 4 * sinf(ph * 12.566f) - ox, e.bottom - ph * 90 - oy, 10 + 5 * ph,
                  Canvas::rgb(255, 90, 130));
    }
  } else if (fx.deco == DF_SHY) {
    const float bx = cx + dir * 14, by = EYE_CY + BLUSH_DY;
    for (int i = -1; i <= 1; i++)
      s.fillCapsule(bx + i * 7 - 3 - ox, by + 4 - oy, bx + i * 7 + 3 - ox, by - 4 - oy, 1.3f,
                    Canvas::rgb(255, 120, 150));
  } else if (fx.deco == DF_STEAM) {
    for (int k = 0; k < 3; k++) {
      float ph = fmodf(t * 2 + k / 3.f, 1.f);
      s.fillCircleAA(e.gx + dir * (e.w / 2 + 6 + ph * 12) - ox, e.bottom - e.hFull - 4 - ph * 20 - oy,
                     3 + ph * 6, mix565(Canvas::rgb(235, 235, 235), BG, ph));
    }
  } else if (fx.deco == DF_SPARKLE) {
    for (int k = 0; k < 2; k++) {
      float sc = fabsf(sinf(t * 9.42f + k * 1.7f + (left ? 0.8f : 0)));
      float x = e.gx + dir * (e.w * 0.55f + k * 6) - ox, y = e.bottom - e.hFull - 6 + k * 30 - oy;
      const uint16_t c = Canvas::rgb(255, 250, 220);
      s.fillCapsule(x - 7 * sc, y, x + 7 * sc, y, 1.3f, c);
      s.fillCapsule(x, y - 10 * sc, x, y + 10 * sc, 1.3f, c);
    }
  }
  if (fx.deco == DF_GLASSES) {
    const float hw = e.w * 0.5f + 7, x0 = e.gx - hw - ox, x1 = e.gx + hw - ox,
                top = e.bottom - e.hFull - 7 - oy, bot = e.bottom + 7 - oy, r = 1.5f;
    s.fillCapsule(x0, top, x1, top, r, GLASSES);
    s.fillCapsule(x0, bot, x1, bot, r, GLASSES);
    s.fillCapsule(x0, top, x0, bot, r, GLASSES);
    s.fillCapsule(x1, top, x1, bot, r, GLASSES);
    const float by = glassesY(e) - oy, inner = (left ? x1 : x0), edge = 160.f - ox;
    s.fillCapsule(inner, by, edge, by, r, GLASSES);
    const float outer = left ? x0 : x1;
    s.fillCapsule(outer, top + 8, outer + dir * 14, top + 4, r, GLASSES);
  }
  if (left) return;
  const float sx = REGIONS[1].x + REG_W - 22 - ox;
  if (fx.deco == DF_SWEAT) {
    drop(s, e.gx + e.w * 0.55f - ox, e.bottom - e.hFull * 0.85f + t * 22 - oy, 5, TEAR);
  } else if (fx.deco == DF_BUBBLE) {
    const float nx = fmaxf(160.f, (float)REGIONS[1].x + 1), ny = fminf((float)EYE_CY + 52, 150.f) - oy;
    const float POP = 0.94f;
    if (t < POP) {
      const float ph = t / POP, breath = 0.5f - 0.5f * cosf(ph * 6.2832f * 3),
                  r = 3 + 14 * (0.35f + 0.65f * ph) * (0.35f + 0.65f * breath);
      const float bx = nx + r - ox, by = ny + 0.2f * r;
      s.fillCircleAA(bx, by, r, faceColor(p, 0.85f));
      s.fillCircleAA(bx, by, r - 1.6f, faceColor(p, 0.2f));
      s.fillCircleAA(bx - 0.35f * r, by - 0.35f * r, fmaxf(1.f, 0.18f * r), Canvas::rgb(255, 255, 255));
    } else {
      const float k = (t - POP) / (1 - POP), bx = nx + 15 - ox, by = ny + 3;
      for (int d = 0; d < 6; d++) {
        const float a = d * 1.0472f + 0.4f;
        s.fillCircleAA(bx + cosf(a) * (8 + 12 * k), by + sinf(a) * (8 + 12 * k), 2 * (1 - k) + 0.5f,
                       faceColor(p, 0.85f));
      }
    }
  } else if (fx.deco == DF_VEIN) {
    const float vy = 28 - oy, k = 1.8f * (1 + 0.15f * wob);
    const uint16_t red = Canvas::rgb(255, 50, 50);
    for (int qx = -1; qx <= 1; qx += 2)
      for (int qy = -1; qy <= 1; qy += 2)
        s.fillCapsule(sx + qx * 2.5f * k, vy + qy * 9 * k, sx + qx * 9 * k, vy + qy * 2.5f * k, 2.8f, red);
  } else if (fx.deco == DF_QUESTION) {
    s.drawText("?", (int)sx, (int)(28 + 4 * wob - oy), FONT_LARGE, fg);
  } else if (fx.deco == DF_EXCLAIM) {
    if (t > 0.05f) s.drawText("!", (int)sx, (int)(26 - oy), FONT_LARGE, Canvas::rgb(255, 220, 80));
  } else if (fx.deco == DF_TEXT) {
    s.drawText(fx.text, REGIONS[1].x + REG_W / 2 - ox, (int)(24 + 3 * wob - oy), FONT_LARGE, fg);
  } else if (fx.deco == DF_DOTS || fx.deco == DF_GLASSES) {
    int dots = (int)(t * 4) % 4;
    for (int i = 0; i < dots; i++) s.fillCircleAA(sx - 14 + i * 10, 24 - oy, 3.2f, fg);
  } else if (fx.deco == DF_NOTES) {
    for (int k = 0; k < 2; k++) {
      float ph = fmodf(t + k * 0.5f, 1.f), x = sx - 8 + k * 12 + 3 * sinf(ph * 6.2832f),
            y = 70 - ph * 55 - oy;
      s.fillCircleAA(x, y, 4, fg);
      s.fillCapsule(x + 3.5f, y, x + 3.5f, y - 14, 1.2f, fg);
      s.fillCapsule(x + 3.5f, y - 14, x + 9, y - 10, 1.2f, fg);
    }
  }
}

void drawTongueFx(Canvas& s, int ox, int oy, float x, float y) {
  s.fillRoundRectAA((int)(x - 7) - ox, (int)y - 2 - oy, 14, 22, 7, Canvas::rgb(255, 110, 140));
}

}  // namespace rd
