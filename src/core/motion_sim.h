#pragma once
#include <math.h>
#include "motion.h"

enum SimScenario : uint8_t {
  SIM_STILL,
  SIM_KNOCK,
  SIM_SHAKE,
  SIM_PICKUP,
  SIM_FALL,
  SIM_FACEDOWN,
  SIM_UPSIDEDOWN,
  SIM_ROCK,
  SIM_SPIN,
  SIM_COUNT
};
static const char* const SIM_NAMES[SIM_COUNT] = {"still",      "knock", "shake", "pickup", "fall",
                                                 "facedown",   "upsidedown", "rock", "spin"};
static const float SIM_SECONDS[SIM_COUNT] = {20.f, 3.f, 3.f, 5.f, 3.f, 5.f, 6.f, 9.f, 4.f};

namespace motion_sim {
inline void gravity(float backDeg, float flipDeg, float g[3]) {
  const float b = backDeg * 0.0174533f, f = flipDeg * 0.0174533f;
  const float y = cosf(b), z = sinf(b);
  g[0] = -y * sinf(f);
  g[1] = y * cosf(f);
  g[2] = z;
}
inline float noise(float t, float seed) {
  return 0.004f * sinf(t * 91.3f + seed) + 0.003f * sinf(t * 37.7f + seed * 2.f);
}
inline float smooth(float t, float a, float b) {
  if (t <= a) return 0;
  if (t >= b) return 1;
  float x = (t - a) / (b - a);
  return x * x * (3 - 2 * x);
}
}  // namespace motion_sim

inline MotionSample simSample(int scenario, float t) {
  using namespace motion_sim;
  float back = 0, flip = 0, lin[3] = {0, 0, 0}, gyro[3] = {0, 0, 0};
  float magScale = 1.f;
  switch (scenario) {
    case SIM_KNOCK:
      if (t >= 1.0f && t < 1.03f) lin[2] = 1.4f;
      if (t >= 1.03f && t < 1.4f) lin[2] = 0.35f * expf(-(t - 1.03f) * 25.f) * sinf(60.f * t);
      break;
    case SIM_SHAKE:
      if (t >= 0.5f && t < 1.9f) {
        lin[0] = 2.2f * sinf(6.2832f * 6.f * t);
        gyro[0] = 200.f * sinf(6.2832f * 6.f * t + 1.f);
      }
      break;
    case SIM_PICKUP:  // lifted and tilted back, carried, then set down at an angle
      back = 35.f * smooth(t, 0.5f, 1.2f);
      if (t >= 0.5f && t < 3.0f) {
        lin[1] = 0.22f * sinf(6.2832f * 3.f * t);
        lin[2] = 0.12f * sinf(6.2832f * 2.f * t + 1.f);
        gyro[0] = 70.f * sinf(6.2832f * 2.5f * t);
        gyro[2] = 30.f * sinf(6.2832f * 1.7f * t);
      }
      break;
    case SIM_FALL:
      if (t >= 0.5f && t < 0.9f) magScale = 0.05f;
      if (t >= 0.9f && t < 0.93f) lin[1] = 3.5f;
      break;
    case SIM_FACEDOWN:
      back = -90.f * smooth(t, 0.5f, 1.4f);
      if (t >= 0.5f && t < 2.2f) {
        lin[1] = 0.2f * sinf(6.2832f * 3.f * t);
        gyro[0] = 90.f * sinf(6.2832f * 2.f * t);
      }
      break;
    case SIM_UPSIDEDOWN:
      flip = 180.f * smooth(t, 0.5f, 1.4f);
      if (t >= 0.5f && t < 2.2f) {
        lin[0] = 0.2f * sinf(6.2832f * 3.f * t);
        gyro[2] = 100.f * sinf(6.2832f * 2.f * t);
      }
      break;
    case SIM_ROCK:  // held in the arms and rocked to and fro, a swing every 0.7 s
      if (t >= 0.5f && t < 8.5f) {
        back = 20.f + 15.f * sinf(6.2832f * 0.7f * t);
        gyro[0] = 66.f * cosf(6.2832f * 0.7f * t);
        lin[1] = 0.03f * sinf(6.2832f * 1.4f * t);
      }
      break;
    case SIM_SPIN:  // lying screen up, spun round on the table
      back = 90.f;
      if (t >= 0.5f && t < 2.5f) {
        gyro[2] = 300.f * smooth(t, 0.5f, 0.7f);
        lin[0] = 0.05f * sinf(6.2832f * 5.f * t);
      }
      break;
    default: break;
  }
  float g[3];
  gravity(back, flip, g);
  MotionSample s;
  s.ax = (g[0] + lin[0]) * magScale + noise(t, 1.f);
  s.ay = (g[1] + lin[1]) * magScale + noise(t, 2.f);
  s.az = (g[2] + lin[2]) * magScale + noise(t, 3.f);
  s.gx = gyro[0] + 0.4f * noise(t, 4.f) * 100.f;
  s.gy = gyro[1] + 0.4f * noise(t, 5.f) * 100.f;
  s.gz = gyro[2] + 0.4f * noise(t, 6.f) * 100.f;
  return s;
}
