#pragma once
#include <math.h>
#include <stdint.h>

// Hearing: turns a stream of sound levels (the RMS of one block of 16-bit samples, every ~20 ms) into two things the robot
// can react to: a sudden loud sound (a clap, a bang, something dropped) and voice-like sound that keeps going (someone
// talking nearby). Only levels: nothing is recorded or kept. Pure logic, no Arduino deps.
//
// Everything is relative to the room's own noise floor, followed slowly upwards and quickly downwards, so a fan or a TV
// left on becomes the new quiet instead of a constant alarm.

struct HearingOut {
    bool loud = false;    // a sudden loud sound this block
    float loudK = 0;      // its strength, 0.3..1
    bool voiceStart = false;  // talking started after a long quiet
};

class Hearing {
  public:
    static constexpr float LOUD_DB = 24;         // above the floor for a loud sound
    static constexpr float RISE_DB = 14;         // and that much louder than 0.1 s before: sudden, not a slow swell
    static constexpr float VOICE_DB = 9;         // above the floor for "someone is making sound"
    static constexpr float TALK_SEC = 0.6f;      // that much of it within a short while = talking
    static constexpr float QUIET_FOR_GREET = 90; // seconds of quiet before talking counts as "someone came"
    static constexpr float WARMUP_SEC = 2;       // at start the codec is still settling: learn the room, report nothing

    void setSensitivity(float s) { _sens = s < 0.3f ? 0.3f : s > 3 ? 3 : s; }
    // Not listening for a while (its own voice is playing): the next block starts afresh, so the gap is not a sudden sound.
    void pause() { _resume = true; }

    HearingOut update(float rms, float dt) {
      HearingOut out;
      const float db = 20 * log10f(rms + 1);
      if (_floor < 0) _floor = _slow = db;
      // The floor: down within half a second, up over about twenty (both quickly while warming up).
      _age += dt;
      const bool warm = _age > WARMUP_SEC;
      const float kFloor = db < _floor ? fminf(1.f, dt / 0.5f) : warm ? dt / 20.f : fminf(1.f, dt / 0.3f);
      _floor += (db - _floor) * kFloor;
      const float over = db - _floor;
      _level = fminf(1.f, fmaxf(0.f, over / 30.f));
      // The level 0.1 s ago (a short smoothing), to tell a sudden sound from a swell.
      const float before = _slow;
      _slow += (db - _slow) * fminf(1.f, dt / 0.1f);
      _sinceLoud += dt;
      if (!warm || _resume) {
        _slow = db;
        _resume = false;
        return out;
      }
      if (over > LOUD_DB / _sens && db - before > RISE_DB / _sens && _sinceLoud > 1.5f) {
        out.loud = true;
        out.loudK = fminf(1.f, fmaxf(0.3f, (over - LOUD_DB / _sens) / 20.f + 0.4f));
        _sinceLoud = 0;
      }
      // Talking: a leaky count of the time spent above the voice level.
      if (over > VOICE_DB / _sens)
        _active = fminf(2.f, _active + dt);
      else
        _active = fmaxf(0.f, _active - dt * 0.5f);
      const bool wasTalking = _talking;
      _talking = _active > TALK_SEC;
      if (_talking && !wasTalking && _quiet > QUIET_FOR_GREET) out.voiceStart = true;
      _quiet = _talking ? 0 : _quiet + dt;
      return out;
    }

    bool talking() const { return _talking; }
    float level() const { return _level; }  // 0..1, how far above the room's floor
    float floorDb() const { return _floor; }

  private:
    float _sens = 1;
    float _floor = -1, _slow = 0, _level = 0;
    float _sinceLoud = 10, _active = 0, _quiet = 1e9f, _age = 0;
    bool _talking = false, _resume = false;
};
