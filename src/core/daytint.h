#pragma once
#include <math.h>

struct DayTint {
    float hue = 0;
    float val = 1;
};

inline DayTint dayTintAt(float hour, float amount) {
  static const float K[][3] = {{0, 12, 0.78f}, {5.5f, 12, 0.78f},   {7.5f, -5, 0.93f}, {10, 0, 1.f},
                               {16, 0, 1.f},   {18.5f, -14, 0.95f}, {21.5f, 6, 0.85f}, {24, 12, 0.78f}};
  DayTint t;
  if (amount <= 0 || hour < 0) return t;
  hour = fmodf(hour, 24.f);
  for (unsigned i = 1; i < sizeof K / sizeof K[0]; i++)
    if (hour <= K[i][0]) {
      float u = (hour - K[i - 1][0]) / (K[i][0] - K[i - 1][0]);
      u = u * u * (3 - 2 * u);
      t.hue = (K[i - 1][1] + (K[i][1] - K[i - 1][1]) * u) * amount;
      t.val = 1 + (K[i - 1][2] + (K[i][2] - K[i - 1][2]) * u - 1) * amount;
      break;
    }
  return t;
}
