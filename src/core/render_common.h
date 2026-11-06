#pragma once
// Internals shared by the renderer's source files:
//   renderer.cpp         face drawing: dirty regions, eyes, mouth
//   render_fx.cpp        emote effects (eye replacements, glitch, tears, hearts, symbols...)
//   render_widgets.cpp   the bottom-corner panels
//   render_common.cpp    geometry from the style, colour helpers
// Everything here runs on core 1 only.
#include <algorithm>
#include <math.h>
#include "renderer.h"

using std::max;
using std::min;

namespace rd {
// Non-overlapping dirty regions, all REG_W wide so a strip buffer pushes contiguously. Eye regions follow the
// style's eye spacing (eye_gap 112..150 keeps them apart and on screen); see layout().
constexpr int REG_W = 112;
constexpr int STRIP_H = 40;
constexpr int BRIDGE_Y = 70;
struct Rect {
    int16_t x, y, h;
};
extern Rect REGIONS[3];

extern int EYE_CX_L, EYE_CX_R, EYE_CY, MOUTH_Y;
extern float DAY_HUE, DAY_VAL, PUPIL_MUL;
extern float MOUTH_PHASE;
constexpr float GAZE_X = 14, GAZE_Y = 12;
constexpr int MOUTH_X = 160;
constexpr int WID_W = 104, WID_H = 70;
constexpr int BLUSH_DY = 58, BLUSH_RY = 8;
constexpr int MOUTH_A = 16, MOUTH_DROP = 34, MOUTH_SKEW = 6;

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Current style (one renderer, core 1 only).
extern FaceStyle S;
extern uint16_t BG;
void layout();

uint16_t hsv565(float h, float s, float v);
uint16_t mix565(uint16_t a, uint16_t b, float t);
inline bool glowOn() { return S.glow > 0.02f; }
inline int matrixDot() {
  const int d = (int)lroundf(S.line_r * 4.f / 3.f);
  return d < 2 ? 2 : d;
}
inline uint8_t eyeStyleOf(bool left) { return left || !S.eye_split ? S.eye_style : S.eye_style_r; }
inline uint8_t pupilStyleOf(bool left) { return left || !S.eye_split ? S.pupil_style : S.pupil_style_r; }
inline float eyeWOf(bool left) { return left || !S.eye_split ? S.eye_w : S.eye_w_r; }
inline float eyeHOf(bool left) { return left || !S.eye_split ? S.eye_h : S.eye_h_r; }
inline float lidOf(bool left) { return left || !S.eye_split ? S.lid : S.lid_r; }
inline bool browsOf(bool left) { return left || !S.eye_split ? S.brows : S.brows_r; }
uint16_t faceColor(const FaceParams& p, float valMul = 1.f);
uint16_t eyeColor(const FaceParams& p, bool left);
uint16_t blushColor(float a);

struct EyeBox {
    int x, y, w, h;
    float gx, bottom, hFull;
};
EyeBox eyeBox(const FaceParams& p, bool left);
constexpr uint16_t GLASSES = 0xF79E;
inline float glassesY(const EyeBox& e) {
  const float y = e.bottom - e.hFull * 0.62f;
  return y < BRIDGE_Y + 4 ? BRIDGE_Y + 4 : y > BRIDGE_Y + STRIP_H - 4 ? BRIDGE_Y + STRIP_H - 4 : y;
}

struct Brow {
    float xo, yo, xi, yi, r;
};
Brow brow(const FaceParams& p, const EyeBox& e, bool left);

struct MouthShape {
    float open, A, K, hw;
    bool cat;
    uint8_t kind;
    float wob;
    float cy;  // mouth centre height (from the style), kept here so the loops never touch a global
    float up(float u) const {
      if (cat) {
        const float v = fabsf(u) * 2 - 1, a = fminf(11.f, fmaxf(-3.f, 4.5f + 6.f * A / MOUTH_A));
        return cy - a / 2 + a * (1 - v * v) - K * u;
      }
      if (kind == 3) return cy - A / 2 + A * (1 - u * u) - K * u + 3.2f * sinf(u * hw * 0.4f - MOUTH_PHASE);
      if (kind == 4)
        return cy - A / 2 + A * (1 - u * u) - K * u +
               3.5f * (fabsf(fmodf(u * hw / 6.f + 100.f, 2.f) - 1.f) * 2.f - 1.f);
      if (kind == 6) return cy - K * u * 0.3f;
      return cy - A / 2 + A * (1 - u * u) - K * u;
    }
    float lo(float u) const { return up(u) + open * MOUTH_DROP * sqrtf(fmaxf(0.f, 1 - u * u)); }
};

// Defined here (inline) and never passed by reference to an out-of-line function: either one forces the struct into
// memory and made the per-column mouth loops ~3.5x slower (measured: 12.4 ms vs 3.5 ms per frame with an open mouth).
inline MouthShape mouthShape(const FaceParams& p) {
  MouthShape m;
  m.open = clampf(p.mouth_open, 0, 1);
  m.A = MOUTH_A * clampf(p.mouth_curve, -1, 1);
  m.K = MOUTH_SKEW * clampf(p.mouth_skew, -1, 1);
  m.cat = S.mouth_style == 1;
  m.hw = clampf(S.mouth_w * (1 - 0.45f * m.open) * (1 + 0.4f * clampf(p.mouth_width, -1, 1)), 6.f, 50.f);
  m.cy = (float)MOUTH_Y;
  m.kind = S.mouth_style;
  m.wob = m.kind == 1   ? 8.f
          : m.kind == 3 ? 4.f
          : m.kind == 4 ? 4.f
          : m.kind == 5 ? 8.f
          : m.kind == 6 ? 10.f
          : m.kind == 7 ? 12.f
          : m.kind == 8 ? 9.f
                        : 0.f;
  if (m.kind == 6) {
    m.A = 0;
    m.hw = clampf(m.hw * (1 + 0.3f * clampf(p.mouth_curve, -1, 1)), 6.f, 50.f);
  }
  return m;
}

struct Fx {
    uint8_t eyes, deco;
    float t;
    const char* text;
};
bool drawEyeFx(Canvas& s, int ox, int oy, const Fx& fx, const FaceParams& p, const EyeBox& e, bool left);
void drawGlitchFx(Canvas& s, int ox, int oy, const Fx& fx, const EyeBox& e, bool left);
void drawDecoFx(Canvas& s, int ox, int oy, const Fx& fx, const FaceParams& p, const EyeBox& e, bool left);
void drawTongueFx(Canvas& s, int ox, int oy, float x, float y);
}  // namespace rd
