#pragma once
#include <math.h>
#include <stdint.h>

// Whole-number (fixed point) helpers for the two neural nets (mind.h, look_net.h), which work in 16-bit numbers instead of floats.
// A signal is an int16 over its whole range: 32767 is 1 ("Q15"). Sums are 32-bit with 19 bits after the point (ACC).
namespace fx {
constexpr int32_t SIG_ONE = 32767;
constexpr int ACC = 19;

// A shift right that rounds to the nearest (a plain shift always rounds down, which would slowly drag every decaying value below zero).
inline int32_t rsh(int32_t v, int s) { return (v + (1 << (s - 1))) >> s; }
inline int64_t rsh64(int64_t v, int s) { return (v + ((int64_t)1 << (s - 1))) >> s; }
inline int32_t clamp16(int64_t v) { return (int32_t)(v < -32767 ? -32767 : v > 32767 ? 32767 : v); }

inline float sigF(int32_t q) { return q * (1.f / SIG_ONE); }
// A float as a signal, held to -1..1 (NaN: 0).
inline int16_t sigQ(float v) {
  if (!(v == v)) return 0;
  const float c = v < -1 ? -1 : v > 1 ? 1 : v;
  return (int16_t)lroundf(c * SIG_ONE);
}

// tanh of a sum (Q19) as a signal (Q15): a table of tanh at steps of 1/64 up to 6 (past that it is 1), and a straight line between
// two entries. The error is under one step of the result.
inline int32_t tanhQ(int32_t s) {
  constexpr int STEP_BITS = ACC - 6, STEPS = 6 * 64;
  static int16_t table[STEPS + 2];
  static bool built = false;
  if (!built) {
    for (int i = 0; i <= STEPS + 1; i++) table[i] = (int16_t)lroundf(tanhf(i / 64.f) * SIG_ONE);
    built = true;
  }
  const uint32_t a = s < 0 ? (uint32_t)-s : (uint32_t)s, i = a >> STEP_BITS;
  if (i >= (uint32_t)STEPS) return s < 0 ? -SIG_ONE : SIG_ONE;
  const int32_t f = (int32_t)(a & ((1u << STEP_BITS) - 1));
  const int32_t y = table[i] + (((table[i + 1] - table[i]) * f) >> STEP_BITS);
  return s < 0 ? -y : y;
}
}  // namespace fx
