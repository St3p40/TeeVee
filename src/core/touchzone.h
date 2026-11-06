#pragma once
#include <math.h>
#include "emotes.h"
#include "stimuli.h"
#include "style.h"

enum Zone : uint8_t { Z_NONE, Z_EYE_L, Z_EYE_R, Z_FOREHEAD, Z_CHEEK_L, Z_CHEEK_R, Z_MOUTH };

inline Zone zoneAt(const FaceStyle& s, int x, int y) {
  const float cx = 160, half = s.eye_gap * 0.5f, ew = s.eye_w * 0.5f + 10, eh = s.eye_h * 0.5f + 10,
              ey = s.eye_y, my = s.mouth_y;
  if (fabsf(y - ey) < eh) {
    if (fabsf(x - (cx - half)) < ew) return Z_EYE_L;
    if (fabsf(x - (cx + half)) < ew) return Z_EYE_R;
  }
  if (y < ey - eh) return Z_FOREHEAD;
  if (y > my - 24 && fabsf(x - cx) < s.mouth_w + 24) return Z_MOUTH;
  if (y > ey + eh * 0.2f && y < my + 10) {
    if (fabsf(x - (cx - half)) < ew + 25) return Z_CHEEK_L;
    if (fabsf(x - (cx + half)) < ew + 25) return Z_CHEEK_R;
  }
  return Z_NONE;
}

struct ZoneFx {
    uint8_t emote = EM_NONE;
    int8_t flinch = 0;
};

inline ZoneFx applyZone(Zone z, StimulusOut& so) {
  ZoneFx fx;
  const int8_t side = z == Z_EYE_L ? -1 : z == Z_EYE_R ? 1 : 0;
  for (int i = 0; i < so.nEvents; i++) {
    EmotionEvent& e = so.events[i];
    if (e.type == EV_POKE) {
      if (side) {
        fx.flinch = side;
        e.intensity = fminf(1.f, e.intensity + 0.3f);
      } else if (z == Z_MOUTH) {
        e = {EV_GREET, 0.3f};
        fx.emote = EM_LAUGH;
      } else if (z == Z_CHEEK_L || z == Z_CHEEK_R) {
        e = {EV_PET, 0.3f};
        fx.emote = EM_SHY;
      }
    } else if (e.type == EV_PET) {
      if (z == Z_FOREHEAD) {
        e.intensity = fminf(1.f, e.intensity * 1.6f);
      } else if (side) {
        e = {EV_POKE, 0.3f};
        fx.flinch = side;
      } else if (z == Z_CHEEK_L || z == Z_CHEEK_R) {
        fx.emote = EM_SHY;
      }
    }
  }
  return fx;
}
