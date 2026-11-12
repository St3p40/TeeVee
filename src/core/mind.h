#pragma once
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "fixed.h"

// The mind net: a small recurrent neural network that keeps Teevee's face alive between events.
//
//   34 senses  ->  32-unit recurrent "inner state"  ->  19 urges
//
// Every 100 ms it reads what it senses (feelings, time of day, how recently and how it was touched, motion, silence...)
// and produces urges: continuous wishes such as "look left", "smile", "be restless", "blink". The idle layer turns
// urges into movement, so nothing is a fixed script: the inner state has memory and a little noise, so it wanders.
//
// The inner state is a fixed random recurrent network (an echo state network, built from a seed, never stored); only the
// readout is learned. It starts from a hand-written prior (happy -> smiles, sleepy -> droops...) and learns from your
// reactions: it keeps trying slightly different urges (smoothly drifting noise), remembers what it did in the last few
// seconds (an eligibility trace), and when you pet or praise it the weights that produced those urges are strengthened,
// when you poke or scold it they are weakened (reward-modulated node perturbation). explain() / learned() say why.
// The net itself works in 16-bit whole numbers (fixed point: see MindNet), not floats; the senses come in and the urges go out as
// floats. No Arduino deps; weights ~2.6 KB at 32 inner units.

// Where each input comes from, per build (the net itself is the same everywhere; a sense a build cannot read stays 0):
//   every build:   valence, arousal, dominance, mood, hour (sin, cos), hour_liked, pet_rate, poke_rate, idle_time, sleepiness,
//                  eyes_open (openness), emote, need_attention, need_rest, need_play, trust, streak, bias
//   touch screen:  touching, touch_x, touch_y (the CYD; the desktop's mouse; the Lilka has no touch, so 0)
//   IMU:           motion, tilt_x, tilt_y, carried (the CYD, when the IMU is live; the desktop has its own motion model)
//   light:         light (the desktop's screen brightness; the boards have no light sensor, so only a simulated level)
//   desktop only:  presence, battery_low, watched, screen_activity (the boards never set them: always 0 there)
struct MindInputs {
    float v = 0, a = 0, d = 0, mood = 0;
    float hour = 12;
    float petRate = 0, pokeRate = 0;
    float light = 0;
    float sinceTouch = 1e9f, sinceEvent = 1e9f;
    float idleSec = 0, sleepy = 0;
    bool touching = false;
    float tx = 0, ty = 0;
    float motion = 0, tiltX = 0, tiltY = 0;
    bool carried = false;
    float openness = 0.5f;
    bool emote = false;
    float needAttention = 0, needRest = 0, needPlay = 0;
    float presence = 0;
    float batteryLow = 0;
    float watched = 0;
    float screen = 0;
    float trust = 0;
    float streak = 0;
    bool weekKnown = false;
    float weekLike = 0;
};

namespace mind {
enum Input : uint8_t {
  I_VALENCE,
  I_AROUSAL,
  I_DOMINANCE,
  I_MOOD,
  I_HOUR_SIN,
  I_HOUR_COS,
  I_PET,
  I_POKE,
  I_LIGHT,
  I_SINCE_TOUCH,
  I_SINCE_EVENT,
  I_IDLE,
  I_SLEEPY,
  I_TOUCHING,
  I_TOUCH_X,
  I_TOUCH_Y,
  I_MOTION,
  I_TILT_X,
  I_TILT_Y,
  I_CARRIED,
  I_EYES,
  I_EMOTE,
  I_NOVELTY,
  I_LONELY,
  I_NEED_ATTN,
  I_NEED_REST,
  I_NEED_PLAY,
  I_HOUR_LIKE,
  I_PRESENCE,
  I_BATTERY,
  I_WATCHED,
  I_SCREEN,
  I_TRUST,
  I_STREAK,
  I_BIAS,
  IN  // the bias stays last (older saves are carried over around it: MindNet::load)
};
enum Urge : uint8_t {
  U_GAZE_X,
  U_GAZE_Y,
  U_ENERGY,
  U_CURIOUS,
  U_MIRTH,
  U_SUSPICIOUS,
  U_DROWSY,
  U_RESTLESS,
  U_BLINK,
  U_MOUTH,
  U_GLITCH,
  U_YAWN,
  U_EMOTE,
  U_PUPIL,
  U_SLEEP,
  U_WARDROBE,
  U_LENS,
  U_HEAD_X,
  U_HEAD_Y,
  OUT
};
// The layouts of older saves, which still load (MindNet::load): version, urges, senses (the last sense is always the bias)
struct SavedLayout {
    uint32_t version;
    int out, in;
};
// (all of these are in floats; 8 was the last of them, and the net has held whole numbers since 9)
inline const SavedLayout OLD_LAYOUTS[] = {{2, 17, 29}, {3, 19, 29}, {4, 19, 31}, {5, 19, 32},
                                          {6, 19, 33}, {7, 19, 34}, {8, 19, 35}};
struct Instinct {
    uint8_t urge, sense;
    float w;
};
constexpr int MAX_INSTINCTS = 24;
inline const Instinct* INSTINCTS = nullptr;
inline int INSTINCT_COUNT = 0;
inline const char* const INPUT_NAMES[IN] = {
    "valence",        "arousal",         "dominance", "mood",           "hour_sin",     "hour_cos",
    "pet_rate",       "poke_rate",       "light",     "touched_lately", "event_lately", "idle_time",
    "sleepiness",     "touching",        "touch_x",   "touch_y",        "motion",       "tilt_x",
    "tilt_y",         "carried",         "eyes_open", "emote",          "surprise",     "lonely",
    "need_attention", "need_rest",       "need_play", "hour_liked",     "presence",     "battery_low",
    "watched",        "screen_activity", "trust",     "streak",         "bias"};
inline const char* const URGE_NAMES[OUT] = {
    "look_x", "look_y", "energy", "curious", "mirth", "suspicious", "drowsy", "restless", "blink", "mouth",
    "glitch", "yawn",   "emote",  "pupils",  "sleep", "wardrobe",   "lens",   "head_x",   "head_y"};
inline const char* const URGE_HELP[OUT] = {"wants to look left / right",
                                           "wants to look up / down",
                                           "jittery and quick / slow and lazy",
                                           "brows up, perky / flat",
                                           "smile and blush / a little frown",
                                           "side-eye, squint / relaxed",
                                           "eyelids heavy",
                                           "bounce, can't sit still",
                                           "blinks often / rarely",
                                           "small 'o' mouth / smirk",
                                           "static, the picture slips / clean",
                                           "about to yawn",
                                           "wants to show a feeling (an emote)",
                                           "pupils wide / small",
                                           "wants to go to sleep",
                                           "wants to change its look",
                                           "bulges the picture like a fisheye lens",
                                           "turns its head left / right",
                                           "nods its head down / up"};
}  // namespace mind

#ifndef TEEVEE_MIND_N
#define TEEVEE_MIND_N 32
#endif

class MindNet {
  public:
    static constexpr int IN = mind::IN, N = TEEVEE_MIND_N, OUT = mind::OUT;
    static constexpr uint32_t
        VERSION = 9,
        RES_SEED =
            0x7EE5EEDu;  // 3: head urges (OUT 17 -> 19); 4: presence + battery (IN 29 -> 31); 5: watched; 6: screen; 7: trust (IN 34); 8: streak (IN 35), the last one in floats; 9: int16. Older saves load

    // The net works in whole numbers, 16 bits each (fixed point), not in floats:
    //   signals (senses, the inner state, urges)  int16, the whole range for -1..1: 32767 is 1 (SIG_ONE; "Q15")
    //   weights (and the exploring noise)         int16, 4096 is 1 (W_ONE; "Q12"), so they reach +-8 and are held to +-3
    //   what it did lately (eligibility traces)   int16, 16384 is 1 (E_ONE; "Q14")
    // Sums are 32-bit, with 19 bits after the point (ACC). tanh is a table. The senses still arrive as floats (the feelings are
    // floats), and what the rest of the program reads (urge(), input(), explain()...) is given back in floats.
    static constexpr int32_t SIG_ONE = fx::SIG_ONE, W_ONE = 4096, E_ONE = 16384;
    static constexpr int ACC = fx::ACC;

    struct Weights {
        uint32_t version, updates, seed;
        int16_t wr[OUT][N];    // Q12
        int16_t wi[OUT][IN];   // Q12
        int16_t b[OUT];        // Q12
        int16_t hourLike[24];  // Q15
    };
    struct Reason {
        int input;
        float amount;
    };
    // The most bytes a save can be: the last float one (version 8) is about twice the size of today's. Read a save into a buffer
    // of this size, or an old one is cut short and what it learned is lost.
    static constexpr size_t MAX_SAVE = 3 * sizeof(uint32_t) + (size_t)(OUT * N + OUT * IN + OUT + 24) * sizeof(float);

    static float sigF(int32_t q) { return fx::sigF(q); }
    static float wF(int32_t q) { return q * (1.f / W_ONE); }
    static int16_t sigQ(float v) { return fx::sigQ(v); }
    static int16_t wQ(float v) {
      if (!(v == v)) return 0;
      const float c = v < -W_LIMIT ? -W_LIMIT : v > W_LIMIT ? W_LIMIT : v;
      return (int16_t)lroundf(c * W_ONE);
    }

    void init(uint32_t noiseSeed) {
      _rng = noiseSeed ? noiseSeed : 0x2545F491u;
      memset(&_w, 0, sizeof _w);
      _w.version = VERSION;
      _w.seed = RES_SEED;
      for (int k = 0; k < OUT; k++) {
        for (int i = 0; i < IN; i++) _w.wi[k][i] = priorQ(k, i);
        _w.b[k] = wQ(priorBias(k));
        for (int j = 0; j < N; j++) _w.wr[k][j] = wQ(0.1f * sqrtf(32.f / N) * unit(0xABCDu + k, j));
      }
      resetState();
    }
    // A save of this version, or an older one in floats (version 8, and mind::OLD_LAYOUTS before it): what it learned is kept, turned
    // into whole numbers; urges and senses added since start from the prior. Senses are only ever added before the bias, so an old
    // save's senses stay in place and its bias column moves to the bias's place.
    bool load(const void* blob, size_t len) {
      const uint8_t* p = (const uint8_t*)blob;
      uint32_t head[3];
      if (len < sizeof head) return false;
      memcpy(head, p, sizeof head);
      if (head[0] == VERSION && len == sizeof(Weights)) {
        memcpy(&_w, blob, sizeof _w);
        return true;
      }
      int oOut = 0, oIn = 0;
      for (const mind::SavedLayout& l : mind::OLD_LAYOUTS)
        if (l.version == head[0]) oOut = l.out, oIn = l.in;
      if (oOut <= 0 || oOut > OUT || oIn <= 0 || oIn > IN ||
          len != sizeof head + (size_t)(oOut * N + oOut * oIn + oOut + 24) * sizeof(float))
        return false;
      init(_rng);  // the prior, then the old weights over it
      _w.updates = head[1];
      _w.seed = head[2];
      p += sizeof head;
      auto next = [&p]() {
        float v;
        memcpy(&v, p, sizeof v);
        p += sizeof v;
        return v;
      };
      for (int k = 0; k < oOut; k++)
        for (int j = 0; j < N; j++) _w.wr[k][j] = wQ(next());
      for (int k = 0; k < oOut; k++) {
        for (int i = 0; i < oIn - 1; i++) _w.wi[k][i] = wQ(next());
        _w.wi[k][mind::I_BIAS] = wQ(next());
      }
      for (int k = 0; k < oOut; k++) _w.b[k] = wQ(next());
      for (int16_t& h : _w.hourLike) h = sigQ(next());
      return true;
    }
    const Weights& weights() const { return _w; }
    void setRates(float learnRate, float explore) {
      _lr = learnRate;
      _sigma = 2.f * explore;
    }

    void step(const MindInputs& in, float dt) {
      using namespace mind;
      // The senses: floats from the rest of the robot, made into signals here.
      const float dv = in.v - _pv, da = in.a - _pa;
      _pv = in.v;
      _pa = in.a;
      _novelty += (fminf(1.f, 3.f * (fabsf(dv) + fabsf(da))) - _novelty) * (1.f - expf(-dt / 3.f));
      const float hr = in.hour * 0.2618f;
      int16_t* x = _x;
      x[I_VALENCE] = sigQ(in.v);
      x[I_AROUSAL] = sigQ(in.a);
      x[I_DOMINANCE] = sigQ(in.d);
      x[I_MOOD] = sigQ(in.mood);
      x[I_HOUR_SIN] = in.hour < 0 ? 0 : sigQ(sinf(hr));
      x[I_HOUR_COS] = in.hour < 0 ? 0 : sigQ(cosf(hr));
      x[I_PET] = sigQ(in.petRate / 3);
      x[I_POKE] = sigQ(in.pokeRate / 3);
      x[I_LIGHT] = sigQ(in.light);
      x[I_SINCE_TOUCH] = sigQ(expf(-in.sinceTouch / 20.f));
      x[I_SINCE_EVENT] = sigQ(expf(-in.sinceEvent / 60.f));
      x[I_IDLE] = sigQ(in.idleSec / 240.f);
      x[I_SLEEPY] = sigQ(in.sleepy);
      x[I_TOUCHING] = in.touching ? SIG_ONE : 0;
      x[I_TOUCH_X] = sigQ(in.tx);
      x[I_TOUCH_Y] = sigQ(in.ty);
      x[I_MOTION] = sigQ(in.motion);
      x[I_TILT_X] = sigQ(in.tiltX);
      x[I_TILT_Y] = sigQ(in.tiltY);
      x[I_CARRIED] = in.carried ? SIG_ONE : 0;
      x[I_EYES] = sigQ(in.openness * 2 - 1);
      x[I_EMOTE] = in.emote ? SIG_ONE : 0;
      x[I_NOVELTY] = sigQ(_novelty);
      x[I_LONELY] = sigQ(in.idleSec / 1800.f);
      x[I_PRESENCE] = sigQ(fmaxf(in.presence, 0.f));
      x[I_BATTERY] = sigQ(fmaxf(in.batteryLow, 0.f));
      x[I_WATCHED] = sigQ(fmaxf(in.watched, 0.f));
      x[I_SCREEN] = sigQ(fmaxf(in.screen, 0.f));
      x[I_TRUST] = sigQ(in.trust);
      x[I_STREAK] = sigQ(in.streak);
      x[I_NEED_ATTN] = sigQ(in.needAttention);
      x[I_NEED_REST] = sigQ(in.needRest);
      x[I_NEED_PLAY] = sigQ(in.needPlay);
      _hour = in.hour;
      x[I_HOUR_LIKE] = in.hour >= 0 && in.hour < 24 ? _w.hourLike[(int)in.hour] : 0;
      if (in.weekKnown) x[I_HOUR_LIKE] = (int16_t)((x[I_HOUR_LIKE] + sigQ(in.weekLike)) / 2);
      x[I_BIAS] = SIG_ONE;

      // This step's rates, worked out once (in floats) and used as whole numbers below.
      const int32_t alpha = rateQ(1.f - expf(-dt / 0.3f));
      const float rhoF = expf(-dt / 2.5f);
      const int32_t rho = rateQ(rhoF), nz = rateQ(sqrtf(1 - rhoF * rhoF)), keep = rateQ(1.f - expf(-dt / 6.f));
      const int32_t sigma = (int32_t)lroundf(_sigma * W_ONE);
      _t += dt;
      static const float PERIOD[3] = {17.f, 47.f,
                                      131.f};  // slow inner rhythms, so it never settles into a fixed state
      int16_t osc[3];
      for (int q = 0; q < 3; q++) osc[q] = sigQ(sinf(6.2831853f * _t / PERIOD[q] + 2.1f * q));

      // The inner state. Products are weight (Q12) x signal (Q15) = Q27, added up at Q19 (ACC).
      int16_t rn[N];
      const Wiring& wire = wiring();
      for (int j = 0; j < N; j++) {
        int32_t s = (gauss() * 6554) >> (30 - ACC);  // 0.2 x the noise
        for (int q = 0; q < 3; q++) s += (wire.osc[j][q] * osc[q]) >> (27 - ACC);
        const int16_t* wi = wire.in[j];
        for (int i = 0; i < IN; i++) s += (wi[i] * x[i]) >> (27 - ACC);
        const int16_t* wr = wire.rec + wire.first[j];
        const uint8_t* from = wire.from + wire.first[j];
        for (int n = wire.first[j + 1] - wire.first[j]; n > 0; n--) s += (*wr++ * _r[*from++]) >> (27 - ACC);
        rn[j] = (int16_t)(_r[j] + rsh((tanhQ(s) - _r[j]) * alpha, 15));
      }
      memcpy(_r, rn, sizeof _r);

      // The urges, and what this step's exploring did (the traces).
      const bool learning = _lr > 0 && _sigma > 0;
      for (int k = 0; k < OUT; k++) {
        int32_t z = _w.b[k] * (1 << (ACC - 12));
        const int16_t *wi = _w.wi[k], *wr = _w.wr[k];
        for (int i = 0; i < IN; i++) z += (wi[i] * x[i]) >> (27 - ACC);
        for (int j = 0; j < N; j++) z += (wr[j] * _r[j]) >> (27 - ACC);
        const int32_t o = tanhQ(z);
        // the exploring noise, Q12: it drifts (an Ornstein-Uhlenbeck walk), held to the int16 range
        const int32_t n = clamp16(rsh(rho * _n[k], 15) + rsh(nz * gauss(), 18));
        _n[k] = (int16_t)n;
        _o[k] = (int16_t)o;
        _a[k] = (int16_t)clampSig(o + rsh(sigma * n, 9));
        if (learning) {  // remember what this perturbation was: trace += (1 - lam) * (noise * (1 - o^2) * signal - trace)
          const int32_t slope = SIG_ONE - ((o * o) >> 15);                // 1 - o^2, Q15
          const int32_t g = clampG(rsh(n * slope, 13));                   // Q12 x Q15 -> Q14
          int16_t *er = _er[k], *ei = _ei[k];
          for (int j = 0; j < N; j++) er[j] = (int16_t)(er[j] + rsh((rsh(g * _r[j], 15) - er[j]) * keep, 15));
          for (int i = 0; i < IN; i++) ei[i] = (int16_t)(ei[i] + rsh((rsh(g * x[i], 15) - ei[i]) * keep, 15));
          _eb[k] = (int16_t)(_eb[k] + rsh((g - _eb[k]) * keep, 15));
        }
      }

      // The slow pull back to the innate prior, so a few bad days do not rewrite it. Per step it is far less than one step of a
      // weight (2e-6 of the distance), so it is saved up and paid out every so often, each weight moving a whole number of steps:
      // the fraction left over is settled by chance, so on average it is exact.
      _pull += 2e-6f * dt * 10;
      if (_pull >= PULL_EVERY) {
        const float p = _pull;
        _pull = 0;
        for (int k = 0; k < OUT; k++) {
          for (int i = 0; i < IN; i++) _w.wi[k][i] = (int16_t)(_w.wi[k][i] + whole(p * (priorQ(k, i) - _w.wi[k][i])));
          for (int j = 0; j < N; j++) _w.wr[k][j] = (int16_t)(_w.wr[k][j] - whole(p * 0.5f * _w.wr[k][j]));
        }
      }
    }

    void reward(float r) {
      if (r == 0 || _lr <= 0) return;
      if (_hour >= 0 && _hour < 24) {
        int16_t& hl = _w.hourLike[(int)_hour];
        hl = (int16_t)(hl + rsh((sigQ(r) - hl) * 3277, 15));  // 0.1 of the way
      }
      // the step per weight shrinks with the size, so a reaction moves the urges the same way at any N
      const float kf = GAIN * _lr * r * (32.f / N);
      const int32_t k = (int32_t)lroundf((kf < -7 ? -7 : kf > 7 ? 7 : kf) * W_ONE);  // Q12
      for (int u = 0; u < OUT; u++) {  // weight (Q12) += k (Q12) x trace (Q14)
        for (int j = 0; j < N; j++) _w.wr[u][j] = clampw(_w.wr[u][j] + rsh(k * _er[u][j], 14));
        for (int i = 0; i < IN; i++) _w.wi[u][i] = clampw(_w.wi[u][i] + rsh(k * _ei[u][i], 14));
        _w.b[u] = clampw(_w.b[u] + rsh(k * _eb[u], 14));
      }
      _w.updates++;
    }

    float urge(int k) const { return sigF(_a[k]); }
    float raw(int k) const { return sigF(_o[k]); }
    float input(int i) const { return sigF(_x[i]); }
    float inner(int j) const { return sigF(_r[j]); }
    // The numbers themselves, as the net holds them (int16; 32767 = 1).
    int16_t urgeQ(int k) const { return _a[k]; }
    int16_t inputQ(int i) const { return _x[i]; }
    int16_t innerQ(int j) const { return _r[j]; }
    // A learned weight as a float: an urge's weight on a sense, and how much it likes an hour of the day.
    float weightIn(int urge, int sense) const { return wF(_w.wi[urge][sense]); }
    float hourLiked(int hour) const { return hour >= 0 && hour < 24 ? sigF(_w.hourLike[hour]) : 0.f; }
    float activity() const {
      float s = 0;
      for (int16_t v : _r) s += sigF(v) * sigF(v);
      return sqrtf(s / N);
    }

    int explain(int urge, Reason* out, int max) const {
      Reason all[IN + 2];
      int n = 0;
      for (int i = 0; i < IN - 1; i++) all[n++] = {i, wF(_w.wi[urge][i]) * sigF(_x[i])};
      float inner = 0;
      for (int j = 0; j < N; j++) inner += wF(_w.wr[urge][j]) * sigF(_r[j]);
      all[n++] = {-1, inner};
      all[n++] = {-2, wF(_w.b[urge]) + wF(_w.wi[urge][mind::I_BIAS])};
      return top(all, n, out, max);
    }
    // What it has learned about an urge: the senses whose weight moved most from the prior (signed shift).
    int learned(int urge, Reason* out, int max) const {
      Reason all[IN];
      int n = 0;
      for (int i = 0; i < IN - 1; i++) all[n++] = {i, wF(_w.wi[urge][i] - priorQ(urge, i))};
      return top(all, n, out, max);
    }
    float shift(int urge) const {  // total distance from the prior
      int32_t s = 0;
      for (int i = 0; i < IN; i++) s += abs(_w.wi[urge][i] - priorQ(urge, i));
      return wF(s);
    }
    // The innate prior: which senses each urge starts out following, as the net holds it (a weight, Q12). Looked up once per urge
    // and sense (a table, rebuilt only when the behaviour's instincts change), not searched on every step.
    static int16_t priorQ(int u, int i) {
      static int16_t table[OUT][IN];
      static const mind::Instinct* key = nullptr;
      static int keyCount = -1;
      if (key != mind::INSTINCTS || keyCount != mind::INSTINCT_COUNT) {
        for (int k = 0; k < OUT; k++)
          for (int j = 0; j < IN; j++) table[k][j] = wQ(priorRaw(k, j));
        key = mind::INSTINCTS;
        keyCount = mind::INSTINCT_COUNT;
      }
      return table[u][i];
    }
    // The same as it was written (a float; searched, so not for the step).
    static float prior(int u, int i) { return priorRaw(u, i); }
    static float priorRaw(int u, int i) {
      using namespace mind;
      for (int k = 0; k < INSTINCT_COUNT; k++)
        if (INSTINCTS[k].urge == u && INSTINCTS[k].sense == i) return INSTINCTS[k].w;
      struct W {
          uint8_t u, i;
          float w;
      };
    static const W T[] = {
        {U_GAZE_X, I_TILT_X, 1.0f},      {U_GAZE_Y, I_VALENCE, -0.4f},    {U_GAZE_Y, I_SLEEPY, 0.7f},    {U_GAZE_Y, I_TILT_Y, 1.0f},
        {U_ENERGY, I_AROUSAL, 0.9f},     {U_ENERGY, I_MOTION, 0.5f},      {U_ENERGY, I_SLEEPY, -0.8f},   {U_ENERGY, I_SINCE_EVENT, 0.3f},
        {U_CURIOUS, I_NOVELTY, 0.9f},    {U_CURIOUS, I_AROUSAL, 0.3f},    {U_CURIOUS, I_CARRIED, 0.4f},  {U_CURIOUS, I_TOUCHING, 0.3f},
        {U_CURIOUS, I_SLEEPY, -0.6f},    {U_MIRTH, I_VALENCE, 1.0f},      {U_MIRTH, I_MOOD, 0.5f},       {U_MIRTH, I_PET, 0.6f},
        {U_MIRTH, I_TOUCHING, 0.2f},     {U_SUSPICIOUS, I_VALENCE, -0.5f}, {U_SUSPICIOUS, I_POKE, 0.9f}, {U_SUSPICIOUS, I_DOMINANCE, 0.3f},
        {U_DROWSY, I_SLEEPY, 1.2f},      {U_DROWSY, I_IDLE, 0.6f},        {U_DROWSY, I_HOUR_COS, 0.3f},  {U_DROWSY, I_AROUSAL, -0.5f},
        {U_DROWSY, I_LIGHT, -0.3f},      {U_RESTLESS, I_AROUSAL, 0.7f},   {U_RESTLESS, I_IDLE, 0.4f},    {U_RESTLESS, I_VALENCE, 0.3f},
        {U_RESTLESS, I_SLEEPY, -0.9f},   {U_BLINK, I_SLEEPY, 0.5f},       {U_BLINK, I_AROUSAL, -0.2f},   {U_BLINK, I_VALENCE, -0.2f},
        {U_MOUTH, I_PET, 0.4f},          {U_MOUTH, I_AROUSAL, 0.2f},
        {U_GLITCH, I_NOVELTY, 1.8f},     {U_GLITCH, I_MOTION, 1.4f},      {U_GLITCH, I_POKE, 0.9f},      {U_GLITCH, I_AROUSAL, 0.8f},
        {U_GLITCH, I_VALENCE, -0.4f},    {U_GLITCH, I_CARRIED, 0.5f},
        {U_YAWN, I_SLEEPY, 0.9f},        {U_YAWN, I_IDLE, 0.6f},          {U_YAWN, I_LONELY, 0.7f},      {U_YAWN, I_HOUR_COS, 0.4f},
        {U_YAWN, I_AROUSAL, -0.6f},      {U_YAWN, I_TOUCHING, -0.8f},
        {U_EMOTE, I_PET, 0.7f},          {U_EMOTE, I_NOVELTY, 0.6f},      {U_EMOTE, I_VALENCE, 0.4f},    {U_EMOTE, I_LONELY, 0.5f},
        {U_EMOTE, I_SLEEPY, -1.0f},
        {U_EMOTE, I_NEED_ATTN, 0.9f},    {U_DROWSY, I_NEED_REST, 0.8f},   {U_YAWN, I_NEED_REST, 0.9f},   {U_BLINK, I_NEED_REST, 0.4f},
        {U_RESTLESS, I_NEED_PLAY, 1.0f}, {U_RESTLESS, I_NEED_ATTN, 0.4f}, {U_CURIOUS, I_NEED_PLAY, 0.5f}, {U_MIRTH, I_NEED_ATTN, -0.7f},
        {U_ENERGY, I_NEED_REST, -0.5f},  {U_GAZE_Y, I_NEED_ATTN, -0.2f},
        {U_PUPIL, I_AROUSAL, 0.7f},      {U_PUPIL, I_NOVELTY, 0.8f},      {U_PUPIL, I_LIGHT, -0.5f},     {U_PUPIL, I_VALENCE, 0.3f},
        {U_PUPIL, I_SLEEPY, -0.8f},
        {U_SLEEP, I_NEED_REST, 1.3f},    {U_SLEEP, I_SLEEPY, 0.8f},       {U_SLEEP, I_IDLE, 0.5f},       {U_SLEEP, I_HOUR_COS, 0.6f},
        {U_SLEEP, I_AROUSAL, -0.6f},     {U_SLEEP, I_TOUCHING, -2.0f},            {U_MIRTH, I_HOUR_LIKE, 0.5f},    {U_EMOTE, I_HOUR_LIKE, 0.3f},
        {U_LENS, I_NOVELTY, 0.8f},       {U_LENS, I_AROUSAL, 0.4f},       {U_LENS, I_SLEEPY, -1.0f},
        {U_WARDROBE, I_NEED_PLAY, 1.0f}, {U_WARDROBE, I_LONELY, 0.6f},    {U_WARDROBE, I_NOVELTY, 0.5f}, {U_WARDROBE, I_SLEEPY, -1.2f},
        {U_WARDROBE, I_TOUCHING, -2.0f},
        {U_HEAD_X, I_TOUCH_X, 0.8f},     {U_HEAD_X, I_TILT_X, 0.6f},      {U_HEAD_X, I_CARRIED, 0.3f},
        {U_HEAD_Y, I_SLEEPY, 0.6f},      {U_HEAD_Y, I_VALENCE, -0.4f},    {U_HEAD_Y, I_TOUCH_Y, 0.5f},   {U_HEAD_Y, I_TILT_Y, 0.6f},
        {U_HEAD_Y, I_NEED_ATTN, -0.3f},
        {U_CURIOUS, I_PRESENCE, 0.3f},   {U_ENERGY, I_PRESENCE, 0.25f},   {U_SLEEP, I_PRESENCE, -0.8f},  {U_YAWN, I_PRESENCE, -0.3f},
        {U_DROWSY, I_BATTERY, 0.7f},     {U_ENERGY, I_BATTERY, -0.6f},    {U_SLEEP, I_BATTERY, 0.4f},    {U_YAWN, I_BATTERY, 0.4f},
        {U_MIRTH, I_WATCHED, 0.3f},      {U_EMOTE, I_WATCHED, 0.3f},      {U_SLEEP, I_WATCHED, -0.5f},   {U_GAZE_Y, I_WATCHED, -0.2f},
        {U_CURIOUS, I_SCREEN, 0.4f},     {U_ENERGY, I_SCREEN, 0.2f},      {U_DROWSY, I_SCREEN, -0.3f},
        {U_MIRTH, I_TRUST, 0.35f},       {U_SUSPICIOUS, I_TRUST, -0.6f},  {U_EMOTE, I_TRUST, 0.2f},
        {U_MIRTH, I_STREAK, 0.4f},       {U_ENERGY, I_STREAK, 0.3f}};
      for (const W& w : T)
        if (w.u == u && w.i == i) return w.w;
      return 0;
    }
    static float priorBias(int u) {
      using namespace mind;
      return u == U_LENS         ? -1.0f
             : u == U_WARDROBE   ? -1.2f
             : u == U_SLEEP      ? -1.3f
             : u == U_YAWN       ? -1.0f
             : u == U_EMOTE      ? -1.1f
             : u == U_GLITCH     ? -0.6f
             : u == U_RESTLESS   ? -0.6f
             : u == U_MOUTH      ? -0.3f
             : u == U_SUSPICIOUS ? -0.4f
             : u == U_CURIOUS    ? -0.2f
             : u == U_DROWSY     ? -0.2f
                                 : 0.f;
    }

  private:
    static constexpr float GAIN = 2.f;
    static constexpr float W_LIMIT = 3.f;       // weights are held to +-3
    static constexpr float PULL_EVERY = 5e-4f;  // the pull to the prior is paid out when this much of the distance is due (about 25 s)

    // Whole-number helpers. rsh: a shift right that rounds to the nearest (a plain shift always rounds down, which would
    // slowly drag every decaying value below zero).
    static int32_t rsh(int32_t v, int s) { return fx::rsh(v, s); }
    static int32_t clamp16(int32_t v) { return v < -32767 ? -32767 : v > 32767 ? 32767 : v; }
    static int32_t clampSig(int32_t v) { return v < -SIG_ONE ? -SIG_ONE : v > SIG_ONE ? SIG_ONE : v; }
    static int32_t clampG(int32_t v) { return v < -32767 ? -32767 : v > 32767 ? 32767 : v; }  // (a trace's target: +-2 at Q14)
    static int16_t clampw(int32_t v) {
      const int32_t lim = (int32_t)(W_LIMIT * W_ONE);
      return (int16_t)(v < -lim ? -lim : v > lim ? lim : v);
    }
    static int32_t rateQ(float v) { return (int32_t)lroundf((v < 0 ? 0 : v > 1 ? 1 : v) * 32768.f); }  // 0..1 as Q15
    // A float amount of weight steps as a whole number: the fraction is settled by chance.
    int32_t whole(float v) {
      const float f = floorf(v);
      return (int32_t)f + ((rnd() >> 8) * (1.f / 16777216.f) < v - f ? 1 : 0);
    }

    static int32_t tanhQ(int32_t s) { return fx::tanhQ(s); }  // (the table is shared with the look net: fixed.h)

    // The inner state's fixed wiring: random from the seed alone, so it is worked out once (not every step). Weights at Q12.
    // Which inner units each one listens to: about a quarter of all pairs, so the links are kept one after another (unit j's are
    // first[j] .. first[j + 1]), not in N x N tables that would be three quarters empty.
    struct Wiring {
        int16_t osc[N][3];      // the slow rhythms
        int16_t in[N][IN];      // the senses
        uint16_t first[N + 1];  // where each unit's links start
        uint8_t* from;          // the unit a link listens to
        int16_t* rec;           // and its weight
    };
    static bool linked(int j, int m) { return ((hash(0x2222u + j, m) >> 3) & 7) < 2; }
    static const Wiring& wiring() {
      static Wiring w;
      static bool built = false;
      if (!built) {
        const float rscale = sqrtf(32.f / N);
        int links = 0;
        for (int j = 0; j < N; j++)
          for (int m = 0; m < N; m++) links += linked(j, m);
        w.from = (uint8_t*)malloc(links ? links : 1);
        w.rec = (int16_t*)malloc((links ? links : 1) * sizeof(int16_t));
        const bool room = w.from && w.rec;  // (no memory: an inner state with no links of its own; it still follows the senses)
        int n = 0;
        for (int j = 0; j < N; j++) {
          for (int q = 0; q < 3; q++) w.osc[j][q] = wQ(0.6f * unit(0x4444u + j, q));
          for (int i = 0; i < IN; i++) w.in[j][i] = wQ(0.4f * unit(0x1111u + j, resKey(i)));
          w.first[j] = (uint16_t)n;
          for (int m = 0; room && m < N; m++)
            if (linked(j, m)) {
              w.from[n] = (uint8_t)m;
              w.rec[n++] = wQ(rscale * unit(0x3333u + j, m));
            }
        }
        w.first[N] = (uint16_t)n;
        built = true;
      }
      return w;
    }
    void resetState() {
      memset(_r, 0, sizeof _r);
      memset(_n, 0, sizeof _n);
      memset(_o, 0, sizeof _o);
      memset(_a, 0, sizeof _a);
      memset(_er, 0, sizeof _er);
      memset(_ei, 0, sizeof _ei);
      memset(_eb, 0, sizeof _eb);
      _novelty = 0;
      _pull = 0;
    }
    static int top(Reason* all, int n, Reason* out, int max) {
      for (int i = 1; i < n; i++) {
        Reason r = all[i];
        int j = i - 1;
        while (j >= 0 && fabsf(all[j].amount) < fabsf(r.amount)) {
          all[j + 1] = all[j];
          j--;
        }
        all[j + 1] = r;
      }
      int m = n < max ? n : max;
      for (int i = 0; i < m; i++) out[i] = all[i];
      return m;
    }

    static uint32_t hash(uint32_t a, uint32_t b) {
      uint32_t h = RES_SEED ^ (a * 0x9E3779B1u) ^ (b * 0x85EBCA77u);
      h ^= h >> 16;
      h *= 0x7FEB352Du;
      h ^= h >> 15;
      h *= 0x846CA68Bu;
      h ^= h >> 16;
      return h;
    }
    static float unit(uint32_t a, uint32_t b) { return (hash(a, b) >> 8) * (2.f / 16777216.f) - 1.f; }
    // A sense's key for its fixed random wiring into the inner state. The first 28 senses and the bias keep the keys they had when there were
    // 29 senses (the bias was the 29th), the ones added later come after: adding a sense never rewires the others, so a saved net keeps
    // the inner state it learned with.
    static int resKey(int i) {
      return i < mind::I_PRESENCE ? i : i == mind::I_BIAS ? (int)mind::I_PRESENCE : i + 1;
    }

    uint32_t rnd() {
      _rng ^= _rng << 13;
      _rng ^= _rng >> 17;
      _rng ^= _rng << 5;
      return _rng;
    }
    // Noise with a bell shape and a spread of 1, as a signal (Q15; it reaches about +-3.5, so it is 32-bit): four draws added up.
    int32_t gauss() {
      int32_t s = 0;
      for (int i = 0; i < 4; i++) s += (int32_t)(rnd() >> 8);  // four of 0..1 at Q24
      return (((s - (2 << 24)) >> 9) * 14189) >> 13;         // (sum - 2) x sqrt(3), Q24 -> Q15
    }

    Weights _w{};
    int16_t _x[IN]{}, _r[N]{}, _o[OUT]{}, _a[OUT]{};  // signals, Q15
    int16_t _n[OUT]{};                                // the exploring noise, Q12
    int16_t _er[OUT][N]{}, _ei[OUT][IN]{}, _eb[OUT]{};  // traces, Q14
    float _pv = 0, _pa = 0, _novelty = 0, _t = 0, _hour = -1, _pull = 0;
    float _lr = 0.05f, _sigma = 0.3f;
    uint32_t _rng = 1;
};

//==============================
// explaining it
//==============================
#include <stdio.h>

inline const char* mindReasonName(int input) {
  return input == -1 ? "inner state" : input == -2 ? "habit" : mind::INPUT_NAMES[input];
}

// Plain-text report: what it senses, what it wants and why, and what it has learned from you.
inline size_t mindReport(const MindNet& m, char* out, size_t n) {
  size_t k = 0;
#define APPEND(...)                                                                                          \
  do {                                                                                                       \
    if (k < n) k += snprintf(out + k, n - k, __VA_ARGS__);                                                   \
  } while (0)
  APPEND("mind net: %lu reactions learned from, inner state activity %.2f\n",
         (unsigned long)m.weights().updates, m.activity());
  APPEND("senses:");
  for (int i = 0; i < mind::IN - 1; i++)
    if (fabsf(m.input(i)) >= 0.05f) APPEND(" %s %+.2f", mind::INPUT_NAMES[i], m.input(i));
  APPEND(
      "\nurges (now; \"because\" = the strongest causes; \"learned\" = senses it has changed its mind about):\n");
  for (int u = 0; u < mind::OUT; u++) {
    MindNet::Reason why[3], lr[2];
    const int nw = m.explain(u, why, 3), nl = m.learned(u, lr, 2);
    APPEND("  %-10s %+.2f  %s\n             because", mind::URGE_NAMES[u], m.urge(u), mind::URGE_HELP[u]);
    for (int i = 0; i < nw; i++)
      APPEND("%s %s %+.2f", i ? "," : "", mindReasonName(why[i].input), why[i].amount);
    if (lr[0].input >= 0 && fabsf(lr[0].amount) >= 0.1f) {
      APPEND("\n             learned");
      for (int i = 0; i < nl; i++)
        if (fabsf(lr[i].amount) >= 0.1f)
          APPEND("%s %s %+.2f", i ? "," : "", mindReasonName(lr[i].input), lr[i].amount);
    }
    APPEND("\n");
  }
#undef APPEND
  return k < n ? k : n;
}

// The same for the web portal: {"brain":{"updates":..,"activity":..,"senses":[..],"x":[..],"urges":[..],"raw":[..],"help":[..],
// "why":[[[name,amount],..],..],"learned":[..]}}
inline size_t mindJson(const MindNet& m, char* out, size_t n) {
  size_t k = 0;
#define APPEND(...)                                                                                          \
  do {                                                                                                       \
    if (k < n) k += snprintf(out + k, n - k, __VA_ARGS__);                                                   \
  } while (0)
  APPEND("{\"brain\":{\"updates\":%lu,\"activity\":%.2f,\"senses\":[", (unsigned long)m.weights().updates,
         m.activity());
  for (int i = 0; i < mind::IN - 1; i++) APPEND("%s\"%s\"", i ? "," : "", mind::INPUT_NAMES[i]);
  APPEND("],\"x\":[");
  for (int i = 0; i < mind::IN - 1; i++) APPEND("%s%.2f", i ? "," : "", m.input(i));
  APPEND("],\"names\":[");
  for (int u = 0; u < mind::OUT; u++) APPEND("%s\"%s\"", u ? "," : "", mind::URGE_NAMES[u]);
  APPEND("],\"help\":[");
  for (int u = 0; u < mind::OUT; u++) APPEND("%s\"%s\"", u ? "," : "", mind::URGE_HELP[u]);
  APPEND("],\"urges\":[");
  for (int u = 0; u < mind::OUT; u++) APPEND("%s%.2f", u ? "," : "", m.urge(u));
  APPEND("],\"inner\":[");
  for (int j = 0; j < MindNet::N; j++) APPEND("%s%.2f", j ? "," : "", m.inner(j));
  APPEND("],\"why\":[");
  for (int u = 0; u < mind::OUT; u++) {
    MindNet::Reason why[3];
    const int nw = m.explain(u, why, 3);
    APPEND("%s[", u ? "," : "");
    for (int i = 0; i < nw; i++)
      APPEND("%s[\"%s\",%.2f]", i ? "," : "", mindReasonName(why[i].input), why[i].amount);
    APPEND("]");
  }
  APPEND("],\"learned\":[");
  for (int u = 0; u < mind::OUT; u++) {
    MindNet::Reason lr[2];
    const int nl = m.learned(u, lr, 2);
    APPEND("%s[", u ? "," : "");
    for (int i = 0; i < nl; i++)
      if (fabsf(lr[i].amount) >= 0.1f)
        APPEND("%s[\"%s\",%.2f]", i ? "," : "", mindReasonName(lr[i].input), lr[i].amount);
    APPEND("]");
  }
  APPEND("]}}");
#undef APPEND
  return k < n ? k : n;
}
