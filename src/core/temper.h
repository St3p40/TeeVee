#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "emotion.h"

// Temperament: a slow drift of the emotional baseline from how it is treated over days. Kind attention (petting, praise, love, greetings) lifts the baseline mood a
// little, scolding and annoying it lowers it; being played with a lot raises its energy, boredom and tiredness lower it. It fades back with a half-life of about
// five days and is capped, so it never becomes a different robot, just a slightly warmer or cooler one. Pure logic, no Arduino deps.
class Temper {
  public:
    float valence = 0, arousal = 0;
    struct Saved {
        uint32_t version;
        float valence, arousal;
    };
    static constexpr uint32_t VERSION = 1;

    void react(uint8_t type, float intensity, float scale) {
      float dv = 0, da = 0;
      switch (type) {
        case EV_PET: dv = 0.0015f; break;
        case EV_PRAISE:
        case EV_LOVE: dv = 0.003f; break;
        case EV_GREET: dv = 0.002f; break;
        case EV_SCOLD: dv = -0.004f; break;
        case EV_ANNOY: dv = -0.003f; break;
        case EV_POKE:
          dv = -0.0004f;
          da = 0.0008f;
          break;
        case EV_WIN:
          dv = 0.001f;
          da = 0.001f;
          break;
        case EV_STARTLE: da = 0.001f; break;
        case EV_TIRED:
        case EV_BORE: da = -0.002f; break;
        default: return;
      }
      valence += dv * intensity * scale;
      arousal += da * intensity * scale;
      clamp();
    }

    void update(float dt) {  // fades with a half-life of five days
      const float k = expf(-dt * 0.6931f / (5.f * 86400.f));
      valence *= k;
      arousal *= k;
    }

    Saved save() const { return {VERSION, valence, arousal}; }
    bool load(const void* blob, size_t len) {
      if (len != sizeof(Saved)) return false;
      Saved s;
      memcpy(&s, blob, sizeof s);
      if (s.version != VERSION) return false;
      valence = s.valence;
      arousal = s.arousal;
      clamp();
      return true;
    }

    void clamp() {
      if (valence != valence) valence = 0;  // NaN (a damaged save)
      if (arousal != arousal) arousal = 0;
      valence = valence < -0.2f ? -0.2f : valence > 0.2f ? 0.2f : valence;
      arousal = arousal < -0.12f ? -0.12f : arousal > 0.12f ? 0.12f : arousal;
    }
};
