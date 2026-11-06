#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "emotion.h"

class Diary {
  public:
    static constexpr int DAYS = 7, HOURS = 24, KINDS = 8;
    enum Kind : uint8_t { K_PET, K_POKE, K_PRAISE, K_SCOLD, K_GREET, K_LOVE, K_ANNOY, K_WIN };
    static constexpr const char* NAMES[KINDS] = {"pet",   "poke", "praise", "scold",
                                                 "greet", "love", "annoy",  "win"};

    struct Day {
        uint32_t id = 0;
        uint16_t n[KINDS] = {};
        int16_t moodSum[HOURS] = {};
        uint16_t moodN[HOURS] = {};
    };
    struct Saved {
        uint32_t version = 0, today = 0;
        Day days[DAYS];  // days[0] = today, then older
    };
    static constexpr uint32_t VERSION = 1;

    void begin(uint32_t today) {
      _d = Saved();
      _d.version = VERSION;
      _d.today = today;
      _d.days[0].id = today;
    }
    uint32_t today() const { return _d.today; }

    // The calendar day changed: older days shift down, a new empty one is today.
    void newDay(uint32_t id) {
      if (id == _d.today) return;
      memmove(&_d.days[1], &_d.days[0], sizeof(Day) * (DAYS - 1));
      _d.days[0] = Day();
      _d.days[0].id = id;
      _d.today = id;
      _dirty = true;
    }

    void event(uint8_t type) {
      int k = -1;
      switch (type) {
        case EV_PET: k = K_PET; break;
        case EV_POKE: k = K_POKE; break;
        case EV_PRAISE: k = K_PRAISE; break;
        case EV_SCOLD: k = K_SCOLD; break;
        case EV_GREET: k = K_GREET; break;
        case EV_LOVE: k = K_LOVE; break;
        case EV_ANNOY: k = K_ANNOY; break;
        case EV_WIN: k = K_WIN; break;
        default: break;
      }
      if (k < 0) return;
      if (_d.days[0].n[k] < 65535) _d.days[0].n[k]++;
      _dirty = true;
    }

    void mood(float valence, float hour) {
      int h = (int)hour;
      if (h < 0 || h >= HOURS) return;
      Day& d = _d.days[0];
      if (d.moodN[h] >= 2000) return;
      d.moodSum[h] += (int16_t)(valence * 100.f);
      d.moodN[h]++;
      _dirty = true;
    }

    const Day& day(int i) const { return _d.days[i]; }

    struct Feel {
        float warmth, lonely;
    };
    Feel feel() const {
      const Day& d = _d.days[0];
      const float kind = d.n[K_PET] + 1.5f * (d.n[K_PRAISE] + d.n[K_LOVE]) + d.n[K_GREET] + d.n[K_WIN];
      const float hard = 2.f * d.n[K_SCOLD] + d.n[K_ANNOY] + 0.3f * d.n[K_POKE];
      long sum = 0, samples = 0;
      for (int h = 0; h < HOURS; h++) sum += d.moodSum[h], samples += d.moodN[h];
      const float balance = (kind - hard) / (kind + hard + 3.f),
                  mood = samples ? sum / (100.f * samples) : 0.f;
      const float warmth = 0.75f * balance + 0.25f * mood;
      const float seen = kind + hard, alone = seen >= 6 ? 0.f : 1.f - seen / 6.f;
      const float upFor = samples >= 120 ? 1.f : samples / 120.f;
      return {warmth < -1 ? -1.f : warmth > 1 ? 1.f : warmth, alone * upFor};
    }
    static int moodAt(const Day& d, int h) { return d.moodN[h] ? d.moodSum[h] / d.moodN[h] : -128; }
    int bestHour(int dayIndex = 0) const {
      int best = -1, bv = -200;
      for (int h = 0; h < HOURS; h++) {
        int v = moodAt(_d.days[dayIndex], h);
        if (v != -128 && v > bv) bv = v, best = h;
      }
      return best;
    }

    bool dirty() const { return _dirty; }
    void clearDirty() { _dirty = false; }
    const Saved& saved() const { return _d; }
    bool load(const void* blob, size_t len) {
      if (len != sizeof(Saved)) return false;
      Saved s;
      memcpy(&s, blob, sizeof s);
      if (s.version != VERSION) return false;
      _d = s;
      return true;
    }

    size_t json(char* out, size_t n) const {
      size_t k = 0;
#define APPEND(...)                                                                                          \
  do {                                                                                                       \
    if (k < n) k += snprintf(out + k, n - k, __VA_ARGS__);                                                   \
  } while (0)
      APPEND("{\"diary\":{\"today\":%lu,\"kinds\":[", (unsigned long)_d.today);
      for (int i = 0; i < KINDS; i++) APPEND("%s\"%s\"", i ? "," : "", NAMES[i]);
      APPEND("],\"days\":[");
      bool first = true;
      for (int i = 0; i < DAYS; i++) {
        const Day& d = _d.days[i];
        int total = 0, samples = 0;
        for (int j = 0; j < KINDS; j++) total += d.n[j];
        for (int h = 0; h < HOURS; h++) samples += d.moodN[h];
        if (!total && !samples) continue;
        APPEND("%s{\"id\":%lu,\"n\":[", first ? "" : ",", (unsigned long)d.id);
        first = false;
        for (int j = 0; j < KINDS; j++) APPEND("%s%u", j ? "," : "", d.n[j]);
        APPEND("],\"mood\":[");
        for (int h = 0; h < HOURS; h++) {
          const int v = moodAt(d, h);
          if (v == -128)
            APPEND("%snull", h ? "," : "");
          else
            APPEND("%s%d", h ? "," : "", v);
        }
        APPEND("]}");
      }
      APPEND("]}}");
#undef APPEND
      return k < n ? k : n;
    }

    size_t text(char* out, size_t n) const {
      size_t k = 0;
#define APPEND(...)                                                                                          \
  do {                                                                                                       \
    if (k < n) k += snprintf(out + k, n - k, __VA_ARGS__);                                                   \
  } while (0)
      for (int i = 0; i < DAYS; i++) {
        const Day& d = _d.days[i];
        int total = 0, samples = 0;
        for (int j = 0; j < KINDS; j++) total += d.n[j];
        for (int h = 0; h < HOURS; h++) samples += d.moodN[h];
        if (!total && !samples) continue;
        APPEND("%s:", i == 0 ? "today" : i == 1 ? "yesterday" : "earlier");
        for (int j = 0; j < KINDS; j++)
          if (d.n[j]) APPEND(" %s %u", NAMES[j], d.n[j]);
        if (!total) APPEND(" quiet");
        const int b = bestHour(i);
        if (b >= 0) APPEND("; best mood around %02d:00 (%+d)", b, moodAt(d, b));
        APPEND("\n");
      }
      if (!k) APPEND("(nothing yet)\n");
#undef APPEND
      return k < n ? k : n;
    }

  private:
    Saved _d{};
    bool _dirty = false;
};

inline size_t diaryLogJson(const char* csv, size_t len, char* out, size_t n) {
  size_t k = snprintf(out, n, "{\"log\":[");
  bool first = true;
  size_t at = 0;
  const bool wholeFile = len >= 3 && !strncmp(csv, "day", 3);
  if (!wholeFile) {
    while (at < len && csv[at] != '\n') at++;
    at++;
  }
  while (at < len) {
    size_t end = at;
    while (end < len && csv[end] != '\n') end++;
    if (end >= len) break;
    long v[10] = {};
    int fields = 0;
    bool hasMood = false, ok = csv[at] >= '0' && csv[at] <= '9';
    for (size_t i = at; ok && i < end && fields < 10;) {
      char* stop = nullptr;
      char num[12];
      size_t j = i;
      while (j < end && csv[j] != ',' && csv[j] != '\r') j++;
      const size_t m = j - i;
      if (m >= sizeof num)
        ok = false;
      else if (m) {
        memcpy(num, csv + i, m);
        num[m] = 0;
        v[fields] = strtol(num, &stop, 10);
        ok = *stop == 0;
        if (fields == 9) hasMood = true;
      }
      fields++;
      i = j + 1;
      if (j >= end || csv[j] == '\r') break;
    }
    if (ok && fields >= 9 && k + 48 < n) {
      const long kind = v[1] + v[3] + v[5] + v[6] + v[8], hard = v[2] + v[4] + v[7];
      if (hasMood)
        k += snprintf(out + k, n - k, "%s[%ld,%ld,%ld,%ld]", first ? "" : ",", v[0], kind, hard, v[9]);
      else
        k += snprintf(out + k, n - k, "%s[%ld,%ld,%ld,null]", first ? "" : ",", v[0], kind, hard);
      first = false;
    }
    at = end + 1;
  }
  return k + snprintf(out + k, n - k, "]}");
}
