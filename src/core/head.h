#pragma once
#include <math.h>
#include <stdint.h>

// The head turn of a board with a motion sensor: the sensor and the mind net together, and not as a mirror of either. Pure logic, no
// Arduino deps. Host test: test/head_test.cpp.
//   Weight      the sensor's lean is a mass on a spring: the eyes go first (idle.lookAt), the head follows a moment later and settles.
//   Mood        wound up, it swings further and quicker and wobbles past the mark; calm, it is slower and stops dead; sleepy, it barely bothers.
//   Boredom     turn it to and fro for a while and it follows less and less; leave it alone for half a minute and it is curious again.
//   Chance      every lean has its own strength: it is re-drawn each time the head comes to rest.
//   Its own     the mind net's head urges (and, a little, where the eyes look) turn it with nobody touching it; curiosity tips it to one
//               side, a side picked anew each time.
//   Asleep      its own turns stop; tilted, the head lolls over slowly, like a sleeper's.
//   Carried     the sensor's part lets go (the face would swing with every step); its own part stays.
// All outputs are fractions (-1..1) of the owner's angle (the setting `head_turn`), and exactly 0 at rest: the turned face is slow to draw
// on a board, so it must be level most of the time.
class HeadLife {
  public:
    struct In {
        float tiltX = 0, tiltY = 0;  // the sensor's tilt, the change from rest (-1..1); x to the right, y down
        bool sensor = false, carried = false, asleep = false;
        float gazeX = 0, gazeY = 0, headX = 0, headY = 0, curious = 0;  // the mind net's urges, already scaled by the mind amount
        float arousal = 0, sleepy = 0;  // -1..1 and 0..1
    };
    struct Out {
        float turnX, turnY, roll, lens;  // turnY: about the upright axis (it follows tiltX); turnX: nodding; lens: how far it leans, 0..1
    };

    void begin(uint32_t seed) {
      *this = HeadLife();
      _rng = (seed ? seed : 0x7EE5EEDu) * 2654435761u;  // (spread: near seeds must not give near first draws)
      if (!_rng) _rng = 0x7EE5EEDu;
      for (int i = 0; i < 4; i++) frand();
      _gain = 0.85f + 0.3f * frand();
    }

    Out step(const In& in, float dt) {
      if (!(dt > 0)) dt = 0.1f;
      if (dt > 0.5f) dt = 0.5f;
      const float a = clampf(in.arousal, -1, 1), up = a > 0 ? a : 0, sl = clampf(in.sleepy, 0, 1);

      // Boredom: how much it has been turned lately.
      const bool held = in.sensor && !in.carried;
      const float tx = held ? clampf(in.tiltX, -1, 1) : 0, ty = held ? clampf(in.tiltY, -1, 1) : 0;
      _bored += 0.15f * (fabsf(tx - _ptx) + fabsf(ty - _pty));
      if (_bored > 1) _bored = 1;
      _bored *= expf(-dt / BORED_TAU);
      _ptx = tx, _pty = ty;

      // The sensor's part: a spring towards the lean, tuned by the mood.
      const float want = (1 - 0.7f * _bored) * _gain * (in.asleep ? 0.45f : (0.75f + 0.35f * a) * (1 - 0.5f * sl));
      const float w = in.asleep ? 2.5f : 5.f + 5.f * up - 2.f * sl;
      const float zeta = in.asleep ? 1.2f : 0.75f - 0.35f * up + 0.4f * sl;
      const int n = (int)ceilf(dt / 0.025f);
      const float h = dt / n;
      for (int i = 0; i < n; i++) {
        _vx += (w * w * (tx * want - _sx) - 2 * zeta * w * _vx) * h;
        _vy += (w * w * (ty * want - _sy) - 2 * zeta * w * _vy) * h;
        _sx += _vx * h;
        _sy += _vy * h;
      }
      const bool still = tx == 0 && ty == 0 && fabsf(_sx) < REST && fabsf(_sy) < REST && fabsf(_vx) < 0.05f && fabsf(_vy) < 0.05f;
      if (still) {
        if (_moving) _gain = 0.85f + 0.3f * frand();  // the next lean is its own
        _sx = _sy = _vx = _vy = 0;
      }
      _moving = !still;

      // Its own part: the head urges and, a little, the gaze, as slow averages (about 2 s); it drifts there, quicker when wound up.
      const float k = 1 - expf(-dt / 2.f);
      _gx += (in.gazeX - _gx) * k;
      _gy += (in.gazeY - _gy) * k;
      _hx += (in.headX - _hx) * k;
      _hy += (in.headY - _hy) * k;
      const float rate = fminf(1.f, (0.8f + 1.f * up) * dt);
      _mx += ((in.asleep ? 0.f : 0.6f * curve(0.7f * _hx + 0.3f * _gx)) - _mx) * rate;
      _my += ((in.asleep ? 0.f : 0.6f * curve(0.7f * _hy + 0.3f * _gy)) - _my) * rate;
      if (in.curious < 0.1f) _side = frand() < 0.5f ? 1.f : -1.f;
      const float tip = in.curious > 0.1f ? fminf(0.9f, in.curious - 0.1f) : 0.f;
      _roll += ((in.asleep ? 0.f : _side * 0.5f * tip) - _roll) * rate;

      Out o;
      o.turnY = rest(clampf(_sx + _mx, -1, 1));
      o.turnX = rest(clampf(_sy + _my, -1, 1));
      o.roll = rest(_roll);
      o.lens = fminf(1.f, hypotf(_sx, _sy));
      if (o.lens < REST) o.lens = 0;
      return o;
    }

    float bored() const { return _bored; }

    static constexpr float BORED_TAU = 30.f;  // seconds for the boredom to fade
    static constexpr float REST = 0.015f;     // below this the head is level (0.3 degrees of a 20 degree setting)

  private:
    static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
    static float rest(float v) { return fabsf(v) < REST ? 0.f : v; }
    // An urge (-1..1) -> a turn: nothing inside a dead band of 0.15, then a soft curve up to +-1 (the desktop's head does the same).
    static float curve(float u) {
      const float a = fabsf(u) - 0.15f;
      if (a <= 0) return 0.f;
      const float s = tanhf(2.f * a / 0.85f);
      return u < 0 ? -s : s;
    }
    float frand() {
      _rng ^= _rng << 13, _rng ^= _rng >> 17, _rng ^= _rng << 5;
      return (_rng >> 8) / 16777216.f;
    }

    uint32_t _rng = 0x7EE5EEDu;
    float _sx = 0, _sy = 0, _vx = 0, _vy = 0, _ptx = 0, _pty = 0, _bored = 0, _gain = 1;
    float _gx = 0, _gy = 0, _hx = 0, _hy = 0, _mx = 0, _my = 0, _roll = 0, _side = 1;
    bool _moving = false;
};
