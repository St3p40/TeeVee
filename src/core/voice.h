#pragma once
#include <math.h>
#include <stdint.h>
#include "emotion.h"

// The voice: short synthesized chirps and trills, one kind per event, coloured by the mood (happier = higher, more aroused
// = quicker) and never quite the same twice (pitch, length and the number of notes wander a little). No samples: each
// utterance is a few notes, each a pitch glide with a soft envelope, a waveform, vibrato and a little breath.
// Pure logic, no Arduino deps: the board's audio task calls render() for each block it sends to the speaker.

enum VoiceWave : uint8_t { VW_SINE, VW_TRI, VW_SOFTSQ, VW_BREATH };

struct VoiceNote {
    float f0, f1;    // Hz, glided from f0 to f1
    uint16_t ms;     // length
    uint16_t gapMs;  // silence after it
    uint8_t wave;
    float vol;       // 0..1
    float vib;       // vibrato depth, fraction of the pitch
};

class Voice {
  public:
    static constexpr int MAX_NOTES = 8;

    // Queue the sound for an event. mood: the current feeling (valence lifts the pitch, arousal the tempo). Replaces
    // anything still playing: a new feeling interrupts the old one.
    void say(uint8_t ev, const PAD& mood, float k) {
      _n = 0;
      _rng = _rng * 1664525u + 1013904223u + ev;
      const float base = 470.f * powf(2.f, 0.35f * mood.v + 0.1f * k) * wander(0.05f);
      const float tempo = 1.f / (1.f + 0.3f * mood.a);  // > 0: quicker when aroused
      const float vol = 0.55f + 0.35f * k;
      auto add = [&](float a, float b, int ms, int gap, uint8_t w, float v, float vib = 0) {
        if (_n < MAX_NOTES)
          _notes[_n++] = {base * a, base * b, (uint16_t)(ms * tempo * wander(0.15f)), (uint16_t)(gap * tempo), w, v * vol, vib};
      };
      switch (ev) {
        case EV_PET: {  // a soft trill, like a purr with a tune
          const int n = 3 + (int)(rnd() * 3);
          for (int i = 0; i < n; i++) add(i & 1 ? 1.19f : 1.f, i & 1 ? 1.19f : 1.f, 55, 10, VW_SINE, 0.5f, 0.02f);
          break;
        }
        case EV_POKE: add(1.2f, 1.6f, 60, 0, VW_TRI, 0.7f); break;
        case EV_PRAISE:
          add(1.f, 1.f, 70, 15, VW_SINE, 0.8f);
          add(1.26f, 1.26f, 70, 15, VW_SINE, 0.8f);
          add(1.5f, 1.5f, 70, 15, VW_SINE, 0.8f);
          add(2.f, 2.1f, 140, 0, VW_SINE, 0.8f, 0.015f);
          break;
        case EV_SCOLD: add(0.8f, 0.5f, 380, 0, VW_TRI, 0.7f, 0.03f); break;
        case EV_STARTLE:
          add(2.2f, 3.f, 80, 20, VW_TRI, 0.9f);
          if (rnd() < 0.6f) add(1.8f, 1.6f, 60, 0, VW_SINE, 0.6f);
          break;
        case EV_WIN:
          add(1.f, 1.f, 90, 20, VW_SINE, 0.9f);
          add(1.5f, 1.5f, 90, 20, VW_SINE, 0.9f);
          add(2.f, 2.f, 260, 0, VW_SINE, 0.9f, 0.02f);
          break;
        case EV_FAIL:
          add(1.2f, 1.15f, 140, 30, VW_TRI, 0.7f);
          add(1.f, 0.95f, 140, 30, VW_TRI, 0.7f);
          add(0.75f, 0.6f, 320, 0, VW_TRI, 0.7f, 0.03f);
          break;
        case EV_GREET:
          add(1.33f, 1.33f, 80, 25, VW_SINE, 0.8f);
          add(1.f, 1.12f, 150, 0, VW_SINE, 0.8f, 0.01f);
          break;
        case EV_TIRED:
          add(0.9f, 0.55f, 600, 0, VW_SINE, 0.45f, 0.01f);
          add(0.5f, 0.45f, 300, 0, VW_BREATH, 0.25f);
          break;
        case EV_ANNOY: add(0.6f, 0.55f, 260, 0, VW_SOFTSQ, 0.5f, 0.06f); break;
        case EV_LOVE:
          add(1.f, 1.5f, 300, 0, VW_SINE, 0.7f);
          add(1.5f, 1.5f, 260, 0, VW_SINE, 0.6f, 0.025f);
          break;
        case EV_CONFUSE: add(0.9f, 1.4f, 220, 0, VW_TRI, 0.7f); break;
        case EV_BORE: add(0.7f, 0.5f, 450, 0, VW_BREATH, 0.35f); break;
        default: break;
      }
      _cur = 0;
      _t = 0;
      _phase = 0;
    }

    bool busy() const { return _cur < _n; }

    // Fill n mono samples at `rate` Hz (silence when nothing plays). volume 0..1.
    void render(int16_t* out, int n, int rate, float volume) {
      for (int i = 0; i < n; i++) {
        if (_cur >= _n) {
          out[i] = 0;
          continue;
        }
        const VoiceNote& nt = _notes[_cur];
        const float len = nt.ms * 0.001f, gap = nt.gapMs * 0.001f;
        float s = 0;
        if (_t < len) {
          const float u = _t / len;
          const float f = (nt.f0 + (nt.f1 - nt.f0) * u) * (1.f + nt.vib * sinf(_t * 2 * 3.14159265f * 7.f));
          _phase += f / rate;
          _phase -= floorf(_phase);
          s = wave(nt.wave, _phase);
          // envelope: 8 ms in, 25 ms out (or less on very short notes)
          const float att = fminf(0.008f, len * 0.3f), rel = fminf(0.025f, len * 0.4f);
          const float env = _t < att ? _t / att : _t > len - rel ? (len - _t) / rel : 1.f;
          s *= env * nt.vol;
        }
        out[i] = (int16_t)(s * volume * 26000.f);
        _t += 1.f / rate;
        if (_t >= len + gap) {
          _cur++;
          _t = 0;
        }
      }
    }

  private:
    VoiceNote _notes[MAX_NOTES];
    int _n = 0, _cur = 0;
    float _t = 0, _phase = 0;
    uint32_t _rng = 0x9E3779B9u, _noise = 0x12345678u;

    float rnd() {
      _rng = _rng * 1664525u + 1013904223u;
      return (_rng >> 8) * (1.f / 16777216.f);
    }
    float wander(float amount) { return 1.f + amount * (2.f * rnd() - 1.f); }
    float wave(uint8_t w, float ph) {
      switch (w) {
        case VW_TRI: return 4.f * fabsf(ph - 0.5f) - 1.f;
        case VW_SOFTSQ: return tanhf(3.f * sinf(ph * 2 * 3.14159265f)) * 0.8f;
        case VW_BREATH: {
          _noise ^= _noise << 13, _noise ^= _noise >> 17, _noise ^= _noise << 5;
          const float nz = (_noise >> 8) * (2.f / 16777216.f) - 1.f;
          return 0.35f * sinf(ph * 2 * 3.14159265f) + 0.5f * nz;
        }
        default: return sinf(ph * 2 * 3.14159265f);
      }
    }
};

// When to speak: not at every event (a long pet sends one every 0.8 s), never while asleep, and a little left to chance so
// it does not answer like a machine.
class VoiceGate {
  public:
    bool shouldSay(uint8_t ev, float k, uint32_t now, bool asleep, float chance01) {
      if (asleep || ev >= EV_COUNT || k < 0.15f) return false;
      const uint32_t minGap = ev == EV_PET ? 2600 : 1200;
      if (_last && now - _last < 900) return false;            // one thing at a time
      if (_lastEv[ev] && now - _lastEv[ev] < minGap) return false;  // and not the same one again so soon
      const float want = ev == EV_PET ? 0.55f : ev == EV_STARTLE || ev == EV_WIN || ev == EV_GREET ? 0.95f : 0.8f;
      if (chance01 > want) return false;
      _last = _lastEv[ev] = now ? now : 1;
      return true;
    }

  private:
    uint32_t _last = 0, _lastEv[EV_COUNT] = {};
};
