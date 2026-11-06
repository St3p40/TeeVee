#pragma once
#include <math.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

class Wardrobe {
  public:
    static constexpr int MAX = 48;
    struct Saved {
        uint32_t version, count;
        float score[MAX];
    };
    static constexpr uint32_t VERSION = 2;

    float score[MAX] = {};
    int current = 0;

    void reward(float r) { score[current] += 0.15f * ((r < -1 ? -1 : r > 1 ? 1 : r) - score[current]); }

    // Picks the next look among `n` presets (names[0..n-1]). valence/arousal -1..1, sleepy 0..1, rnd01 returns 0..1. Never the current one.
    template <typename Rnd>
    int pick(const char* const* names, int n, float valence, float arousal, float sleepy, Rnd rnd01) const {
      int best = 0;
      float bv = -1e9f;
      for (int c = 0; c <= n && c < MAX; c++) {
        if (c == current) continue;
        float v = 1.6f * score[c] + 0.35f * rnd01();
        if (c == 0)
          v += 0.4f;
        else
          v += mood(names[c - 1], valence, arousal, sleepy);
        if (v > bv) bv = v, best = c;
      }
      return best;
    }

    Saved save(int n) const {
      Saved s{};
      s.version = VERSION;
      s.count = (uint32_t)n;
      memcpy(s.score, score, sizeof score);
      return s;
    }
    // A save is a prefix of Saved: an older, shorter one (fewer looks) loads, and the looks it did not have start at 0.
    // oldIndex (for the looks, not the mouths): where a version 1 save's preset i is now, -1 if it is gone.
    bool load(const void* blob, size_t len, int (*oldIndex)(int) = nullptr) {
      if (len < offsetof(Saved, score) || len > sizeof(Saved) ||
          (len - offsetof(Saved, score)) % sizeof(float))
        return false;
      Saved s{};
      memcpy(&s, blob, len);
      if (s.version != VERSION && s.version != 1) return false;
      for (int i = 0; i < MAX; i++) score[i] = 0;
      for (int i = 0; i < MAX; i++) {
        int to = i;
        if (s.version == 1 && oldIndex && i > 0) to = oldIndex(i - 1) + 1;
        if ((to > 0 || i == 0) && to < MAX)
          score[to] =
              s.score[i] >= -1 && s.score[i] <= 1 ? s.score[i] : 0;  // NaN or out of range: no opinion
      }
      return true;
    }

  private:
    static bool in(const char* name, const char* list) {
      const size_t n = strlen(name);
      for (const char* p = list; *p;) {
        const char* e = strchr(p, ' ');
        const size_t len = e ? (size_t)(e - p) : strlen(p);
        if (len == n && !strncmp(p, name, n)) return true;
        p += len + (e ? 1 : 0);
      }
      return false;
    }
    static float mood(const char* name, float valence, float arousal, float sleepy) {
      float m = 0;
      if (in(name,
             "sweet sparkle doll sunset round starry cat diamonds twins owl bashful curve wave dot teeth"))
        m += 0.9f * valence + 0.2f * arousal;
      if (in(name, "dreamy ghost noir sunset moonlit vapor bashful curve dot"))
        m += 0.9f * sleepy - 0.5f * arousal;
      if (in(name,
             "monster vampire grump robot visor alien neon hypno retro matrix scan crosshair inkwell lopsided pirate static zigzag fangs bar"))
        m += -0.9f * valence + 0.2f * arousal;
      return m;
    }
};
