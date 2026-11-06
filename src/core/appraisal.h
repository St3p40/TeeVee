#pragma once
#include <math.h>
#include <stdint.h>
#include "emotes.h"
#include "emotion.h"

// Between what happens and how the robot reacts: so that a reaction is not a lookup in a table. Pure logic, no Arduino deps; the board, the
// desktop and the web page share it. Host test: test/appraisal_test.cpp.
//   (Getting used to a repeated event is the emotion model's own habit, per personality: emotion.h. The habit count here is for what it does not do:)
//   Escalation   a poke or two is play; keep going and it turns into real annoyance. A scolding stings more, then it shuts down.
//   Trust        a slow memory of how it has been treated: after a scolding a pet lands weakly until it thaws; a scolding hurts more if it trusted you.
//   Noticing     after a long quiet anything counts for a bit more.
//   Timing       it reacts a moment later, not in the same frame: reflexes fast, the rest slower, slowest when sleepy, quicker when wound up.
//   Emotes       picked by chance among what fits the event and its state, not the one it just showed, often none.
//   Glance back  a few seconds after a touch it often looks at that spot again, and may keep doing so for a while, less and less; longer when wary or wound up.
//   Overload     a storm of events builds up a load and the picture starts to glitch; alone it is nearly clean, asleep cleaner still (glitchFor).
class Appraisal {
  public:
    void begin(uint32_t seed) {
      _rng = seed ? seed : 0x51ED270Bu;
      for (float& h : _habit) h = 0;
      _trust = 0;
      _load = 0;
      _n = 0;
      _lastAt = 0;
      _glanceAt = 0;
      _attention = 0;
      _lastEmote = EM_NONE;
    }

    void hear(const EmotionEvent& ev, const PAD& feel, float sleepy, uint32_t now) {
      if (ev.type >= EV_COUNT) return;
      const bool gentle =
          ev.type == EV_PET || ev.type == EV_LOVE || ev.type == EV_PRAISE || ev.type == EV_GREET;
      _load += (gentle ? 0.1f : 0.25f) * (ev.intensity < 0.3f ? 0.3f : ev.intensity > 1 ? 1 : ev.intensity);
      if (_load > 1.5f) _load = 1.5f;
      EmotionEvent out = meaning(ev, feel, now);
      const uint32_t due = now + delayFor(out.type, feel, sleepy);
      if (_n >= QUEUE) {
        _ready = _q[0].ev;
        _hasReady = true;
        for (int i = 1; i < _n; i++) _q[i - 1] = _q[i];
        _n--;
      }
      int at = _n;  // in order of when they are due: a reflex can overtake a slower reaction
      while (at > 0 && (int32_t)(_q[at - 1].due - due) > 0) {
        _q[at] = _q[at - 1];
        at--;
      }
      _q[at] = {due, out};
      _n++;
    }

    // The next reaction whose moment has come. Call until it returns false.
    bool pop(uint32_t now, EmotionEvent& ev) {
      if (_hasReady) {
        _hasReady = false;
        ev = _ready;
        return true;
      }
      if (!_n || (int32_t)(now - _q[0].due) < 0) return false;
      ev = _q[0].ev;
      for (int i = 1; i < _n; i++) _q[i - 1] = _q[i];
      _n--;
      return true;
    }

    void setRules(const EmoteRule* rules) { _rules = rules; }
    void setLiked(const float* scores) { _liked = scores; }

    uint8_t emoteFor(uint8_t type, const PAD& feel) {
      const float r = type < EV_COUNT ? _habit[type] : 0.f;
      Cand c[6];
      int n = 0;
      auto add = [&](uint8_t e, float w) {
        if (w > 0 && n < 6) {
          if (_liked && e != EM_NONE) w *= expf(1.1f * _liked[e]);
          c[n++] = {e, e == _lastEmote && e != EM_NONE ? w * 0.15f : w};
        }
      };
      const bool ruled = _rules && type < EV_COUNT && _rules[type].n;
      if (ruled)
        for (int i = 0; i < _rules[type].n; i++) add(_rules[type].emote[i], _rules[type].weight[i]);
      else
        switch (type) {
          case EV_PET:
            if (feel.v > 0.8f)
              add(EM_LOVE, 3), add(EM_HEART, 3), add(EM_MUSIC, 1), add(EM_NONE, 2);
            else if (feel.v > 0.5f)
              add(EM_HEART, 3), add(EM_MUSIC, 1), add(EM_NONE, 3);
            else if (_trust < -0.25f)
              add(EM_QUESTION, 2), add(EM_SWEAT, 1), add(EM_NONE, 4);
            else
              add(EM_NONE, 1);
            if (_trust < 0.3f) add(EM_SHY, 1);
            break;
          case EV_LOVE: add(EM_LOVE, 4), add(EM_HEART, 3), add(EM_SHY, 2), add(EM_SPARKLE, 1); break;
          case EV_PRAISE:
            add(EM_SPARKLE, 3), add(EM_STAR, 2), add(EM_SHY, 2), add(EM_MUSIC, 1), add(EM_NONE, r * 3);
            break;
          case EV_WIN:
            add(EM_STAR, 3), add(EM_LAUGH, 3), add(EM_SPARKLE, 1), add(EM_MUSIC, 1), add(EM_MONEY, 0.4f);
            break;
          case EV_STARTLE:
            add(EM_EXCLAIM, 4), add(EM_QUESTION, 1), add(EM_SWEAT, 1), add(EM_NONE, r * 2);
            break;
          case EV_CONFUSE: add(EM_THINK, 3), add(EM_QUESTION, 3), add(EM_SWEAT, 0.5f); break;
          case EV_ANNOY: add(r > 5 ? EM_RAGE : EM_ANGRY, 4), add(EM_SWEAT, 1), add(EM_GLITCH, 0.5f); break;
          case EV_SCOLD:
            add(EM_ANGRY, feel.d > 0 ? 3.f : 1.f), add(EM_CRY, feel.d < 0 ? 3.f : 1.f), add(EM_SWEAT, 1),
                add(EM_SHY, 1);
            break;
          case EV_FAIL: add(EM_SWEAT, 3), add(EM_CRY, 2), add(EM_DIZZY, 1); break;
          case EV_POKE:
            add(EM_NONE, r < 2 ? 5.f : 1.f), add(EM_DIZZY, r > 2 ? 3.f : 0.f), add(EM_QUESTION, 1);
            if (_trust > 0.2f) add(EM_LAUGH, 1.5f);
            break;
          case EV_TIRED: add(EM_SWEAT, 1), add(EM_NONE, 3); break;
          case EV_GREET:
            add(EM_MUSIC, 2), add(EM_SPARKLE, 1), add(EM_LAUGH, 1), add(EM_HEART, _trust > 0.3f ? 2.f : 0.f),
                add(EM_NONE, 2);
            break;
          default: return EM_NONE;
        }
      float sum = 0;
      for (int i = 0; i < n; i++) sum += c[i].w;
      float x = frand() * sum;
      uint8_t pick = EM_NONE;
      for (int i = 0; i < n; i++) {
        pick = c[i].e;
        if ((x -= c[i].w) <= 0) break;
      }
      if (pick != EM_NONE) _lastEmote = pick;
      return pick;
    }

    void update(float dt) {
      const float h = expf(-dt / HABIT_TAU), t = expf(-dt / TRUST_TAU);
      _load *= expf(-dt / LOAD_TAU);
      for (float& v : _habit) v *= h;
      _trust *= t;
    }

    struct GlitchMix {
        float scale, drive;
    };
    GlitchMix glitchFor(float net, bool asleep, float arousal, float extra) const {
      if (asleep) return {0.1f, 0.f};
      float a = _load + 0.25f * (arousal > 0.3f ? (arousal - 0.3f) / 0.7f : 0.f) + extra;
      a = a > 1 ? 1 : a;
      if (a < 0.15f) return {0.25f, 0.f};
      const float over = a > 0.5f ? (a - 0.5f) / 0.5f : 0.f, gated = net * (a * 2.5f > 1 ? 1 : a * 2.5f);
      return {0.25f + 1.2f * a, (0.9f * over > gated ? 0.9f * over : gated)};
    }
    float load() const { return _load; }

    void touched(float gx, float gy, uint32_t now) {
      _attention = 0.5f + 0.2f * (_load > 1 ? 1 : _load) + (_trust < 0 ? -0.4f * _trust : 0.f);
      _glanceAt = 0;
      _gx = gx, _gy = gy;
      if (frand() < _attention) nextGlance(now, 2000, 5000);
    }
    bool glance(uint32_t now, float& gx, float& gy) {
      if (!_glanceAt || (int32_t)(now - _glanceAt) < 0) return false;
      _glanceAt = 0;
      gx = _gx, gy = _gy;
      _attention *= 0.6f;
      if (frand() < _attention) nextGlance(now, 4000, 10000);
      return true;
    }

    float trust() const { return _trust; }
    void setTrust(float t) { _trust = t == t ? clamp1(t) : 0; }  // from a save (NaN: a damaged one)
    float habit(uint8_t type) const { return type < EV_COUNT ? _habit[type] : 0.f; }

    static constexpr float LOAD_TAU = 10.f;
    static constexpr float HABIT_TAU = 45.f;
    static constexpr float TRUST_TAU = 21600.f;

  private:
    static constexpr int QUEUE = 8;
    struct Item {
        uint32_t due;
        EmotionEvent ev;
    };
    struct Cand {
        uint8_t e;
        float w;
    };

    EmotionEvent meaning(const EmotionEvent& ev, const PAD& feel, uint32_t now) {
      const float r = _habit[ev.type];
      uint8_t type = ev.type;
      float gain = 1;
      switch (ev.type) {
        case EV_POKE:
          if (r >= 3) {
            type = EV_ANNOY;
            gain = fminf(1.f, 0.45f + 0.12f * (r - 3));
          } else if (r < 1.5f && (_trust > 0.2f || feel.v > 0.3f)) {
            gain = 0.6f;
          }
          break;
        case EV_SCOLD: gain = r < 2 ? 1 + 0.2f * r : 0.7f; break;
        default: break;
      }
      const bool warm =
          ev.type == EV_PET || ev.type == EV_PRAISE || ev.type == EV_LOVE || ev.type == EV_GREET;
      if (warm && _trust < 0) gain *= 1 + 0.6f * _trust;  // wary: kindness lands weakly until it thaws
      if (ev.type == EV_SCOLD && _trust > 0) gain *= 1 + 0.3f * _trust;
      if (_lastAt && now - _lastAt > 180000u) gain *= 1.25f;
      gain *= 0.85f + 0.3f * frand();  // never quite the same twice
      _lastAt = now;
      _habit[ev.type] += 1;
      _trust = clamp1(_trust + trustStep(ev.type) * ev.intensity);
      float k = ev.intensity * gain;
      return {type, k > 1 ? 1.f : k};
    }

    uint32_t delayFor(uint8_t type, const PAD& feel, float sleepy) {
      float lo = 150, hi = 450;
      if (type == EV_STARTLE)
        lo = 30, hi = 90;
      else if (type == EV_POKE || type == EV_ANNOY)
        lo = 70, hi = 180;
      float ms = lo + (hi - lo) * frand();
      ms *= 1 + 1.5f * (sleepy < 0 ? 0 : sleepy > 1 ? 1 : sleepy);
      ms *= 1 - 0.3f * (feel.a > 0 ? (feel.a > 1 ? 1 : feel.a) : 0);
      return (uint32_t)(ms > 900 ? 900 : ms);
    }

    static float trustStep(uint8_t type) {
      switch (type) {
        case EV_PET: return 0.05f;
        case EV_PRAISE: return 0.08f;
        case EV_LOVE: return 0.1f;
        case EV_GREET: return 0.03f;
        case EV_POKE: return -0.03f;
        case EV_ANNOY: return -0.1f;
        case EV_SCOLD: return -0.25f;
        default: return 0;
      }
    }
    static float clamp1(float v) { return v < -1 ? -1 : v > 1 ? 1 : v; }
    void nextGlance(uint32_t now, float least, float spread) {
      _glanceAt = now + (uint32_t)(least + frand() * spread);
      if (!_glanceAt) _glanceAt = 1;
    }
    float frand() {
      _rng ^= _rng << 13, _rng ^= _rng >> 17, _rng ^= _rng << 5;
      return (_rng >> 8) / 16777216.f;
    }

    uint32_t _rng = 0x51ED270Bu, _lastAt = 0, _glanceAt = 0;
    float _habit[EV_COUNT] = {}, _trust = 0, _gx = 0, _gy = 0, _load = 0, _attention = 0;
    Item _q[QUEUE] = {};
    int _n = 0;
    EmotionEvent _ready = {};
    bool _hasReady = false;
    uint8_t _lastEmote = EM_NONE;
    const EmoteRule* _rules = nullptr;
    const float* _liked = nullptr;
};
