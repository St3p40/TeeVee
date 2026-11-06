#include "render_common.h"

namespace rd {
Rect REGIONS[3] = {
    {95 - (REG_W >> 1), 0, 170},
    {225 - (REG_W >> 1), 0, 170},
    {160 - (REG_W >> 1), 170, 70},
};

float MOUTH_PHASE = 0;
float DAY_HUE = 0, DAY_VAL = 1, PUPIL_MUL = 1;
int EYE_CX_L = 95, EYE_CX_R = 225, EYE_CY = 95, MOUTH_Y = 192;

// Current style (one renderer, core 1 only).
FaceStyle S = STYLE_DEFAULT;
uint16_t BG = 0;

uint16_t hsv565(float h, float s, float v) {
  h = fmodf(h, 360.f);
  if (h < 0) h += 360.f;
  float c = v * s, x = c * (1 - fabsf(fmodf(h / 60.f, 2.f) - 1)), m = v - c;
  float r, g, b;
  if (h < 60) {
    r = c;
    g = x;
    b = 0;
  } else if (h < 120) {
    r = x;
    g = c;
    b = 0;
  } else if (h < 180) {
    r = 0;
    g = c;
    b = x;
  } else if (h < 240) {
    r = 0;
    g = x;
    b = c;
  } else if (h < 300) {
    r = x;
    g = 0;
    b = c;
  } else {
    r = c;
    g = 0;
    b = x;
  }
  return Canvas::rgb((r + m) * 255, (g + m) * 255, (b + m) * 255);
}

void layout() {
  const int half = (int)lroundf(S.eye_gap / 2);
  EYE_CX_L = 160 - half;
  EYE_CX_R = 160 + half;
  EYE_CY = (int)lroundf(S.eye_y);
  MOUTH_Y = (int)lroundf(S.mouth_y);
  REGIONS[0].x = EYE_CX_L - (REG_W >> 1);
  REGIONS[1].x = EYE_CX_R - (REG_W >> 1);
}

uint16_t mix565(uint16_t a, uint16_t b, float t) {
  auto ch = [&](int sh, int mask) {
    return (int)(((a >> sh) & mask) * (1 - t) + ((b >> sh) & mask) * t) & mask;
  };
  return (uint16_t)(ch(11, 31) << 11 | ch(5, 63) << 5 | ch(0, 31));
}

uint16_t eyeColor(const FaceParams& p, bool left) {
  if (left || !S.eye_split || S.hue_r == 0.f) return faceColor(p);
  return hsv565(S.hue + DAY_HUE + S.hue_r + (S.tint ? p.hue : 0.f), S.sat, S.val * DAY_VAL);
}

uint16_t faceColor(const FaceParams& p, float valMul) {
  return hsv565(S.hue + DAY_HUE + (S.tint ? p.hue : 0.f), S.sat, S.val * DAY_VAL * valMul);
}

EyeBox eyeBox(const FaceParams& p, bool left) {
  const int cx = left ? EYE_CX_L : EYE_CX_R;
  const float sq = clampf(p.squash, -1, 1);
  const float raise = fmaxf(0.f, clampf(p.asym, -1, 1) * (left ? 1 : -1));
  float w = eyeWOf(left) * (1 - 0.25f * sq);
  float hFull =
      eyeHOf(left) * (1 + 0.3f * sq) * (1 + 0.15f * clampf(p.brow_height, -1, 1)) * (1 + 0.15f * raise);
  float h = fmaxf(6.f, hFull * clampf(left ? p.eye_open_l : p.eye_open_r, 0, 1));
  float gx = cx + clampf(p.pupil_x, -1, 1) * GAZE_X;
  float bottom = EYE_CY + eyeHOf(left) / 2 + clampf(p.pupil_y, -1, 1) * GAZE_Y;
  return {(int)lroundf(gx - w / 2),
          (int)lroundf(bottom - h),
          (int)lroundf(w),
          (int)lroundf(h),
          gx,
          bottom,
          hFull};
}

Brow brow(const FaceParams& p, const EyeBox& e, bool left) {
  const float dir = left ? -1.f : 1.f, a = clampf(p.brow_angle, -1, 1);
  const float y = e.bottom - e.hFull - 11 - 8 * clampf(p.brow_height, -1, 1);
  return {e.gx + dir * e.w * 0.48f, y - 4 * a, e.gx - dir * e.w * 0.32f, y + 8 * a, S.line_r + 0.5f};
}

uint16_t blushColor(float a) {
  a = clampf(a, 0, 1) * 0.8f;
  return Canvas::rgb(S.bg[0] + (255 - S.bg[0]) * a, S.bg[1] + (90 - S.bg[1]) * a,
                     S.bg[2] + (120 - S.bg[2]) * a);
}

}  // namespace rd
