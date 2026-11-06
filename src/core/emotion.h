#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>

// Emotion engine (core 0). Two layers in PAD space (valence, arousal, dominance, each -1..1):
//  - emotion: fast, kicked by events, decays toward the mood;
//  - mood: slow, drifts toward the emotion and decays toward the personality baseline.
// The displayed state is the emotion. No Arduino deps.

struct PAD {
    float v, a, d;
};

struct Personality {
    PAD baseline;
    float sensitivity;
    float emotionHalfLife;
    float moodHalfLife;
    float moodFollow;
    float moodKick;
    float habituation;
    float habitHalfLife;
};

inline const Personality PERSONALITIES[] = {
 {{ 0.15f,  0.00f,  0.05f}, 1.0f, 6.f,  180.f, 0.02f, 0.1f, 0.5f, 20.f},
 {{ 0.10f, -0.30f,  0.10f}, 0.6f, 4.f,  300.f, 0.01f, 0.05f, 0.7f, 30.f},
 {{-0.25f,  0.10f,  0.35f}, 1.0f, 8.f,  240.f, 0.02f, 0.1f, 0.4f, 20.f},
 {{ 0.05f, -0.10f, -0.40f}, 1.3f, 10.f, 200.f, 0.02f, 0.1f, 0.6f, 25.f},
 {{ 0.30f,  0.50f,  0.20f}, 1.5f, 3.f,  120.f, 0.03f, 0.15f, 0.3f, 10.f},
};
inline const Personality& PERSONALITY = PERSONALITIES[0];

enum EventType : uint8_t {
  EV_PET,
  EV_POKE,
  EV_PRAISE,
  EV_SCOLD,
  EV_STARTLE,
  EV_WIN,
  EV_FAIL,
  EV_GREET,
  EV_TIRED,
  EV_ANNOY,
  EV_LOVE,
  EV_CONFUSE,
  EV_BORE,
  EV_COUNT
};

struct EventDef {
    const char* name;
    PAD delta;
};

inline const EventDef EVENTS[EV_COUNT] = {
  {"pet",     { 0.60f, -0.10f,  0.10f}},
  {"poke",    {-0.25f,  0.50f, -0.10f}},
  {"praise",  { 0.70f,  0.30f,  0.30f}},
  {"scold",   {-0.60f,  0.20f, -0.50f}},
  {"startle", { 0.00f,  0.90f, -0.40f}},
  {"win",     { 0.80f,  0.60f,  0.50f}},
  {"fail",    {-0.60f,  0.30f, -0.30f}},
  {"greet",   { 0.40f,  0.40f,  0.10f}},
  {"tired",   {-0.10f, -0.60f, -0.10f}},
  {"annoy",   {-0.50f,  0.40f,  0.50f}},
  {"love",    { 0.70f, -0.30f,  0.10f}},
  {"confuse", {-0.20f,  0.40f, -0.40f}},
  {"bore",    {-0.30f, -0.50f,  0.05f}},
};

struct EmotionEvent {
    uint8_t type;
    float intensity;
};

static inline int eventIndex(const char* name) {
  for (int i = 0; i < EV_COUNT; i++)
    if (!strcmp(name, EVENTS[i].name)) return i;
  return -1;
}

class EmotionEngine {
  public:
    void begin(const Personality& p) {
      _p = p;
      _mood = _emo = p.baseline;
      for (float& h : _habit) h = 0;
    }

    void setPersonality(const Personality& p) { _p = p; }

    void apply(const EmotionEvent& e) {
      if (e.type >= EV_COUNT) return;
      float gain = _p.sensitivity * clamp(e.intensity, 0, 1) / (1 + _p.habituation * _habit[e.type]);
      _habit[e.type] += 1;
      const PAD& d = EVENTS[e.type].delta;
      push(_emo, d, gain);
      push(_mood, d, gain * _p.moodKick);
    }

    void setBaselineShift(const PAD& s) { _shift = s; }

    void set(const PAD& p) { _emo = {clamp(p.v, -1, 1), clamp(p.a, -1, 1), clamp(p.d, -1, 1)}; }

    void update(float dt) {
      float ke = 1 - expf(-dt * 0.6931f / _p.emotionHalfLife);
      float km = 1 - expf(-dt * 0.6931f / _p.moodHalfLife);
      float kf = fminf(1.f, _p.moodFollow * dt);
      float kh = expf(-dt * 0.6931f / _p.habitHalfLife);
      lerp(_mood, _emo, kf);
      PAD base = {_p.baseline.v + _shift.v, _p.baseline.a + _shift.a, _p.baseline.d + _shift.d};
      lerp(_mood, base, km);
      lerp(_emo, _mood, ke);
      for (float& h : _habit) h *= kh;
    }

    const PAD& emotion() const { return _emo; }
    const PAD& mood() const { return _mood; }

  private:
    static float clamp(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
    static void push(PAD& s, const PAD& d, float g) {
      auto one = [](float x, float dx) { return x + dx * (dx > 0 ? 1 - x : 1 + x); };
      s.v = one(s.v, clamp(d.v * g, -1, 1));
      s.a = one(s.a, clamp(d.a * g, -1, 1));
      s.d = one(s.d, clamp(d.d * g, -1, 1));
    }
    static void lerp(PAD& s, const PAD& t, float k) {
      s.v += (t.v - s.v) * k;
      s.a += (t.a - s.a) * k;
      s.d += (t.d - s.d) * k;
    }

    Personality _p{};
    PAD _emo{}, _mood{}, _shift{};
    float _habit[EV_COUNT]{};
};
