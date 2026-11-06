#pragma once
#include <math.h>
#include <stdint.h>
#include "face_params.h"

// Idle life (core 0): blinks, saccades around a wandering gaze target, breathing, occasional
// micro-behaviours, and drowsy -> asleep after inactivity. Overlays the base expression to give
// the shared target FaceParams; the core-1 springs do all the smoothing. No Arduino deps.
// The face of a sleeping robot (what idle.apply makes when asleep, without the breathing): the pose to start from at power-on.
inline FaceParams sleepingPose(const FaceParams& base) {
  FaceParams p = base;
  p.eye_open_l = p.eye_open_r = 0.04f;
  p.brow_height -= 0.3f;
  p.mouth_curve *= 0.3f;
  p.mouth_open = 0.05f;
  return p;
}

class IdleGen {
  public:
    enum State : uint8_t { AWAKE, DROWSY, ASLEEP };
    enum Behaviour : uint8_t {
      B_NONE,
      B_LOOK_AROUND,
      B_SQUINT,
      B_SMILE,
      B_CURIOUS,
      B_YAWN,
      B_WINK,
      B_BOUNCE,
      B_EYEROLL,
      B_WHISTLE,
      B_COUNT
    };
    // Picks the next micro-behaviour (1..B_COUNT-1), e.g. random; must not return `last`. 0 = let idle pick.
    using Chooser = uint8_t (*)(void* ctx, uint8_t last, bool yawnOk);
    static constexpr uint32_t DROWSY_AFTER_MS = 4 * 60 * 1000;
    static constexpr uint32_t DROWSY_MS = 90 * 1000;

    void begin(uint32_t seed, uint32_t now) {
      _rng = seed ? seed : 0x9E3779B9u;
      _lastActivity = _lastMs = now;
      _drowsyMs = DROWSY_MS;
      _nextBlink = now + 1500;
      _nextSaccade = now + 600;
      _nextFix = now + 3000;
      _nextBehave = now + (uint32_t)frand(15000, 40000);
    }

    State state(uint32_t now) const {
      if (_forcedAsleep) return ASLEEP;
      uint32_t t = now - _lastActivity;
      return t < _drowsyAfter ? AWAKE : t < _drowsyAfter + _drowsyMs ? DROWSY : ASLEEP;
    }

    void poke(uint32_t now) {
      if (state(now) != AWAKE) {
        pulse(now, 0.35f);
        _nextBlink = now + 250;
        _doubleBlink = true;
        _behaveEnd = now;
      }
      _lastActivity = now;
      _forcedAsleep = false;
      _drowsyMs = DROWSY_MS;
      _nextStir = 0;
    }

    void fallAsleepNow(uint32_t now) {
      _forcedAsleep = true;  // asleep whatever the sleep_after setting says, until the first poke / wakeUp
      _behaveEnd = now;
    }
    void wakeUp(uint32_t now) {
      poke(now);
      startBehaviour(B_YAWN, now, 30000, 60000);
    }

    void sleepNow(uint32_t now) {
      _lastActivity = now - _drowsyAfter;  // unsigned wrap is fine: only differences are used
      _drowsyMs = 8000;
      _behaveEnd = now;
    }

    void setDrowsyAfter(uint32_t ms) { _drowsyAfter = ms; }

    void lookAt(float gx, float gy, uint32_t now, uint32_t ms) {
      _lookX = clampf(gx, -1, 1);
      _lookY = clampf(gy, -1, 1);
      _lookUntil = now + ms;
      _behaveEnd = now;
    }

    void flinch(int side, uint32_t now) {
      _flinchSide = side;
      _flinchUntil = now + 420;
      pulse(now, -0.25f);
      _doubleBlink = false;
    }

    void setPetting(bool on) { _petting = on; }

    // How often micro-behaviours happen: 1 = normal (every 20-70 s), 2 = twice as often, 0 = never.
    void setBehaviourRate(float r) { _behaveRate = r < 0 ? 0 : r; }

    void setChooser(Chooser fn, void* ctx) {
      _chooser = fn;
      _chooserCtx = ctx;
    }

    static constexpr int URGES = 19;
    void setUrges(const float* u, float amount) {
      for (int i = 0; i < URGES; i++) _u[i] = u[i];
      _mind = amount;
    }

    void setDay(float warmth, float lonely) {
      _dayWarm = clampf(warmth, -1, 1);
      _dayLonely = clampf(lonely, 0, 1);
    }

    void setDayGap(float seconds) { _dayGap = seconds < 5 ? 5 : seconds; }

    void setAmounts(float squash, float motion, float gaze) {
      _squashAmt = squash;
      _motionAmt = motion;
      _gazeAmt = gaze;
    }

    void setValence(float v) { _valence = clampf(v, -1, 1); }

    void setArousal(float a) { _arousal = clampf(a, -1, 1); }

    void exprChanged(const FaceParams& from, const FaceParams& to, uint32_t now) {
      float dir = (to.eye_open_l + to.eye_open_r - from.eye_open_l - from.eye_open_r) * 0.5f +
                  (to.mouth_open - from.mouth_open) + (to.brow_height - from.brow_height) * 0.5f;
      float mag = fabsf(to.eye_open_l - from.eye_open_l) + fabsf(to.mouth_open - from.mouth_open) +
                  0.5f * (fabsf(to.mouth_curve - from.mouth_curve) +
                          fabsf(to.brow_height - from.brow_height) + fabsf(to.brow_angle - from.brow_angle));
      pulse(now, (dir >= 0 ? 0.3f : -0.3f) * fminf(mag, 1.5f));
      if (frand(0, 1) < 0.4f) _nextBlink = now + 60;
      _behaveEnd = now;
    }

    FaceParams apply(const FaceParams& base, uint32_t now) {
      float dt = fminf((now - _lastMs) * 1e-3f, 0.1f);
      _lastMs = now;
      const State st = state(now);
      const uint32_t idleMs = now - _lastActivity;
      const float sleepy = st == AWAKE    ? 0.f
                           : st == DROWSY ? fminf(1.f, (idleMs - _drowsyAfter) / (float)_drowsyMs)
                                          : 1.f;
      FaceParams p = base;
      float eyeMul = 1.f, winkL = 1.f, winkR = 1.f;

      const float arEff = clampf(_arousal + 0.6f * _mind * _us[2], -1, 1);

      _breathPhase += dt * 6.2832f / ((4.f + 2.f * sleepy) * (1 - 0.3f * arEff) * _breathJitter);
      if (_breathPhase > 6.2832f) {
        _breathPhase -= 6.2832f;
        _breathJitter = frand(0.85f, 1.15f);
      }
      const float br = sinf(_breathPhase);
      p.squash += br * (0.05f + 0.04f * sleepy);

      if (st != ASLEEP && now >= _nextBlink) {
        uint32_t hold = st == DROWSY ? (uint32_t)frand(250, 250 + 700 * sleepy) : (uint32_t)frand(110, 170);
        _blinkUntil = now + hold;
        if (_doubleBlink) {
          _nextBlink = now + hold + 220;
          _doubleBlink = false;
        } else {
          float interval = st == DROWSY ? 2000 + expo(3000) : 1200 + fminf(expo(2800), 7800);
          interval *= 1 + 0.5f * fmaxf(0.f, -_valence);
          if (_mind > 0) interval *= expf(-0.8f * _mind * _us[8]);
          _nextBlink = now + hold + (uint32_t)interval;
          _doubleBlink = frand(0, 1) < 0.15f;
          if (_doubleBlink) _nextBlink = now + hold + 220;
        }
      }

      if (now >= _nextFix) {
        float ox = _fx, oy = _fy;
        if (frand(0, 1) < 0.55f) {
          _fx = frand(-0.3f, 0.3f);
          _fy = frand(-0.2f, 0.2f);
        } else {
          _fx = frand(-0.9f, 0.9f);
          _fy = frand(-0.7f, 0.6f);
        }
        _nextFix = now + (uint32_t)frand(3000, 12000);
        if (fabsf(_fx - ox) + fabsf(_fy - oy) > 0.6f && frand(0, 1) < 0.35f) _nextBlink = now + 40;
      }
      if (now >= _nextSaccade) {
        _sx = frand(-0.12f, 0.12f);
        _sy = frand(-0.08f, 0.08f);
        _nextSaccade = now + 300 + (uint32_t)(expo(1100) * (1 + 2 * sleepy) * (1 - 0.4f * arEff));
      }
      float gx = (_fx + _sx) * _gazeAmt, gy = (_fy + _sy) * _gazeAmt;
      if (now < _lookUntil) {
        gx = _lookX + _sx * 0.5f;
        gy = _lookY + _sy * 0.5f;
      }
      gy += 0.3f * fmaxf(0.f, -_valence);
      if (_mind > 0) {
        if (_us[5] < 0.05f && frand(0, 1) < 0.02f) _suspSide = frand(0, 1) < 0.5f ? -1.f : 1.f;
        gx += _mind * (0.7f * _us[0] + 0.3f * fmaxf(0.f, _us[5]) * _suspSide);
        gy += _mind * 0.5f * _us[1];
      }
      gx += noise(N_GAZE, now) * 0.05f;
      gx *= 1 - sleepy;
      gy += (0.4f - gy) * sleepy;

      const bool driven = _mind > 0;
      const float us = 1.f - expf(-dt / 0.5f);
      for (int i = 0; i < URGES; i++) _us[i] += ((driven ? _u[i] : 0.f) - _us[i]) * us;
      if (driven && st == AWAKE) {
        // restlessness / mouth play that builds up past a threshold breaks out as a hop / a whistle (never two in a row)
        if (now >= _nextBehave && now >= _behaveEnd) {
          uint8_t b = _us[11] > 0.5f && idleMs >= 60000 ? B_YAWN
                      : _us[7] > 0.55f                  ? B_BOUNCE
                      : _us[9] > 0.6f                   ? B_WHISTLE
                                                        : B_NONE;
          if (b != B_NONE) startBehaviour(b, now, b == B_YAWN ? 60000 : 6000, b == B_YAWN ? 150000 : 14000);
        }
      }

      if (driven && st == AWAKE && idleMs >= 20000 && now >= _behaveEnd && _behaveRate > 0) {
        const float pull = fmaxf(fabsf(_dayWarm), _dayLonely);
        if (!_nextDay) _nextDay = now + (uint32_t)frand(5000, 40000);
        if (pull >= 0.2f && now >= _nextDay) {
          uint8_t b = dayBehaviour();
          if (b == _behave) b = dayBehaviour();
          startBehaviour(b, now, 6000, 14000);
          _nextDay = _behaveEnd + (uint32_t)(frand(500, 1500) * _dayGap / ((0.5f + pull) * _behaveRate));
        }
      }

      // Micro-behaviours every 20-70 s while awake, never the same twice in a row.
      if (!driven && st == AWAKE && now >= _nextBehave && _behaveRate > 0) {
        bool yawnOk = idleMs >= 120000;
        uint8_t b = _chooser ? _chooser(_chooserCtx, _behave, yawnOk) : 0;
        if (b == 0 || b >= B_COUNT || b == _behave) {
          do b = 1 + rnd() % (B_COUNT - 1);
          while (b == _behave || (b == B_YAWN && !yawnOk));
        }
        startBehaviour(b, now, 20000, 70000);
      }
      if (now < _behaveEnd) {
        uint32_t t = now - _behaveStart;
        switch (_behave) {
          case B_LOOK_AROUND:
            gx = t < 1100 ? -0.85f * _behaveSide : t < 2200 ? 0.85f * _behaveSide : 0.f;
            gy = 0;
            break;
          case B_SQUINT:
            eyeMul *= 0.6f;
            p.brow_angle += 0.35f;
            gx = 0.6f * _behaveSide;
            break;
          case B_SMILE:
            p.mouth_curve += 0.4f;
            p.blush += 0.35f;
            eyeMul *= 0.85f;
            gx *= 0.3f;
            break;
          case B_CURIOUS:
            p.brow_height += 0.5f;
            p.squash += 0.12f;
            gx = 0.5f * _behaveSide;
            gy = -0.45f;
            break;
          case B_YAWN:
            if (t < 2000) {
              p.mouth_open = fmaxf(p.mouth_open, 0.85f);
              eyeMul *= 0.3f;
              p.squash += 0.25f;
              p.brow_height += 0.3f;
            }
            break;
          case B_WINK:
            if (t > 150 && t < 650) (_behaveSide < 0 ? winkL : winkR) = 0.05f;
            p.mouth_curve += 0.5f;
            p.mouth_skew += 0.4f * _behaveSide;
            p.lid_bottom += 0.3f;
            break;
          case B_BOUNCE:
            p.squash += 0.28f * sinf(t * 6.2832f / 500.f);
            p.mouth_curve += 0.4f;
            p.lid_bottom += 0.4f;
            break;
          case B_EYEROLL: {
            float ang = t * 6.2832f / 1300.f;
            if (t < 1300) {
              gx = 0.8f * sinf(ang) * _behaveSide;
              gy = -0.7f * cosf(ang);
            }
            p.brow_height += 0.3f;
            p.mouth_curve = -0.1f;
            p.mouth_skew += 0.3f * _behaveSide;
            break;
          }
          case B_WHISTLE:
            p.mouth_open = fmaxf(p.mouth_open, 0.35f);
            p.mouth_width = -0.85f;
            p.mouth_curve = 0;
            p.lid_bottom += 0.5f;
            p.squash += 0.05f * sinf(t * 6.2832f / 400.f);
            gx = 0.4f * _behaveSide;
            gy = -0.3f;
            break;
        }
      }

      if (_mind > 0 && st != ASLEEP) {
        const float cur = _us[3], mir = _us[4], sus = fmaxf(0.f, _us[5]), dro = fmaxf(0.f, _us[6]),
                    res = fmaxf(0.f, _us[7]);
        p.brow_height += _mind * 0.35f * cur;
        p.squash += _mind * 0.05f * fmaxf(0.f, cur);
        eyeMul *= clampf(1 + _mind * 0.15f * cur, 0.5f, 1.4f);
        p.mouth_curve += _mind * 0.35f * mir;
        p.blush += _mind * 0.25f * fmaxf(0.f, mir);
        eyeMul *= clampf(1 - _mind * 0.3f * sus, 0.4f, 1.f);
        p.brow_angle += _mind * 0.3f * _us[5];
        eyeMul *= clampf(1 - _mind * 0.3f * dro, 0.4f, 1.f);
        p.brow_height -= _mind * 0.2f * dro;
        _fidget += dt * (0.8f + 3.f * res);
        p.squash += _mind * 0.10f * res * sinf(_fidget * 3.1f);
        p.mouth_open = fmaxf(p.mouth_open, _mind * 0.12f * fmaxf(0.f, _us[9]));
        p.mouth_skew += _mind * 0.3f * _us[9];
      }

      // Micro-motion: slow random drift so the face is never perfectly still.
      if (st != ASLEEP) {
        const float m = _motionAmt;
        eyeMul *= 1 + 0.04f * m * noise(N_EYE, now);
        p.mouth_curve += 0.06f * m * noise(N_CURVE, now);
        p.brow_angle += 0.06f * m * noise(N_BROW, now);
        p.asym += 0.07f * m * noise(N_ASYM, now);
        p.mouth_skew += 0.1f * m * noise(N_SKEW, now);
      }

      if ((int32_t)(_flinchUntil - now) > 0) (_flinchSide < 0 ? winkL : winkR) = 0.04f;

      if (_petting) {
        eyeMul *= 0.55f;
        p.mouth_curve += 0.25f;
        p.blush += 0.2f;
      }

      if (st == ASLEEP) {
        if (!_nextStir) _nextStir = now + (uint32_t)frand(60000, 180000);
        if (now >= _nextStir) {
          _stirEnd = now + (uint32_t)frand(1200, 2000);
          _stirEye = rnd() % 3;
          _nextStir = _stirEnd + (uint32_t)frand(60000, 180000);
        }
        bool stir = now < _stirEnd;
        p.eye_open_l = stir && _stirEye != 1 ? 0.35f : 0.04f;
        p.eye_open_r = stir && _stirEye != 0 ? 0.35f : 0.04f;
        p.brow_height -= 0.3f;
        p.mouth_curve *= 0.3f;
        p.mouth_open = 0.05f + 0.05f * (br + 1);
        if (stir) p.squash += 0.1f;
      } else {
        eyeMul *= 1 - 0.55f * sleepy;
        p.brow_height -= 0.3f * sleepy;
        p.mouth_curve *= 1 - 0.5f * sleepy;
        bool blinking = now < _blinkUntil;
        p.eye_open_l = blinking ? 0 : p.eye_open_l * eyeMul * winkL;
        p.eye_open_r = blinking ? 0 : p.eye_open_r * eyeMul * winkR;
      }

      p.pupil_x = clampf(p.pupil_x + gx, -1, 1);
      p.pupil_y = clampf(p.pupil_y + gy, -1, 1);
      if (now < _pulseUntil) p.squash += _pulse;
      p.squash = clampf(p.squash * _squashAmt, -1, 1);
      p.blush = clampf(p.blush, 0, 1);
      p.lid_bottom = clampf(p.lid_bottom, 0, 1);
      p.asym = clampf(p.asym, -1, 1);
      p.mouth_skew = clampf(p.mouth_skew, -1, 1);
      return p;
    }

  private:
    enum : uint8_t { N_EYE, N_CURVE, N_BROW, N_ASYM, N_SKEW, N_GAZE, N_COUNT };

    float noise(int ch, uint32_t now) {
      Noise& n = _noise[ch];
      if (now - n.t0 >= n.period) {
        n.a = n.b;
        n.b = frand(-1, 1);
        n.t0 = now;
        n.period = (uint32_t)frand(2000, 7000);
      }
      float x = (now - n.t0) / (float)n.period;
      x = x * x * (3 - 2 * x);
      return n.a + (n.b - n.a) * x;
    }

    static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
    void startBehaviour(uint8_t b, uint32_t now, float gapMin, float gapMax) {
      _behave = b;
      _behaveStart = now;
      _behaveSide = frand(0, 1) < 0.5f ? -1.f : 1.f;
      static const uint16_t DUR[B_COUNT] = {0, 3300, 2200, 3200, 2200, 2600, 900, 1500, 1600, 2600};
      _behaveEnd = now + DUR[b] + (uint32_t)frand(0, 800);
      _nextBehave = _behaveEnd + (uint32_t)(frand(gapMin, gapMax) / (_mind > 0 ? 1.f : _behaveRate));
      if (b == B_YAWN) _nextBlink = _behaveEnd + 100;
      if (b == B_LOOK_AROUND) _nextBlink = now + 1100;
    }

    uint8_t dayBehaviour() {
      if (_dayLonely > fabsf(_dayWarm)) return frand(0, 1) < 0.8f ? B_LOOK_AROUND : B_CURIOUS;
      const float r = frand(0, 1);
      if (_dayWarm > 0) return r < 0.55f ? B_SMILE : r < 0.85f ? B_WHISTLE : B_WINK;
      return r < 0.6f ? B_SQUINT : B_EYEROLL;
    }

    uint32_t rnd() {
      _rng ^= _rng << 13;
      _rng ^= _rng >> 17;
      _rng ^= _rng << 5;
      return _rng;
    }
    float frand(float a, float b) { return a + (b - a) * (rnd() >> 8) * (1.f / 16777216.f); }
    float expo(float mean) { return -mean * logf(1.f - frand(0, 0.999f)); }
    void pulse(uint32_t now, float amount) {
      _pulse = amount;
      _pulseUntil = now + 90;
    }

    uint32_t _rng = 1, _lastMs = 0, _lastActivity = 0, _drowsyMs = DROWSY_MS;
    uint32_t _nextBlink = 0, _blinkUntil = 0, _nextSaccade = 0, _nextFix = 0;
    uint32_t _nextBehave = 0, _behaveStart = 0, _behaveEnd = 0, _nextStir = 0, _stirEnd = 0, _pulseUntil = 0;
    bool _doubleBlink = false;
    uint8_t _behave = B_NONE, _stirEye = 0;
    float _behaveSide = 1, _pulse = 0;
    float _fx = 0, _fy = 0, _sx = 0, _sy = 0;
    float _breathPhase = 0, _breathJitter = 1, _arousal = 0;
    uint32_t _drowsyAfter = DROWSY_AFTER_MS, _lookUntil = 0;
    float _lookX = 0, _lookY = 0;
    bool _petting = false;
    float _valence = 0, _behaveRate = 1, _squashAmt = 1, _motionAmt = 1, _gazeAmt = 1;
    bool _forcedAsleep = false;
    int _flinchSide = 0;
    uint32_t _flinchUntil = 0;
    float _u[URGES] = {}, _us[URGES] = {}, _mind = 0, _suspSide = 1, _fidget = 0;
    float _dayWarm = 0, _dayLonely = 0, _dayGap = 60;
    uint32_t _nextDay = 0;
    Chooser _chooser = nullptr;
    void* _chooserCtx = nullptr;
    struct Noise {
        float a = 0, b = 0;
        uint32_t t0 = 0, period = 1;
    } _noise[N_COUNT];

  public:
    float sleepiness(uint32_t now) const {
      const State st = state(now);
      return st == AWAKE    ? 0.f
             : st == DROWSY ? fminf(1.f, (now - _lastActivity - _drowsyAfter) / (float)_drowsyMs)
                            : 1.f;
    }
    float idleSeconds(uint32_t now) const { return (now - _lastActivity) * 1e-3f; }

    uint8_t behaviour(uint32_t now) const { return now < _behaveEnd ? _behave : 0; }
    bool blinking(uint32_t now) const { return now < _blinkUntil; }
};
