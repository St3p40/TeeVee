#pragma once
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "emotes.h"
#include "emotion.h"
#include "style.h"

template <int N>
struct Taste {
    float score[N] = {};
    int current = -1;
    uint32_t at = 0;

    void chose(int i, uint32_t now) { current = i >= 0 && i < N ? i : -1, at = now; }
    // A reaction, r -1..1. window ms: only if it came this soon after the choice (0: for as long as the choice stands).
    void react(float r, uint32_t now, uint32_t window) {
      if (current < 0 || (window && now - at > window)) return;
      r = r < -1 ? -1 : r > 1 ? 1 : r;
      score[current] += 0.1f * (r - score[current]);
    }
    float weight(int i) const { return i >= 0 && i < N ? expf(1.1f * score[i]) : 1.f; }
    template <typename Rnd>
    int pick(int n, int except, Rnd rnd01) const {
      float sum = 0;
      for (int i = 0; i < n && i < N; i++) sum += i == except ? 0.f : weight(i);
      float x = rnd01() * sum;
      int got = -1;
      for (int i = 0; i < n && i < N; i++) {
        if (i == except) continue;
        got = i;
        if ((x -= weight(i)) <= 0) break;
      }
      return got;
    }
    void sanitize() {
      for (float& s : score) s = s >= -1 && s <= 1 ? s : 0;
    }
};

class Tastes {
  public:
    static constexpr int EYES = 8, APPS = 12, WEEK = 7 * 24;
    static constexpr uint32_t VERSION = 1;
    static constexpr uint32_t EMOTE_WINDOW_MS = 10000;
    struct Saved {  // new fields go last: a shorter, older save still loads
        uint32_t version;
        float emote[EM_COUNT], eye[EYES], week[WEEK], app[APPS];
        char appName[APPS][12];
    };

    Taste<EM_COUNT> emote;
    Taste<EYES> eye;
    Taste<APPS> app;
    float week[WEEK] = {};
    char appName[APPS][12] = {};
    float streak = 0;

    void react(float reward, uint32_t now, int day, int hour) {
      emote.react(reward, now, EMOTE_WINDOW_MS);
      eye.react(reward, now, 0);
      if (day >= 0 && day < 7 && hour >= 0 && hour < 24) {
        float& w = week[day * 24 + hour];
        w += 0.02f * ((reward < -1 ? -1 : reward > 1 ? 1 : reward) - w);
      }
    }
    float weekLike(int day, int hour) const {
      return day >= 0 && day < 7 && hour >= 0 && hour < 24 ? week[day * 24 + hour] : 0.f;
    }

    // Won or lost something (a game's face.event, or any win or fail): the streak, and how it feels about the game on the screen.
    void won(float k, bool inGame, uint32_t now) {
      streak = fminf(1.f, (streak < 0 ? 0.f : streak) + 0.3f * k);
      if (inGame) app.react(0.7f, now, 0);
    }
    void lost(float k, bool inGame, uint32_t now) {
      streak = fmaxf(-1.f, (streak > 0 ? 0.f : streak) - 0.3f * k);
      if (inGame) app.react(-0.35f, now, 0);
    }
    void update(float dt) { streak *= expf(-dt / 600.f); }

    int appSlot(const char* name) {
      int weakest = 0;
      for (int i = 0; i < APPS; i++) {
        if (!strncmp(appName[i], name, sizeof appName[i] - 1)) return i;
        if (!appName[i][0]) {
          weakest = i;
          break;
        }
        if (fabsf(app.score[i]) < fabsf(app.score[weakest])) weakest = i;
      }
      snprintf(appName[weakest], sizeof appName[weakest], "%s", name);
      app.score[weakest] = 0;
      if (app.current == weakest) app.current = -1;
      return weakest;
    }
    void gameStarted(const char* name, uint32_t now) { app.chose(appSlot(name), now); }
    void gameEnded() { app.current = -1; }
    float appScore(const char* name) const {
      for (int i = 0; i < APPS; i++)
        if (appName[i][0] && !strncmp(appName[i], name, sizeof appName[i] - 1)) return app.score[i];
      return 0;
    }
    template <typename Rnd>
    int suggest(const char* const* names, int n, Rnd rnd01) const {
      float sum = 0;
      for (int i = 0; i < n; i++) sum += expf(1.5f * appScore(names[i]));
      float x = rnd01() * sum;
      int got = -1;
      for (int i = 0; i < n; i++) {
        got = i;
        if ((x -= expf(1.5f * appScore(names[i]))) <= 0) break;
      }
      return got;
    }

    Saved save() const {
      Saved s{};
      s.version = VERSION;
      memcpy(s.emote, emote.score, sizeof s.emote);
      memcpy(s.eye, eye.score, sizeof s.eye);
      memcpy(s.week, week, sizeof s.week);
      memcpy(s.app, app.score, sizeof s.app);
      memcpy(s.appName, appName, sizeof s.appName);
      return s;
    }
    bool load(const void* blob, size_t len) {
      if (len < sizeof(uint32_t) || len > sizeof(Saved)) return false;
      Saved s{};
      memcpy(&s, blob, len);
      if (s.version != VERSION) return false;
      memcpy(emote.score, s.emote, sizeof s.emote);
      memcpy(eye.score, s.eye, sizeof s.eye);
      memcpy(app.score, s.app, sizeof s.app);
      memcpy(appName, s.appName, sizeof appName);
      for (int i = 0; i < WEEK; i++) week[i] = s.week[i] >= -1 && s.week[i] <= 1 ? s.week[i] : 0;
      emote.sanitize(), eye.sanitize(), app.sanitize();
      for (auto& n : appName) n[sizeof n - 1] = 0;
      return true;
    }

    size_t summary(char* out, size_t n) const {
      static const char* const DAYS[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
      size_t k = snprintf(out, n, "likes");
      int shown = 0;
      bool taken[EM_COUNT] = {};
      for (int pass = 0; pass < 2 && k < n; pass++) {
        int best = -1;
        for (int i = 1; i < EM_COUNT; i++)
          if (!taken[i] && emote.score[i] > 0.05f && (best < 0 || emote.score[i] > emote.score[best]))
            best = i;
        if (best < 0) break;
        taken[best] = true;
        k += snprintf(out + k, n - k, "%s %s", shown++ ? "," : "", EMOTE_NAMES[best]);
      }
      if (!shown && k < n) k += snprintf(out + k, n - k, " nothing in particular yet");
      int be = -1;
      for (int i = 0; i < EYES; i++)
        if (eye.score[i] > 0.05f && (be < 0 || eye.score[i] > eye.score[be])) be = i;
      char name[16];
      if (be >= 0 && k < n && styleEnumName(EYE_OPTIONS, be, name, sizeof name))
        k += snprintf(out + k, n - k, "; eyes %s", name);
      int bw = -1;
      for (int i = 0; i < WEEK; i++)
        if (week[i] > 0.02f && (bw < 0 || week[i] > week[bw])) bw = i;
      if (bw >= 0 && k < n) k += snprintf(out + k, n - k, "; best time %s %dh", DAYS[bw / 24], bw % 24);
      int ba = -1;
      for (int i = 0; i < APPS; i++)
        if (appName[i][0] && app.score[i] > 0.05f && (ba < 0 || app.score[i] > app.score[ba])) ba = i;
      if (ba >= 0 && k < n) k += snprintf(out + k, n - k, "; games %s", appName[ba]);
      return k < n ? k : (n ? n - 1 : 0);
    }
};
