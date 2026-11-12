#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "fixed.h"
#include "style.h"

// The look net (desktop and web): a small network that scores a look for the moment (how it feels, what it needs) and learns from
// how the owner reacts to the look it wears. Like the mind net it works in 16-bit whole numbers, not floats:
//   inputs and the two hidden layers   int16, 32767 is 1 (fx::SIG_ONE; "Q15")
//   weights                            int16, 16384 is 1 (W_ONE; "Q14"), so they are held to +-2
// What goes in and comes out (score, train, and a save: flatten / unflatten) is still floats, so a save is the same file as before
// and an old one loads: a weight is a whole number of 1/16384, which a float holds exactly.
class LookNet {
  public:
    static constexpr int CTX = 12, CAND = 11 + STYLE_PRESET_COUNT, IN = CTX + CAND, H1 = 64, H2 = 32;
    static constexpr int WEIGHTS = IN * H1 + H1 + H1 * H2 + H2 + H2 + 1;
    static constexpr uint32_t MAGIC = 0x4C4B4E32u, MAGIC_OLD = 0x4C4B4E31u;
    static constexpr int32_t W_ONE = 16384;

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
        for (int16_t& v : row) v = wQ(rnd() * 0.25f);
      for (int16_t& v : b1) v = 0;
      for (auto& row : W2)
        for (int16_t& v : row) v = wQ(rnd() * 0.25f);
      for (int16_t& v : b2) v = 0;
      for (int16_t& v : w3) v = wQ(rnd() * 0.25f);
      b3 = 0;
      updates = 0;
    }

    float score(const float* x) const {
      int16_t xq[IN], h1[H1], h2[H2];
      return fx::sigF(forward(x, xq, h1, h2));
    }

    // One step of learning: the score for x moves towards t (backpropagation, all in whole numbers; the sums that can be large are 64-bit).
    void train(const float* x, float t, float lr) {
      using namespace fx;
      int16_t xq[IN], h1[H1], h2[H2];
      const int32_t ds = forward(x, xq, h1, h2) - (int32_t)lroundf(t * SIG_ONE);  // Q15, not held to +-1: the score can pass it
      const int64_t rate = (int64_t)lroundf((lr < 0 ? 0 : lr > 1 ? 1 : lr) * 65536.f);  // Q16
      int32_t dh2[H2], dh1[H1];  // Q15
      for (int k = 0; k < H2; k++)
        dh2[k] = (int32_t)rsh64(rsh64((int64_t)ds * w3[k], 14) * (SIG_ONE - ((h2[k] * h2[k]) >> 15)), 15);
      for (int j = 0; j < H1; j++) {
        int64_t g = 0;
        for (int k = 0; k < H2; k++) g += (int64_t)W2[k][j] * dh2[k];
        dh1[j] = (int32_t)rsh64(rsh64(g, 14) * (SIG_ONE - ((h1[j] * h1[j]) >> 15)), 15);
      }
      // weight (Q14) -= rate (Q16) x gradient (Q15) x signal (Q15): Q46 -> Q14; a bias has no signal: Q31 -> Q14
      auto moved = [&](int16_t w, int64_t grad, int32_t sig) {
        return (int16_t)clamp16(w - rsh64(rate * grad * sig, 32));
      };
      auto movedBias = [&](int16_t w, int64_t grad) { return (int16_t)clamp16(w - rsh64(rate * grad, 17)); };
      for (int k = 0; k < H2; k++) {
        w3[k] = moved(w3[k], ds, h2[k]);
        for (int j = 0; j < H1; j++) W2[k][j] = moved(W2[k][j], dh2[k], h1[j]);
        b2[k] = movedBias(b2[k], dh2[k]);
      }
      b3 = movedBias(b3, ds);
      for (int j = 0; j < H1; j++) {
        if (!dh1[j]) continue;
        for (int i = 0; i < IN; i++) W1[j][i] = moved(W1[j][i], dh1[j], xq[i]);
        b1[j] = movedBias(b1[j], dh1[j]);
      }
      updates++;
    }

    uint32_t updates = 0;

    // A save is the weights as floats, in this order (the file format from before the net held whole numbers).
    void flatten(float* o) const {
      float* p = o;
      for (auto& row : W1)
        for (int16_t v : row) *p++ = wF(v);
      for (int16_t v : b1) *p++ = wF(v);
      for (auto& row : W2)
        for (int16_t v : row) *p++ = wF(v);
      for (int16_t v : b2) *p++ = wF(v);
      for (int16_t v : w3) *p++ = wF(v);
      *p = wF(b3);
    }
    static int inputsOf(long weights) {
      const long rest = (long)H1 + H1 * H2 + H2 + H2 + 1, in = (weights - rest) / H1;
      return weights > rest && (weights - rest) % H1 == 0 && in >= CTX + 11 && in <= IN ? (int)in : -1;
    }
    void unflattenFrom(const float* src, int in) {
      const float* p = src;
      for (auto& row : W1)
        for (int i = 0; i < IN; i++) row[i] = i < in ? wQ(*p++) : row[i];
      rest(p);
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
            row[to] = wQ(v);
          else if (to >= 0)
            row[CTX + 11 + to] = wQ(v);
        }
      rest(p);
    }
    void unflatten(const float* in) { unflattenFrom(in, IN); }

  private:
    static float wF(int32_t q) { return q * (1.f / W_ONE); }
    // A float as a weight, held to what an int16 can be (just under +-2; NaN: 0).
    static int16_t wQ(float v) {
      if (!(v == v)) return 0;
      const float q = v * W_ONE;
      return (int16_t)(q < -32767 ? -32767 : q > 32767 ? 32767 : lroundf(q));
    }
    // Everything after the first layer's weights, the same in every save.
    void rest(const float* p) {
      for (int16_t& v : b1) v = wQ(*p++);
      for (auto& row : W2)
        for (int16_t& v : row) v = wQ(*p++);
      for (int16_t& v : b2) v = wQ(*p++);
      for (int16_t& v : w3) v = wQ(*p++);
      b3 = wQ(*p);
    }
    // The inputs as signals (xq), the two hidden layers, and the score (Q15, 32-bit: it is not held to +-1).
    // Products are weight (Q14) x signal (Q15) = Q29, added up at Q19.
    int32_t forward(const float* x, int16_t* xq, int16_t* h1, int16_t* h2) const {
      using namespace fx;
      for (int i = 0; i < IN; i++) xq[i] = sigQ(x[i]);
      for (int j = 0; j < H1; j++) {
        int32_t s = b1[j] * (1 << (ACC - 14));
        for (int i = 0; i < IN; i++) s += (W1[j][i] * xq[i]) >> (29 - ACC);
        h1[j] = (int16_t)tanhQ(s);
      }
      for (int k = 0; k < H2; k++) {
        int32_t s = b2[k] * (1 << (ACC - 14));
        for (int j = 0; j < H1; j++) s += (W2[k][j] * h1[j]) >> (29 - ACC);
        h2[k] = (int16_t)tanhQ(s);
      }
      int32_t s = b3 * (1 << (ACC - 14));
      for (int k = 0; k < H2; k++) s += (w3[k] * h2[k]) >> (29 - ACC);
      return rsh(s, ACC - 15);
    }
    int16_t W1[H1][IN], b1[H1], W2[H2][H1], b2[H2], w3[H2], b3 = 0;  // Q14
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
