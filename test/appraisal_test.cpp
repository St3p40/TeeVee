#include <cstdio>
#include <vector>
#include "appraisal.h"
static int fails = 0;
#define CHECK(c)                                                                                             \
  do {                                                                                                       \
    if (!(c)) {                                                                                              \
      printf("FAIL line %d: %s\n", __LINE__, #c);                                                            \
      fails++;                                                                                               \
    }                                                                                                        \
  } while (0)

static const PAD CALM = {0.1f, 0.f, 0.f};

// Hears the event, waits until it is due; returns the reaction and the delay.
static EmotionEvent once(Appraisal& a, uint8_t type, float k, uint32_t& now, const PAD& feel = CALM,
                         float sleepy = 0, uint32_t* delay = nullptr) {
  a.hear({type, k}, feel, sleepy, now);
  EmotionEvent out{EV_COUNT, 0};
  for (uint32_t t = 0; t < 2000; t += 10) {
    if (a.pop(now + t, out)) {
      if (delay) *delay = t;
      now += t;
      return out;
    }
  }
  CHECK(false);  // it never came
  return out;
}

int main() {
  {
    Appraisal a;
    a.begin(1);
    uint32_t now = 1000;
    float last = 0;
    for (int i = 0; i < 7; i++) last = once(a, EV_STARTLE, 1, now).intensity;
    CHECK(last > 0.8f);
    CHECK(a.habit(EV_STARTLE) > 5);
    for (int i = 0; i < 300; i++) a.update(1);
    CHECK(a.habit(EV_STARTLE) < 0.1f);
  }
  {
    Appraisal a;
    a.begin(3);
    uint32_t now = 1000;
    const PAD happy = {0.5f, 0.1f, 0};
    const EmotionEvent p1 = once(a, EV_POKE, 0.6f, now, happy);
    CHECK(p1.type == EV_POKE && p1.intensity < 0.5f);
    once(a, EV_POKE, 0.6f, now);
    once(a, EV_POKE, 0.6f, now);
    const EmotionEvent p4 = once(a, EV_POKE, 0.6f, now);
    CHECK(p4.type == EV_ANNOY);
    EmotionEvent p9{};
    for (int i = 0; i < 5; i++) p9 = once(a, EV_POKE, 0.6f, now);
    printf("  poke: first %s %.2f, 4th %s %.2f, 9th %s %.2f\n", EVENTS[p1.type].name, p1.intensity,
           EVENTS[p4.type].name, p4.intensity, EVENTS[p9.type].name, p9.intensity);
    CHECK(p9.type == EV_ANNOY && p9.intensity > p4.intensity);
  }
  {
    Appraisal fresh;
    fresh.begin(4);
    uint32_t now = 1000;
    const float friendly = once(fresh, EV_PET, 0.6f, now).intensity;
    Appraisal hurt;
    hurt.begin(4);
    for (int i = 0; i < 4; i++) once(hurt, EV_SCOLD, 1, now);
    for (int i = 0; i < 120; i++) hurt.update(1);
    const float wary = once(hurt, EV_PET, 0.6f, now).intensity;
    printf("  pet: trust 0 -> %.2f; after scolds (trust %.2f) -> %.2f\n", friendly, hurt.trust(), wary);
    CHECK(hurt.trust() < -0.5f && wary < friendly * 0.8f);
    Appraisal close;
    close.begin(5);
    for (int i = 0; i < 20; i++) once(close, EV_PRAISE, 1, now);
    for (int i = 0; i < 200; i++) close.update(1);
    Appraisal stranger;
    stranger.begin(5);
    float a = 0, b = 0;
    for (int i = 0; i < 1; i++)
      a = once(close, EV_SCOLD, 0.7f, now).intensity, b = once(stranger, EV_SCOLD, 0.7f, now).intensity;
    printf("  scold: stranger %.2f, trusted (%.2f) %.2f\n", b, close.trust(), a);
    CHECK(close.trust() > 0.4f && a > b);
  }
  {  // timing: varied; a startle before a pet; sleepy is slower; never long
    Appraisal a;
    a.begin(6);
    uint32_t now = 1000, lo = 99999, hi = 0, d = 0;
    for (int i = 0; i < 100; i++) {
      once(a, EV_GREET, 0.5f, now, CALM, 0, &d);
      if (d < lo) lo = d;
      if (d > hi) hi = d;
    }
    printf("  delay: greet %u..%u ms", lo, hi);
    CHECK(hi - lo > 150 && lo > 100 && hi <= 460);
    uint32_t st = 0, sleepy = 0, sum = 0;
    for (int i = 0; i < 30; i++) {
      once(a, EV_STARTLE, 1, now, CALM, 0, &d);
      if (d > st) st = d;
    }
    for (int i = 0; i < 30; i++) {
      once(a, EV_GREET, 0.5f, now, CALM, 1, &d);
      sum += d;
      if (d > sleepy) sleepy = d;
    }
    printf(", startle up to %u ms, sleepy greet up to %u ms\n", st, sleepy);
    CHECK(st <= 100 && sum / 30 > 400 && sleepy <= 900);
    a.hear({EV_GREET, 0.5f}, CALM, 1, now);
    a.hear({EV_STARTLE, 1}, CALM, 0, now + 10);
    EmotionEvent e;
    uint32_t t = now;
    while (!a.pop(t, e)) t += 5;
    CHECK(e.type == EV_STARTLE);
  }
  {
    Appraisal a;
    a.begin(7);
    int got = 0;
    for (int i = 0; i < 40; i++) a.hear({EV_PET, 0.3f}, CALM, 0, 1000);
    EmotionEvent e;
    for (uint32_t t = 1000; t < 3000; t += 5)
      while (a.pop(t, e)) got++;
    CHECK(got >= 8 && got <= 40);
  }
  {  // emotes: variety, not the same every time, often none for a pet; never one it does not have
    Appraisal a;
    a.begin(8);
    const PAD glad = {0.9f, 0.2f, 0.1f};
    int counts[EM_COUNT] = {};
    int same = 0, last = -1;
    for (int i = 0; i < 300; i++) {
      const uint8_t e = a.emoteFor(EV_PET, glad);
      CHECK(e < EM_COUNT);
      counts[e]++;
      if (e != EM_NONE && e == last) same++;
      if (e != EM_NONE) last = e;
    }
    printf("  pet emotes: love %d heart %d music %d none %d; same twice running %d\n", counts[EM_LOVE],
           counts[EM_HEART], counts[EM_MUSIC], counts[EM_NONE], same);
    CHECK(counts[EM_LOVE] > 20 && counts[EM_HEART] > 20 && counts[EM_NONE] > 20 && same < 30);
    int win[EM_COUNT] = {};
    for (int i = 0; i < 100; i++) win[a.emoteFor(EV_WIN, glad)]++;
    CHECK(win[EM_STAR] > 5 && win[EM_LAUGH] > 5 && win[EM_NONE] == 0);
    for (int ev = 0; ev < EV_COUNT; ev++)
      for (int i = 0; i < 50; i++) CHECK(a.emoteFor((uint8_t)ev, CALM) < EM_COUNT);
    CHECK(a.emoteFor(EV_BORE, CALM) == EM_NONE);
  }
  {  // trust from a save: clamped, NaN refused; it lasts for hours, not minutes
    Appraisal a;
    a.begin(10);
    a.setTrust(-0.6f);
    CHECK(a.trust() == -0.6f);
    a.setTrust(5);
    CHECK(a.trust() == 1);
    a.setTrust(NAN);
    CHECK(a.trust() == 0);
    a.setTrust(-0.6f);
    for (int i = 0; i < 3600; i++) a.update(1);
    CHECK(a.trust() < -0.45f);
  }
  {
    Appraisal a;
    a.begin(11);
    const auto asleep = a.glitchFor(1, true, 0.9f, 1);
    CHECK(asleep.drive == 0 && asleep.scale < 0.3f);
    const auto alone = a.glitchFor(0.9f, false, 0, 0);
    CHECK(alone.drive == 0 && alone.scale < 0.3f);
    for (int i = 0; i < 4; i++) a.hear({EV_PET, 0.4f}, CALM, 0, 1000 + i * 2000);
    const auto petted = a.glitchFor(0, false, 0, 0);
    CHECK(petted.drive < 0.1f);
    for (int i = 0; i < 12; i++) {
      a.hear({EV_POKE, 0.7f}, CALM, 0, 9000 + i * 500);
      a.update(0.5f);
    }
    const auto storm = a.glitchFor(0, false, 0.5f, 0);
    printf(
        "  glitch: asleep %.2f / %.2f, alone %.2f / %.2f, a storm %.2f / %.2f (scale / drive), load %.2f\n",
        asleep.scale, asleep.drive, alone.scale, alone.drive, storm.scale, storm.drive, a.load());
    CHECK(storm.drive > 0.5f && storm.scale > 1);
    for (int i = 0; i < 60; i++) a.update(1);
    const auto after = a.glitchFor(0, false, 0, 0);
    CHECK(after.drive == 0 && after.scale < 0.3f);
  }
  {
    Appraisal a;
    a.begin(9);
    int yes = 0;
    for (int i = 0; i < 100; i++) {
      float gx, gy;
      a.touched(0.5f, -0.2f, 1000);
      CHECK(!a.glance(1500, gx, gy));
      if (a.glance(9000, gx, gy)) {
        yes++;
        CHECK(gx == 0.5f && gy == -0.2f);
        CHECK(!a.glance(9001, gx, gy));
      }
    }
    printf("  glance back: %d of 100 touches\n", yes);
    CHECK(yes > 15 && yes < 70);
  }
  {
    const auto looks = [](float trust, int& most) {
      Appraisal a;
      a.begin(21);
      int total = 0;
      most = 0;
      for (int i = 0; i < 200; i++) {
        a.setTrust(trust);
        a.touched(0.3f, 0.1f, 1000);
        float gx, gy;
        int n = 0;
        for (uint32_t t = 1000; t < 200000; t += 100) n += a.glance(t, gx, gy);
        total += n;
        most = n > most ? n : most;
      }
      return total;
    };
    int mostCalm, mostWary;
    const int calm = looks(0.3f, mostCalm), wary = looks(-0.8f, mostWary);
    printf("  looks back per 200 touches: calm %d (most %d), wary %d (most %d)\n", calm, mostCalm, wary,
           mostWary);
    CHECK(mostCalm >= 2);
    CHECK(mostCalm <= 12);
    CHECK(wary > calm * 1.2f);
    Appraisal a;
    a.begin(4);
    a.touched(0.2f, 0.2f, 1000);
    a.touched(-0.7f, 0.4f, 1000);
    float gx = 0, gy = 0;
    for (uint32_t t = 1000; t < 40000 && gx == 0; t += 100) a.glance(t, gx, gy);
    CHECK(gx == 0 || gx == -0.7f);
  }
  if (fails) {
    printf("appraisal_test: %d FAILED\n", fails);
    return 1;
  }
  printf("appraisal_test: ok\n");
  return 0;
}
