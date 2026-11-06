#pragma once
#include <math.h>
#include <stdint.h>
#include "emotion.h"

// Local stimuli (core 0): touch gestures, ambient light, time of day. Pure logic, no Arduino deps;
// main.cpp feeds raw readings in and pushes the resulting events into the emotion queue.

// The boards have no light sensor: the light level is only the simulated one (console `light <0..1>`), else bright.

struct StimulusOut {
    EmotionEvent events[2];
    int nEvents = 0;
    void add(uint8_t type, float k) {
      if (nEvents < 2) events[nEvents++] = {type, k};
    }
};

class TouchGestures {
  public:
    bool down = false, petting = false;
    float gazeX = 0, gazeY = 0;
    int startX() const { return _x0; }
    int startY() const { return _y0; }

    void update(bool touched, int x, int y, uint32_t now, StimulusOut& out) {
      if (touched) {
        _upCount = 0;
        if (!down) {
          down = true;
          _t0 = now;
          _x0 = _lx = x;
          _y0 = _ly = y;
          _path = _maxDisp = 0;
          _held = petting = false;
        }
        float step = hypotf(x - _lx, y - _ly);
        if (step > 2) {
          _path += step;
          _lx = x;
          _ly = y;
        }
        _maxDisp = fmaxf(_maxDisp, hypotf(x - _x0, y - _y0));
        gazeX = (x - 160) / 120.f;
        gazeY = (y - 120) / 100.f;
        uint32_t held = now - _t0;
        if (!petting && held > 400 && _path > 40) {
          petting = true;
          _nextPet = now;
        }
        if (petting && now >= _nextPet) {
          out.add(EV_PET, 0.5f);
          _nextPet = now + 800;
        }
        if (!petting && !_held && held > 1200 && _maxDisp < 15) {
          _held = true;
          out.add(EV_PET, 0.3f);
        }
        return;
      }
      if (!down || ++_upCount < 2) return;
      down = false;
      uint32_t dur = now - _t0 - 20;
      if (!petting && !_held) {
        if (dur < 350 && _maxDisp < 20) {
          out.add(EV_POKE, 0.6f);
          if (now - _tapStart > 3000) {
            _tapStart = now;
            _taps = 0;
          }
          if (++_taps >= 4) {
            out.add(EV_ANNOY, 0.6f);
            _taps = 0;
          }
        } else if (dur < 500 && _maxDisp >= 40) {
          out.add(EV_POKE, 0.9f);
        } else {
          out.add(EV_POKE, 0.4f);
        }
      }
      petting = false;
    }

  private:
    uint32_t _t0 = 0, _nextPet = 0, _tapStart = 0;
    int _x0 = 0, _y0 = 0, _lx = 0, _ly = 0, _taps = 0, _upCount = 0;
    float _path = 0, _maxDisp = 0;
    bool _held = false;
};

class LightSense {
  public:
    bool dark = false;

    bool update(float level, float dt, StimulusOut& out) {
      _lvl += (level - _lvl) * fminf(1.f, dt / 2.f);
      if (!dark) {
        _darkFor = _lvl < 0.15f ? _darkFor + dt : 0;
        if (_darkFor > 10) {
          dark = true;
          out.add(EV_TIRED, 0.4f);
        }
      } else if (_lvl > 0.3f) {
        dark = false;
        _darkFor = 0;
        out.add(EV_STARTLE, 0.3f);
        return true;
      }
      return false;
    }
    float level() const { return _lvl; }

  private:
    float _lvl = 1, _darkFor = 0;
};

// Time of day -> energy. No RTC on the CYD: time is unknown until set (Serial `time HH:MM`, NTP in stage 6).
class DayClock {
  public:
    void set(int h, int m, uint32_t nowMs) {
      _offsetMs = (uint32_t)((h * 60 + m) * 60000) - nowMs;
      _known = true;
    }
    bool known() const { return _known; }
    float hour(uint32_t nowMs) const { return ((nowMs + _offsetMs) % 86400000u) / 3600000.f; }
    float energy(uint32_t nowMs) const { return _known ? cosf((hour(nowMs) - 15.f) * 6.2832f / 24.f) : 0.f; }
    bool night(uint32_t nowMs) const { return energy(nowMs) < -0.6f; }

  private:
    uint32_t _offsetMs = 0;
    bool _known = false;
};

// Horizontal swipe: a fast, mostly horizontal drag of at least 70 px (switches apps). update() returns -1 for a swipe
// to the left, +1 to the right, 0 otherwise; call it every poll with the raw touch state.
class SwipeDetector {
  public:
    int update(bool touched, int x, int y, uint32_t now) {
      if (touched) {
        if (!_down) {
          _down = true;
          _x0 = x;
          _y0 = y;
          _t0 = now;
        }
        _x = x;
        _y = y;
        return 0;
      }
      if (!_down) return 0;
      _down = false;
      int dx = _x - _x0, dy = _y - _y0;
      int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
      if (now - _t0 <= 700 && adx >= 70 && adx > 2 * ady) return dx < 0 ? -1 : 1;
      return 0;
    }

  private:
    bool _down = false;
    int _x0 = 0, _y0 = 0, _x = 0, _y = 0;
    uint32_t _t0 = 0;
};

class HoldDetector {
  public:
    bool update(bool touched, int x, int y, uint32_t now, uint32_t ms) {
      if (!touched) {
        _holding = _fired = false;
        return false;
      }
      if (!_holding) {
        _holding = true;
        _x0 = x;
        _y0 = y;
        _t0 = now;
        return false;
      }
      int dx = x - _x0, dy = y - _y0;
      if (dx * dx + dy * dy > 30 * 30) {
        _x0 = x;
        _y0 = y;
        _t0 = now;
        return false;
      }
      if (!_fired && now - _t0 >= ms) {
        _fired = true;
        return true;
      }
      return false;
    }

  private:
    bool _holding = false, _fired = false;
    int _x0 = 0, _y0 = 0;
    uint32_t _t0 = 0;
};
