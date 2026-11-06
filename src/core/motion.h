#pragma once
#include <math.h>
#include <stdint.h>

struct MotionSample {
    float ax, ay, az;
    float gx, gy, gz;
};

enum MotionEvent : uint8_t {
  ME_NONE,
  ME_KNOCK,
  ME_SHAKE,
  ME_PICKUP,
  ME_PUTDOWN,
  ME_FALL,
  ME_FACEDOWN,
  ME_FACEUP,
  ME_UPSIDEDOWN,
  ME_UPRIGHT,
  ME_ROCK,  // rocked slowly to and fro, like a baby (repeats while the rocking goes on)
  ME_SPIN,  // spun round on the table
  ME_COUNT
};
static const char* const MOTION_EVENT_NAMES[ME_COUNT] = {"none",       "knock",   "shake", "pickup",
                                                          "putdown",    "fall",    "facedown", "faceup",
                                                          "upsidedown", "upright", "rock",  "spin"};
enum Orient : uint8_t { OR_UNKNOWN, OR_UPRIGHT, OR_UPSIDE, OR_FACEUP, OR_FACEDOWN, OR_SIDE };

struct MotionOut {
    uint8_t event = ME_NONE;
    float intensity = 0;
};

class MotionClassifier {
  public:
    void setSensitivity(float s) { _sens = s < 0.2f ? 0.2f : s > 5.f ? 5.f : s; }

    MotionOut update(const MotionSample& s, float dt) {
      MotionOut out;
      const float a[3] = {s.ax, s.ay, s.az};
      const float mag = norm(a);
      if (!_init) {
        for (int i = 0; i < 3; i++) _g[i] = _rest[i] = a[i];
        _cand = _stable = classify(_g);
        _init = true;
      }
      const float th = 1.f / _sens;
      for (float* t : {&_cdKnock, &_cdShake, &_cdFall})
        if (*t > 0) *t -= dt;

      if (fabsf(mag - 1.f) < 0.3f) {
        const float ga = 1.f - expf(-dt / (_carried ? 0.35f : 0.8f));
        for (int i = 0; i < 3; i++) _g[i] += (a[i] - _g[i]) * ga;
      }
      const float lin[3] = {a[0] - _g[0], a[1] - _g[1], a[2] - _g[2]};
      const float lmag = norm(lin);
      const float rot = sqrtf(s.gx * s.gx + s.gy * s.gy + s.gz * s.gz) / 300.f;
      const float ePrev = _e;
      const float k = 1.f - expf(-dt / 0.2f);
      _e += (lmag - _e) * k;
      _ge += (rot - _ge) * k;

      const bool moving = _e > 0.05f * th || _ge > 0.12f * th;
      const bool still = _e < 0.025f * th && _ge < 0.05f * th;
      if (mag < 0.5f) {
      } else if (moving) {
        _moveT += dt;
        _stillT = 0;
      } else if (still) {
        _stillT += dt;
        _moveT = 0;
        if (_stillT > 0.5f) copy3(_rest, _g);
      }

      if (mag < 0.35f) {
        _ff += dt;
        if (_ff >= 0.12f && !_ffFired && _cdFall <= 0) {
          _ffFired = true;
          _cdFall = 2.f;
          out.event = ME_FALL;
          out.intensity = 1.f;
        }
      } else if (mag > 0.6f) {
        if (_ff >
            0.05f) {  // landed: the free fall isn't "being carried", and the thud isn't a separate knock
          _e = _ge = _moveT = 0;
          _cdKnock = 0.6f;
        }
        _ff = 0;
        _ffFired = false;
      }

      if (out.event == ME_NONE && lmag > 0.5f * th && ePrev < 0.05f * th && rot < 0.1f * th && !_carried &&
          _cdKnock <= 0 && _ff == 0) {
        _cdKnock = 0.5f;
        out.event = ME_KNOCK;
        out.intensity = clamp01(lmag / 2.f);
      }

      {
        int ax = 0;
        for (int i = 1; i < 3; i++)
          if (fabsf(lin[i]) > fabsf(lin[ax])) ax = i;
        if (fabsf(lin[ax]) > 0.45f * th) {
          const int sign = lin[ax] > 0 ? 1 : -1;
          if (_lastSign != 0 && sign != _lastSign) {
            for (int i = 0; i < 5; i++) _rev[i] = _rev[i + 1];
            _rev[5] = _t;
          }
          _lastSign = sign;
        }
        int recent = 0;
        for (float r : _rev)
          if (r > 0 && _t - r < 1.0f) recent++;
        // ...and it is still going on (the last reversal was just now), so a finished shake isn't reported late
        if (out.event == ME_NONE && recent >= 4 && _cdShake <= 0 && _t - _rev[5] < 0.3f) {
          _cdShake = 1.5f;
          for (float& r : _rev) r = 0;
          out.event = ME_SHAKE;
          out.intensity = clamp01(_e * 2.f);
        }
      }
      _t += dt;

      if (out.event == ME_NONE && !_carried && _moveT > 0.25f &&
          (dot(unit(_g), unit(_rest)) < 0.985f || _moveT > 0.6f)) {
        _carried = true;
        out.event = ME_PICKUP;
        out.intensity = clamp01(_e * 3.f);
      } else if (out.event == ME_NONE && _carried && _stillT > 0.7f) {
        _carried = false;
        out.event = ME_PUTDOWN;
        out.intensity = 0.3f;
      }

      // Rocking: the turn rate on its main axis swings to and fro, each swing 0.3..1.3 s, gently (not a shake), four
      // swings in a row; then again every couple of seconds while it goes on.
      {
        const float g3[3] = {s.gx, s.gy, s.gz};
        const float kr = 1.f - expf(-dt / 0.5f);
        for (int i = 0; i < 3; i++) _gAbs[i] += (fabsf(g3[i]) - _gAbs[i]) * kr;
        int ax = 0;
        for (int i = 1; i < 3; i++)
          if (_gAbs[i] > _gAbs[ax]) ax = i;
        const float w = g3[ax];
        _swingPeak = fmaxf(_swingPeak, fabsf(w));
        _swingT += dt;
        const int sign = w > 15.f ? 1 : w < -15.f ? -1 : 0;
        if (sign && _swingSign && sign != _swingSign) {
          const bool gentle = _swingT >= 0.3f && _swingT <= 1.3f && _swingPeak > 20.f * th && _swingPeak < 250.f;
          _swings = gentle ? _swings + 1 : 0;
          _swingT = 0;
          _swingPeak = 0;
        }
        if (sign) _swingSign = sign;
        if (_swingT > 1.6f) _swings = 0;  // stopped
        if (_cdRock > 0) _cdRock -= dt;
        if (out.event == ME_NONE && _swings >= 4 && _cdRock <= 0) {
          _cdRock = 2.5f;
          out.event = ME_ROCK;
          out.intensity = clamp01(_swings / 10.f);
        }
      }
      // Spinning: turning round the screen's axis, fast and steadily, for most of a second.
      {
        if (fabsf(s.gz) > 120.f * th && fabsf(s.gx) < 0.6f * fabsf(s.gz) && fabsf(s.gy) < 0.6f * fabsf(s.gz))
          _spinT += dt;
        else
          _spinT = fmaxf(0.f, _spinT - 2 * dt);
        if (_cdSpin > 0) _cdSpin -= dt;
        if (out.event == ME_NONE && _spinT > 0.7f && _cdSpin <= 0) {
          _cdSpin = 3.f;
          _spinT = 0;
          out.event = ME_SPIN;
          out.intensity = clamp01(fabsf(s.gz) / 400.f);
        }
      }

      // Orientation: reported once it has stayed the same for a while, and not while it is being carried.
      const uint8_t cand = classify(_g);
      if (cand == _cand)
        _candT += dt;
      else {
        _cand = cand;
        _candT = 0;
      }
      if (out.event == ME_NONE && !_carried && _candT > 1.0f && _cand != _stable) {
        if (_cand == OR_FACEDOWN)
          out.event = ME_FACEDOWN;
        else if (_stable == OR_FACEDOWN)
          out.event = ME_FACEUP;
        else if (_cand == OR_UPSIDE)
          out.event = ME_UPSIDEDOWN;
        else if (_stable == OR_UPSIDE)
          out.event = ME_UPRIGHT;
        out.intensity = 0.5f;
        _stable = _cand;
      }
      return out;
    }

    // Tilt for the gaze (-1..1): x to the right, y down. It is the change from the pose it last rested in, not the angle
    // itself: turn it and the eyes lean that way, hold it still and they settle back to the middle (the rest pose follows
    // it once it has been still for half a second).
    float tiltX() const { return clampf((_g[0] - _rest[0]) / 0.5f); }
    float tiltY() const { return clampf(-(_g[2] - _rest[2]) / 0.5f); }
    // Lying (or held) flat, screen up: which way is downhill (-1..1, x to the right, y down), for eyes that roll there
    // like marbles.
    float flatX() const { return clampf(-_g[0] / 0.35f); }
    float flatY() const { return clampf(_g[1] / 0.35f); }
    // Standing up: how far it is turned round the screen's axis, for a face that stays level. Degrees: turned clockwise
    // (its right side down) by t gives -t. 0 when it is not standing up.
    float rollDeg() const {
      const float n = norm(_g);
      if (n < 0.5f || fabsf(_g[2]) / n > 0.6f) return 0;
      return atan2f(_g[0], _g[1]) * 57.2958f;
    }
    uint8_t orientation() const { return _stable; }
    bool carried() const { return _carried; }
    float energy() const { return _e; }

  private:
    static float clampf(float v) { return v < -1 ? -1 : v > 1 ? 1 : v; }
    static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
    static float norm(const float v[3]) { return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }
    static float dot(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
    static void copy3(float* d, const float* s) {
      d[0] = s[0];
      d[1] = s[1];
      d[2] = s[2];
    }
    const float* unit(const float v[3]) {
      float n = norm(v);
      _u[_ui ^= 1][0] = n > 1e-3f ? v[0] / n : 0;
      _u[_ui][1] = n > 1e-3f ? v[1] / n : 0;
      _u[_ui][2] = n > 1e-3f ? v[2] / n : 0;
      return _u[_ui];
    }
    static uint8_t classify(const float g[3]) {
      float n = norm(g);
      if (n < 0.5f) return OR_UNKNOWN;
      const float x = g[0] / n, y = g[1] / n, z = g[2] / n;
      if (z > 0.8f) return OR_FACEUP;
      if (z < -0.8f) return OR_FACEDOWN;
      if (y > 0.7f) return OR_UPRIGHT;
      if (y < -0.7f) return OR_UPSIDE;
      return fabsf(x) > 0.7f ? OR_SIDE : OR_UNKNOWN;
    }

    float _sens = 1.f;
    bool _init = false, _carried = false, _ffFired = false;
    float _g[3] = {0, 1, 0}, _rest[3] = {0, 1, 0}, _u[2][3] = {};
    int _ui = 0, _lastSign = 0;
    float _e = 0, _ge = 0, _moveT = 0, _stillT = 0, _ff = 0, _t = 0, _candT = 0;
    float _cdKnock = 0, _cdShake = 0, _cdFall = 0, _rev[6] = {};
    float _gAbs[3] = {}, _swingT = 0, _swingPeak = 0, _cdRock = 0, _spinT = 0, _cdSpin = 0;
    int _swingSign = 0, _swings = 0;
    uint8_t _cand = OR_UNKNOWN, _stable = OR_UNKNOWN;
};
