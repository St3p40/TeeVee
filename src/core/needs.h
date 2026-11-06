#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>

class Needs {
  public:
    float attention = 0.2f, rest = 0.1f, play = 0.2f;
    float attentionSec = 5400, restSec = 21600, playSec = 10800, restoreSec = 1800;
    struct Saved {
        uint32_t version;
        float attention, rest, play;
    };
    static constexpr uint32_t VERSION = 1;

    void update(float dt, bool awake, bool asleep, bool touching, bool appActive) {
      attention += dt / attentionSec;
      if (touching) attention -= dt * 0.05f;
      if (awake) rest += dt / restSec;
      if (asleep) rest -= dt / restoreSec;
      play += dt / playSec;
      if (appActive) play -= dt / 120.f;
      clamp();
    }

    void fed(float a) {
      attention -= a;
      clamp();
    }
    void played(float p) {
      play -= p;
      clamp();
    }

    float valenceShift() const { return -0.25f * attention * attention - 0.1f * play * play; }
    float arousalShift() const { return -0.25f * rest; }

    Saved save() const { return {VERSION, attention, rest, play}; }
    bool load(const void* blob, size_t len) {
      if (len != sizeof(Saved)) return false;
      Saved s;
      memcpy(&s, blob, sizeof s);
      if (s.version != VERSION) return false;
      attention = s.attention;
      rest = s.rest;
      play = s.play;
      clamp();
      return true;
    }

    void clamp() {  // 0..1; NaN (a damaged save) -> 0
      attention = attention >= 0 && attention <= 1 ? attention : attention > 1 ? 1 : 0;
      rest = rest >= 0 && rest <= 1 ? rest : rest > 1 ? 1 : 0;
      play = play >= 0 && play <= 1 ? play : play > 1 ? 1 : 0;
    }
};
