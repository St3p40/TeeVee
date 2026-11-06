#pragma once
#include <math.h>
#include <stdint.h>
#include "emotes.h"
#include "mind.h"

inline uint8_t mindPickEmote(const float* urge, uint32_t r, const float* liked = nullptr) {
  using namespace mind;
  const float mirth = urge[U_MIRTH], curious = urge[U_CURIOUS], susp = urge[U_SUSPICIOUS],
              drowsy = urge[U_DROWSY], restless = urge[U_RESTLESS];
  float best = 0.15f;
  uint8_t e = EM_MUSIC;
  auto consider = [&](float v, uint8_t a, uint8_t b, uint8_t c) {
    if (v > best) {
      best = v;
      const uint8_t pick[3] = {a, b, c};
      e = pick[r % 3];
      if (liked) {
        float w[3], sum = 0;
        for (int i = 0; i < 3; i++) sum += w[i] = expf(1.1f * liked[pick[i]]);
        float x = (r % 1024) / 1024.f * sum;
        for (int i = 0; i < 3; i++) {
          e = pick[i];
          if ((x -= w[i]) <= 0) break;
        }
      }
    }
  };
  consider(mirth, EM_HEART, EM_MUSIC, EM_SPARKLE);
  consider(curious, EM_QUESTION, EM_THINK, EM_STAR);
  consider(susp, EM_THINK, EM_SWEAT, EM_QUESTION);
  consider(drowsy, EM_SHY, EM_SWEAT, EM_THINK);
  consider(restless, EM_MUSIC, EM_STAR, EM_DIZZY);
  return e;
}

// The gaze urge (-1..1) -> a tilt (-1..1) with a dead zone of 0.25 around level.
inline float tiltFrom(float u) {
  const float a = u < 0 ? -u : u, t = (a - 0.1f) / 0.9f * 2.5f;
  const float m = t < 0 ? 0 : t > 1 ? 1 : t;
  return u < 0 ? -m : m;
}
inline float lensFrom(float u) {
  const float t = (u - 0.3f) / 0.7f;
  return t < 0 ? 0 : t > 1 ? 1 : t;
}
