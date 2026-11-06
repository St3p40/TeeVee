// Does it feel alive, or scripted? Runs the real desktop / web engine through simulated scenes (no window, no board, hours in seconds) and scores
// the signs of a script: clockwork timing, identical reactions, a face that stands dead still, no memory of how it was treated.
// It cannot say that the robot is charming; it can say when it is mechanical. See .claude/skills/teevee-alive/SKILL.md.
//
//   g++ -O2 -std=c++17 -DTEEVEE_MIND_N=128 -Isrc/core -Idesktop tools/dev/alive_probe.cpp desktop/engine.cpp -o alive_probe && ./alive_probe [scenes]
//   scenes: idle twins same memory emotes treatment personality learning sleep overload (treatment, learning and sleep are the slow ones)
//
// Each line: OK / WEAK / SCRIPTED, the measure, what was measured, the target. Exit 1 if anything is SCRIPTED. Nothing is saved or loaded.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>
#include "engine.h"

static int nOk = 0, nWeak = 0, nBad = 0;

static void report(const char* name, float v, float ok, float weak, bool higherIsBetter, const char* unit,
                   const char* why) {
  const bool isOk = higherIsBetter ? v >= ok : v <= ok, isWeak = higherIsBetter ? v >= weak : v <= weak;
  (isOk ? nOk : isWeak ? nWeak : nBad)++;
  printf("%-9s %-30s %8.2f %-6s (target %s %.2f)  %s\n",
         isOk     ? "OK"
         : isWeak ? "WEAK"
                  : "SCRIPTED",
         name, v, unit, higherIsBetter ? ">=" : "<=", ok, why);
}

static void noData(const char* name, const char* why) {
  printf("%-9s %-30s %8s %-6s %s\n", "-", name, "n/a", "", why);
}

static float mean(const std::vector<float>& v) {
  float s = 0;
  for (float x : v) s += x;
  return v.empty() ? 0 : s / v.size();
}
static float cv(const std::vector<float>& v) {
  if (v.size() < 3) return 0;
  const float m = mean(v);
  float s = 0;
  for (float x : v) s += (x - m) * (x - m);
  return m > 0 ? std::sqrt(s / v.size()) / m : 0;
}

static Engine* robot(uint32_t seed, uint32_t& now, int personality = 0) {
  Engine* e = new Engine;
  e->begin(seed, 0, false);
  e->setPersonality(personality);
  now = 0;
  for (int i = 0; i < 100; i++) e->update(now += 20);
  return e;
}
static void run(Engine& e, uint32_t& now, uint32_t ms) {
  for (uint32_t t = 0; t < ms; t += 20) e.update(now += 20);
}
static float jump(Engine& e, uint32_t& now, uint8_t type, float k, float PAD::* axis,
                  int* latencyMs = nullptr) {
  const float before = e.emotion().*axis;
  e.event(type, k);
  float best = 0;
  bool moved = false;
  for (int t = 20; t <= 1200; t += 20) {
    e.update(now += 20);
    const float d = e.emotion().*axis - before;
    if (std::fabs(d) > std::fabs(best)) best = d;
    if (!moved && std::fabs(d) > 0.02f) {
      moved = true;
      if (latencyMs) *latencyMs = t;
    }
  }
  return best;
}

static void idleScene() {
  std::vector<float> blinkGaps, moveGaps;
  float longestStill = 0, awakeSec = 0;
  int emotes = 0;
  std::vector<float> emoteGaps;
  for (uint32_t seed = 1; seed <= 3; seed++) {
    uint32_t now;
    Engine* e = robot(seed * 7919, now);
    FaceParams last = e->face(), ref = e->face();
    float still = 0, blinkAt = -1, moveAt = -1, emoteAt = -1;
    uint32_t lastStart = 0;
    bool closed = false;
    for (int i = 0; i < 15 * 60 * 50; i++) {
      e->update(now += 20);
      const float t = now / 1000.f;
      const FaceParams& f = e->face();
      if (strcmp(e->mind(), "awake")) {
        last = ref = f;
        still = 0;
        continue;
      }
      awakeSec += 0.02f;
      if (!closed && f.eye_open_l < 0.3f) {
        closed = true;
        if (blinkAt >= 0) blinkGaps.push_back(t - blinkAt);
        blinkAt = t;
      } else if (closed && f.eye_open_l > 0.5f) {
        closed = false;
      }
      const float gaze = std::fabs(f.pupil_x - last.pupil_x) + std::fabs(f.pupil_y - last.pupil_y);
      if (gaze > 0.02f) {
        if (moveAt >= 0 && t - moveAt > 0.15f) moveGaps.push_back(t - moveAt);
        moveAt = t;
      }
      const float moved =
          std::max({std::fabs(f.pupil_x - ref.pupil_x), std::fabs(f.pupil_y - ref.pupil_y),
                    std::fabs(f.eye_open_l - ref.eye_open_l), std::fabs(f.mouth_curve - ref.mouth_curve) / 2,
                    std::fabs(f.mouth_open - ref.mouth_open), std::fabs(f.squash - ref.squash) / 2});
      if (moved > 0.02f)
        ref = f, still = 0;
      else
        still += 0.02f;
      longestStill = std::max(longestStill, still);
      last = f;
      const Overlay& o = e->overlay();
      if (o.start != lastStart && (o.eyes || o.deco)) {
        lastStart = o.start;
        emotes++;
        if (emoteAt >= 0) emoteGaps.push_back(t - emoteAt);
        emoteAt = t;
      }
    }
    delete e;
  }
  printf("\n-- Left alone (15 min, 3 robots; %.0f s awake)\n", awakeSec);
  report("blink rhythm (irregularity)", cv(blinkGaps), 0.5f, 0.3f, true, "cv",
         "blinks like a metronome read as a machine");
  report("blinks per minute", blinkGaps.size() / std::max(1.f, awakeSec / 60), 8, 4, true, "/min",
         "too few and it stares");
  report("glances: irregularity", cv(moveGaps), 0.6f, 0.35f, true, "cv",
         "the eyes should not move on a beat");
  report("longest dead-still face", longestStill, 4, 8, false, "s", "a face that freezes looks switched off");
  report("does things by itself", emotes / std::max(1.f, awakeSec / 60), 0.1f, 0.03f, true, "/min",
         "spontaneous emotes with nobody there");
  if (emoteGaps.size() >= 4)
    report("  ...and not on a timer", cv(emoteGaps), 0.4f, 0.2f, true, "cv", "");
  else
    noData("  ...and not on a timer", "too few emotes in this time to tell");
}

static void twinsScene() {
  uint32_t n1, n2;
  Engine* a = robot(111, n1);
  Engine* b = robot(222, n2);
  double sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0;
  const int N = 5 * 60 * 50;
  for (int i = 0; i < N; i++) {
    a->update(n1 += 20);
    b->update(n2 += 20);
    const double x = a->face().pupil_x, y = b->face().pupil_x;
    sa += x, sb += y, saa += x * x, sbb += y * y, sab += x * y;
  }
  const double cov = sab / N - sa / N * sb / N, va = saa / N - sa * sa / N / N,
               vb = sbb / N - sb * sb / N / N;
  const float corr = va > 0 && vb > 0 ? (float)std::fabs(cov / std::sqrt(va * vb)) : 1;
  delete a;
  delete b;
  printf("\n-- Twins (two robots, same 5 min)\n");
  report("gazes in step", corr, 0.3f, 0.6f, false, "|r|",
         "two robots doing the same thing at the same time is a script");
}

static void sameInputScene() {
  std::vector<float> petMoves, latency;
  for (uint32_t seed = 1; seed <= 24; seed++) {
    uint32_t now;
    Engine* e = robot(seed * 104729, now);
    run(*e, now, 2000);
    int lat = 0;
    petMoves.push_back(jump(*e, now, EV_PET, 0.6f, &PAD::v, &lat));
    if (lat) latency.push_back((float)lat);
    delete e;
  }
  std::sort(latency.begin(), latency.end());
  printf("\n-- The same pet, the same moment, 24 robots\n");
  report("how much it lands: spread", cv(petMoves), 0.06f, 0.03f, true, "cv",
         "the identical reaction every time is the clearest sign of a script");
  report("reaction time: spread", latency.empty() ? 0 : latency.back() - latency.front(), 150, 60, true, "ms",
         "reacting in the same frame every time feels mechanical");
  report("reaction time: not instant", latency.empty() ? 0 : mean(latency), 120, 60, true, "ms",
         "it should take a moment to notice");
  report("reaction time: not slow", latency.empty() ? 9999 : latency.back(), 700, 1100, false, "ms",
         "an awake robot should not lag behind you");
}

static void memoryScene() {
  printf("\n-- It remembers what just happened\n");
  std::vector<float> ratio, rested;
  for (uint32_t seed = 1; seed <= 8; seed++) {
    uint32_t now;
    Engine* e = robot(seed * 31337, now);
    run(*e, now, 3000);
    const float first = std::fabs(jump(*e, now, EV_STARTLE, 1, &PAD::a));
    float last = first;
    for (int i = 0; i < 6; i++) {
      run(*e, now,
          30000);  // long enough for the arousal to settle (a full feeling cannot jump further), not for the whole habit to fade
      last = std::fabs(jump(*e, now, EV_STARTLE, 1, &PAD::a));
    }
    run(*e, now, 5 * 60 * 1000);
    rested.push_back(std::fabs(jump(*e, now, EV_STARTLE, 1, &PAD::a)) / std::max(0.01f, first));
    ratio.push_back(last / std::max(0.01f, first));
    delete e;
  }
  report("startled the 7th time", mean(ratio), 0.6f, 0.85f, false, "x 1st", "it should get used to things");
  report("startled again after a rest", mean(rested), 0.75f, 0.5f, true, "x 1st",
         "and get over getting used to them");

  std::vector<float> early, teased;
  for (uint32_t seed = 1; seed <= 8; seed++) {
    uint32_t now;
    Engine* e = robot(seed * 271828, now);
    run(*e, now, 3000);
    for (int i = 0; i < 2; i++) {
      e->event(EV_POKE, 0.6f);
      run(*e, now, 1500);
    }
    early.push_back(e->emotion().d);
    for (int i = 0; i < 6; i++) {
      e->event(EV_POKE, 0.6f);
      run(*e, now, 1500);
    }
    teased.push_back(e->emotion().d);
    delete e;
  }
  report("teasing turns cross", mean(teased) - mean(early), 0.15f, 0.05f, true, "d",
         "8 pokes should not feel like 2 more of the same");

  std::vector<float> fresh, wary;
  for (uint32_t seed = 1; seed <= 8; seed++) {
    uint32_t now;
    Engine* a = robot(seed * 65537, now);
    run(*a, now, 3000);
    a->event(EV_FAIL, 0.7f);
    run(*a, now, 15000);
    fresh.push_back(jump(*a, now, EV_PET, 0.6f, &PAD::v));
    delete a;
    Engine* b = robot(seed * 65537, now);
    run(*b, now, 3000);
    for (int i = 0; i < 3; i++) {
      b->event(EV_SCOLD, 1);
      run(*b, now, 5000);
    }
    wary.push_back(jump(*b, now, EV_PET, 0.6f, &PAD::v));
    delete b;
  }
  report("a pet after scolding", mean(wary) / std::max(0.01f, mean(fresh)), 0.8f, 0.95f, false, "x kind",
         "it should be wary of who just scolded it");
}

static void emoteScene() {
  std::map<int, int> counts;
  int shown = 0, tried = 0, longestRun = 0;
  for (uint32_t seed = 1; seed <= 4; seed++) {
    uint32_t now;
    Engine* e = robot(seed * 15485863, now);
    int lastKind = -1, runLen = 0;
    uint32_t lastStart = e->overlay().start;
    for (int i = 0; i < 40; i++) {
      e->event(EV_PET, 0.8f);
      tried++;
      run(*e, now, 900);
      const Overlay& o = e->overlay();
      if (o.start != lastStart && (o.eyes || o.deco)) {
        lastStart = o.start;
        const int kind = e->emote();
        counts[kind]++;
        shown++;
        runLen = kind == lastKind ? runLen + 1 : 1;
        lastKind = kind;
        longestRun = std::max(longestRun, runLen);
      }
      run(*e, now, 7500);
    }
    delete e;
  }
  printf("\n-- Being petted now and then (160 pets, 8 s apart)\n");
  report("different emotes", (float)counts.size(), 3, 2, true, "kinds",
         "always the same emote is a lookup table");
  report("longest same emote in a row", (float)longestRun, 3, 5, false, "times", "");
  report("pets with an emote", tried ? (float)shown / tried : 0, 0.15f, 0.05f, true, "share",
         "it should show it sometimes...");
  report("pets without one", tried ? 1 - (float)shown / tried : 0, 0.2f, 0.05f, true, "share",
         "...but not perform on every touch");
}

static void treatmentScene() {
  std::vector<float> cared, ignored;
  for (uint32_t seed = 1; seed <= 2; seed++) {
    uint32_t now;
    Engine* a = robot(seed * 49979687, now);
    Engine* b = robot(seed * 49979687, now);
    uint32_t na = now, nb = now;
    float sa = 0, sb = 0;
    int n = 0;
    const int MIN = 120;
    for (int m = 0; m < MIN; m++) {
      if (m % 5 == 0) a->event(EV_PET, 0.7f);
      for (int i = 0; i < 60 * 50; i++) {
        a->update(na += 20);
        b->update(nb += 20);
      }
      if (m >= MIN - 30) {
        sa += a->emotion().v;
        sb += b->emotion().v;
        n++;
      }
    }
    cared.push_back(sa / n);
    ignored.push_back(sb / n);
    delete a;
    delete b;
  }
  printf("\n-- Two hours: petted every 5 min, or ignored\n");
  report("mood: cared for - ignored", mean(cared) - mean(ignored), 0.15f, 0.05f, true, "v",
         "how it is treated should show");
}

static void personalityScene() {
  float lo = 9, hi = -9;
  for (int p = 0; p < 5; p++) {
    uint32_t now;
    Engine* e = robot(424242, now, p);
    run(*e, now, 3000);
    const float j = jump(*e, now, EV_POKE, 0.8f, &PAD::a);
    lo = std::min(lo, j);
    hi = std::max(hi, j);
    delete e;
  }
  printf("\n-- Five personalities, the same poke\n");
  report("how differently they take it", hi - lo, 0.1f, 0.03f, true, "a", "a personality should show");
}

static void sleepScene() {
  int emotes = 0, bubbles = 0;
  float sleptSec = 0, maxDrive = 0, peeks = 0, wokenBubble = 0;
  for (uint32_t seed = 1; seed <= 3; seed++) {
    uint32_t now;
    Engine* e = robot(seed * 6700417, now);
    e->toggleSleep();
    run(*e, now, 6000);
    uint32_t last = e->overlay().start;
    bool open = false;
    for (int i = 0; i < 40 * 60 * 50; i++) {
      e->update(now += 20);
      if (strcmp(e->mind(), "asleep")) continue;
      sleptSec += 0.02f;
      if (e->emote() != 0 && e->overlay().start != last) {
        last = e->overlay().start;
        if (e->emote() == EM_BUBBLE)
          bubbles++;
        else
          emotes++;
      }
      maxDrive = std::max(maxDrive, e->glitchDrive());
      const bool o = e->face().eye_open_l > 0.2f;
      if (o && !open) peeks++;
      open = o;
    }
    for (int i = 0; i < 20 * 50 && e->emote() != EM_BUBBLE; i++) e->update(now += 20);
    e->event(EV_POKE, 1);
    float gone = -1;
    for (int t = 0; t < 12000 && gone < 0; t += 20) {
      e->update(now += 20);
      if (e->emote() != EM_BUBBLE) gone = t;
    }
    wokenBubble = std::max(wokenBubble, gone < 0 ? 12000 : gone);
    delete e;
  }
  printf("\n-- Asleep (40 min, 3 robots; %.0f s asleep)\n", sleptSec);
  report("nose bubbles per minute", bubbles / std::max(1.f, sleptSec / 60), 2, 1, true, "/min",
         "its sleep has a rhythm");
  report("bubble gone after waking", wokenBubble, 1500, 3000, false, "ms",
         "no sleepy face left on an awake robot");
  report("other emotes while asleep", (float)emotes, 0, 0, false, "", "none at all");
  report("eyes peeking now and then", peeks, 2, 1, true, "times", "the one thing it still does");
  report("glitch wish while asleep", maxDrive, 0, 0, false, "", "only rare tiny shifts");
}
static void overloadScene() {
  std::vector<float> alone, storm, after;
  for (uint32_t seed = 1; seed <= 4; seed++) {
    uint32_t now;
    Engine* e = robot(seed * 39916801, now);
    run(*e, now, 30000);
    float d = 0;
    for (int i = 0; i < 1500; i++) {
      e->update(now += 20);
      d = std::max(d, e->glitchDrive());
    }
    alone.push_back(d);
    float top = 0;
    for (int i = 0; i < 16; i++) {
      e->event(i % 2 ? EV_POKE : EV_STARTLE, 0.8f);
      for (int k = 0; k < 25; k++) {
        e->update(now += 20);
        top = std::max(top, e->glitchDrive());
      }
    }
    storm.push_back(top);
    run(*e, now, 45000);
    after.push_back(e->glitchDrive());
    delete e;
  }
  printf("\n-- The picture: alone, in a storm of events, after it\n");
  report("glitch when nothing happens", mean(alone), 0.1f, 0.3f, false, "drive", "clean, or a tiny shift");
  report("glitch in a storm", mean(storm), 0.6f, 0.3f, true, "drive", "overload");
  report("calms down afterwards", mean(after), 0.15f, 0.4f, false, "drive", "it passes");
}

static MindNet s_net, s_look;
static float smileUrge() {
  s_look = s_net;
  s_look.setRates(0.05f, 0);
  MindInputs c;
  c.v = 0.1f, c.mood = 0.1f, c.hour = 14, c.sinceTouch = 300, c.sinceEvent = 300, c.idleSec = 20,
  c.openness = 0.4f;
  double sum = 0;
  for (int t = 0; t < 1500; t++) {
    s_look.step(c, 0.1f);
    sum += s_look.raw(mind::U_MIRTH);
  }
  return (float)(sum / 1500);
}
static void teach(int sign, int reactions, uint32_t& rng) {
  MindInputs in;
  in.v = 0.1f, in.mood = 0.1f, in.hour = 14, in.sinceTouch = 300, in.sinceEvent = 300, in.idleSec = 20,
  in.openness = 0.4f;
  int got = 0;
  for (long t = 0; got < reactions && t < 2000000; t++) {
    s_net.step(in, 0.1f);
    rng = rng * 1664525u + 1013904223u;
    const float s = s_net.urge(mind::U_MIRTH) * sign, x = (rng >> 8) / 16777216.f;
    if (s > 0.15f && x < 0.01f)
      s_net.reward(0.8f), got++;
    else if (s < -0.05f && x < 0.01f)
      s_net.reward(-0.5f), got++;
  }
}
static void learningScene() {
  std::vector<float> early, wobble, change;
  for (int seed = 1; seed <= 3; seed++) {
    uint32_t rng = seed * 12345u;
    s_net.init(seed * 991);
    s_net.setRates(0.05f, 0.15f);
    teach(1, 25, rng);
    const float likes = smileUrge();
    s_net.init(seed * 991);
    s_net.setRates(0.05f, 0.15f);
    teach(-1, 25, rng);
    early.push_back(likes - smileUrge());

    s_net.init(seed * 991 + 1);
    s_net.setRates(0.05f, 0.15f);
    teach(1, 300, rng);
    std::vector<float> at;
    for (int i = 0; i < 6; i++) {
      teach(1, 50, rng);
      at.push_back(smileUrge());
    }
    const float m = mean(at);
    float s = 0;
    for (float x : at) s += (x - m) * (x - m);
    wobble.push_back(std::sqrt(s / at.size()));
    teach(-1, 150, rng);
    change.push_back(m - smileUrge());
  }
  printf("\n-- Learning (two owners with opposite tastes)\n");
  report("habit after 25 reactions", mean(early), 0.3f, 0.1f, true, "gap",
         "how it is treated should show soon");
  report("not flipped by 25 alone", mean(early), 1.4f, 1.8f, false, "gap",
         "a first impression, not a new character");
  report("settled: wobble when old", mean(wobble), 0.1f, 0.2f, false, "sd",
         "an old habit should not swing with one owner's same treatment");
  report("an old robot can change", mean(change), 0.3f, 0.1f, true, "urge",
         "after 150 reactions the other way");
}

static void dayScene() {
  auto antics = [](int kind, int& warm, int& cold, int& other) {
    warm = cold = other = 0;
    for (uint32_t seed = 1; seed <= 12; seed++) {
      uint32_t now;
      Engine* e = robot(seed * 15485863, now);
      for (int i = 0; kind && i < 30; i++) {
        e->event(kind > 0 ? (i % 3 ? EV_PET : EV_PRAISE) : (i % 3 ? EV_ANNOY : EV_SCOLD), 0.7f);
        run(*e, now, 10000);
      }
      int last = 0;
      for (int i = 0; i < 4 * 60 * 50; i++) {
        e->update(now += 20);
        const int b = e->idleBehaviour();
        if (b && b != last) {
          if (b == IdleGen::B_SMILE || b == IdleGen::B_WINK)
            warm++;
          else if (b == IdleGen::B_SQUINT || b == IdleGen::B_EYEROLL)
            cold++;
          else if (b == IdleGen::B_LOOK_AROUND || b == IdleGen::B_CURIOUS)
            other++;
        }
        last = b;
      }
      delete e;
    }
  };
  int kw, kc, ko, hw, hc, ho, nw, nc, no;
  antics(1, kw, kc, ko);
  antics(-1, hw, hc, ho);
  antics(0, nw, nc, no);
  printf("\n-- The day it has had, then left alone (12 robots; kind %d/%d, hard %d/%d warm/cold antics)\n",
         kw, kc, hw, hc);
  if (kw + kc < 4 || hw + hc < 4) {
    printf("n/a       the day's antics (too few to tell)\n");
    return;
  }
  report("after a kind day: warm antics", kw / (float)(kw + kc), 0.8f, 0.6f, true, "share",
         "a good day should show when it is alone");
  report("after a hard day: cold antics", hc / (float)(hw + hc), 0.8f, 0.6f, true, "share",
         "and so should a bad one");
  report("an ordinary moment: none", (float)(nw + nc + no), 2.f, 6.f, false, "acts",
         "no day, no mood to act out");
}

int main(int argc, char** argv) {
  printf("TeeVee: alive or scripted? (simulated; the clock's hour of day has a small effect)\n");
  const struct {
      const char* name;
      void (*fn)();
  } scenes[] = {{"idle", idleScene},
                {"twins", twinsScene},
                {"same", sameInputScene},
                {"memory", memoryScene},
                {"emotes", emoteScene},
                {"treatment", treatmentScene},
                {"personality", personalityScene},
                {"learning", learningScene},
                {"sleep", sleepScene},
                {"day", dayScene},
                {"overload", overloadScene}};
  for (const auto& s : scenes) {
    bool on = argc < 2;
    for (int i = 1; i < argc; i++) on = on || !strcmp(argv[i], s.name);
    if (on) s.fn();
  }
  printf("\n%d ok, %d weak, %d scripted\n", nOk, nWeak, nBad);
  return nBad ? 1 : 0;
}
