#pragma once
#include <math.h>
#include <stddef.h>
#include "face_params.h"

class FaceSpring {
  public:
    static constexpr int N = sizeof(FaceParams) / sizeof(float);
    static_assert(sizeof(FaceParams) == N * sizeof(float), "FaceParams must be all floats");

    void reset(const FaceParams& p) {
      _x = p;
      for (float& v : _v) v = 0;
    }

    void setFeel(float speed, float bounce) {
      _speed = speed;
      _damp = 1.6f - 1.2f * bounce;
    }

    void kick(int field, float dv) { _v[field] += dv; }

    bool step(const FaceParams& target, float dt) {
      static const float OMEGA[N] = {22, 21, 26, 26, 14, 13, 12, 15, 16, 5, 4, 14, 10, 12, 14};
      static const float ZETA[N] = {0.75, 0.75, 0.8, 0.8, 0.6, 0.55, 0.5, 0.5,
                                    0.3,  1.0,  1.0, 0.6, 0.6, 0.5,  0.55};
      const float* t = reinterpret_cast<const float*>(&target);
      float* x = reinterpret_cast<float*>(&_x);
      bool moving = false;
      // Substep so stiff springs stay stable even if a frame runs long.
      int sub = (int)ceilf(dt / 0.005f);
      if (sub < 1) sub = 1;
      float h = dt / sub;
      for (int i = 0; i < N; i++) {
        float goal = t[i];
        if (i == HUE) goal = x[i] + wrap180(t[i] - x[i]);
        const float w = OMEGA[i] * _speed, z = fminf(ZETA[i] * _damp, 1.5f);
        for (int s = 0; s < sub; s++) {
          _v[i] += (w * w * (goal - x[i]) - 2 * z * w * _v[i]) * h;
          x[i] += _v[i] * h;
        }
        float eps = i == HUE ? 0.2f : 0.002f;
        if (fabsf(goal - x[i]) < eps && fabsf(_v[i]) < eps * 5) {
          x[i] = goal;
          _v[i] = 0;
        } else {
          moving = true;
        }
        if (i == HUE) x[i] = fmodf(x[i] + 360.f, 360.f);
      }
      return moving;
    }

    const FaceParams& value() const { return _x; }

    static constexpr int SQUASH = offsetof(FaceParams, squash) / sizeof(float);
    static constexpr int HUE = offsetof(FaceParams, hue) / sizeof(float);

  private:
    static float wrap180(float d) {
      d = fmodf(d + 180.f, 360.f);
      return (d < 0 ? d + 360.f : d) - 180.f;
    }

    FaceParams _x{};
    float _speed = 1, _damp = 1;
    float _v[N]{};
};
