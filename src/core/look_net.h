#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "style.h"

class LookNet {
  public:
    static constexpr int CTX = 12, CAND = 11 + STYLE_PRESET_COUNT, IN = CTX + CAND, H1 = 64, H2 = 32;
    static constexpr int WEIGHTS = IN * H1 + H1 + H1 * H2 + H2 + H2 + 1;
    static constexpr uint32_t MAGIC = 0x4C4B4E32u, MAGIC_OLD = 0x4C4B4E31u;

    LookNet() { init(0x51A3u); }
    void init(uint32_t seed) {
      uint32_t r = seed ? seed : 1;
      auto rnd = [&]() {
        r ^= r << 13;
        r ^= r >> 17;
        r ^= r << 5;
        return (r >> 8) / 16777216.f * 2.f - 1.f;
      };
      for (auto& row : W1)
        for (float& v : row) v = rnd() * 0.25f;
      for (float& v : b1) v = 0;
      for (auto& row : W2)
        for (float& v : row) v = rnd() * 0.25f;
      for (float& v : b2) v = 0;
      for (float& v : w3) v = rnd() * 0.25f;
      b3 = 0;
      updates = 0;
    }

    float score(const float* x) const {
      float h1[H1], h2[H2];
      forward(x, h1, h2);
      float s = b3;
      for (int k = 0; k < H2; k++) s += w3[k] * h2[k];
      return s;
    }

    void train(const float* x, float t, float lr) {
      float h1[H1], h2[H2];
      forward(x, h1, h2);
      float s = b3;
      for (int k = 0; k < H2; k++) s += w3[k] * h2[k];
      const float ds = s - t;
      float dh2[H2], dh1[H1];
      for (int k = 0; k < H2; k++) dh2[k] = ds * w3[k] * (1.f - h2[k] * h2[k]);
      for (int j = 0; j < H1; j++) {
        float g = 0;
        for (int k = 0; k < H2; k++) g += W2[k][j] * dh2[k];
        dh1[j] = g * (1.f - h1[j] * h1[j]);
      }
      for (int k = 0; k < H2; k++) {
        w3[k] -= lr * ds * h2[k];
        for (int j = 0; j < H1; j++) W2[k][j] -= lr * dh2[k] * h1[j];
        b2[k] -= lr * dh2[k];
      }
      b3 -= lr * ds;
      for (int j = 0; j < H1; j++) {
        for (int i = 0; i < IN; i++) W1[j][i] -= lr * dh1[j] * x[i];
        b1[j] -= lr * dh1[j];
      }
      updates++;
    }

    uint32_t updates = 0;

    void flatten(float* o) const {
      float* p = o;
      for (auto& row : W1)
        for (float v : row) *p++ = v;
      for (float v : b1) *p++ = v;
      for (auto& row : W2)
        for (float v : row) *p++ = v;
      for (float v : b2) *p++ = v;
      for (float v : w3) *p++ = v;
      *p = b3;
    }
    static int inputsOf(long weights) {
      const long rest = (long)H1 + H1 * H2 + H2 + H2 + 1, in = (weights - rest) / H1;
      return weights > rest && (weights - rest) % H1 == 0 && in >= CTX + 11 && in <= IN ? (int)in : -1;
    }
    void unflattenFrom(const float* src, int in) {
      const float* p = src;
      for (auto& row : W1)
        for (int i = 0; i < IN; i++) row[i] = i < in ? *p++ : row[i];
      for (float& v : b1) v = *p++;
      for (auto& row : W2)
        for (float& v : row) v = *p++;
      for (float& v : b2) v = *p++;
      for (float& v : w3) v = *p++;
      b3 = *p;
    }
    static constexpr int OLD_IN = CTX + 11 + STYLE_PRESETS_OLD_COUNT;
    static constexpr int OLD_WEIGHTS = OLD_IN * H1 + H1 + H1 * H2 + H2 + H2 + 1;
    static int oldInputsOf(long weights) {
      const long rest = (long)H1 + H1 * H2 + H2 + H2 + 1, in = (weights - rest) / H1;
      return weights > rest && (weights - rest) % H1 == 0 && in >= CTX + 11 && in <= OLD_IN ? (int)in : -1;
    }
    void unflattenOld(const float* src, int in) {
      const float* p = src;
      for (auto& row : W1)
        for (int i = 0; i < in; i++) {
          const float v = *p++;
          const int to = i < CTX + 11 ? i : styleOldPreset(i - CTX - 11);
          if (i < CTX + 11)
            row[to] = v;
          else if (to >= 0)
            row[CTX + 11 + to] = v;
        }
      for (float& v : b1) v = *p++;
      for (auto& row : W2)
        for (float& v : row) v = *p++;
      for (float& v : b2) v = *p++;
      for (float& v : w3) v = *p++;
      b3 = *p;
    }
    void unflatten(const float* in) {
      const float* p = in;
      for (auto& row : W1)
        for (float& v : row) v = *p++;
      for (float& v : b1) v = *p++;
      for (auto& row : W2)
        for (float& v : row) v = *p++;
      for (float& v : b2) v = *p++;
      for (float& v : w3) v = *p++;
      b3 = *p;
    }

  private:
    void forward(const float* x, float* h1, float* h2) const {
      for (int j = 0; j < H1; j++) {
        float s = b1[j];
        for (int i = 0; i < IN; i++) s += W1[j][i] * x[i];
        h1[j] = tanhf(s);
      }
      for (int k = 0; k < H2; k++) {
        float s = b2[k];
        for (int j = 0; j < H1; j++) s += W2[k][j] * h1[j];
        h2[k] = tanhf(s);
      }
    }
    float W1[H1][IN], b1[H1], W2[H2][H1], b2[H2], w3[H2], b3 = 0;
};

inline void lookFeatures(const FaceStyle& s, int preset, float* f) {
  f[0] = s.eye_style / (float)(EYE_COUNT - 1);
  f[1] = s.mouth_style / 9.f;
  f[2] = s.hue / 180.f - 1.f;
  f[3] = s.sat;
  f[4] = s.val;
  f[5] = (s.bg[0] + s.bg[1] + s.bg[2]) / 765.f;
  f[6] = s.pixel / 8.f;
  f[7] = s.glitch / 10.f;
  f[8] = s.daytint;
  f[9] = s.mouth_solid ? 1.f : 0.f;
  f[10] = s.pupil_style / (float)(PUPIL_COUNT - 1);
  for (int k = 0; k < STYLE_PRESET_COUNT; k++) f[11 + k] = k == preset ? 1.f : 0.f;
}
