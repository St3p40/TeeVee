#include <cmath>
#include <cstdio>
#include <initializer_list>
#include "head.h"

static int fails = 0;
#define CHECK(c)                                                                                             \
  do {                                                                                                       \
    if (!(c)) {                                                                                              \
      printf("FAIL line %d: %s\n", __LINE__, #c);                                                            \
      fails++;                                                                                               \
    }                                                                                                        \
  } while (0)

static const float DT = 0.1f;

// Lean it to the right and hold: the furthest the head goes, and where it is after `secs`.
static float lean(HeadLife& h, HeadLife::In in, float secs, float* last = nullptr, float* after100ms = nullptr) {
  float peak = 0;
  in.sensor = true;
  in.tiltX = 1;
  for (int i = 0; i < (int)(secs / DT); i++) {
    const HeadLife::Out o = h.step(in, DT);
    if (i == 0 && after100ms) *after100ms = o.turnY;
    if (o.turnY > peak) peak = o.turnY;
    if (last) *last = o.turnY;
  }
  return peak;
}
static void quiet(HeadLife& h, float secs, HeadLife::In in = HeadLife::In()) {
  in.sensor = true;
  for (int i = 0; i < (int)(secs / DT); i++) h.step(in, DT);
}

int main() {
  // Nothing happens: the head is exactly level (the turned face is slow to draw on a board).
  {
    HeadLife h;
    h.begin(1);
    HeadLife::In in;
    in.sensor = true;
    for (int i = 0; i < 600; i++) {
      const HeadLife::Out o = h.step(in, DT);
      CHECK(o.turnX == 0 && o.turnY == 0 && o.roll == 0 && o.lens == 0);
    }
  }

  // Leaned: the head follows, a moment later, and comes back to exactly level when the lean is over.
  {
    HeadLife h;
    h.begin(2);
    float last = 0, first = 0;
    const float peak = lean(h, HeadLife::In(), 3, &last, &first);
    CHECK(first < 0.5f * last);  // not in the same step
    CHECK(last > 0.45f && last <= 1.f && peak <= 1.f);
    HeadLife::In in;
    in.sensor = true;
    HeadLife::Out o = {};
    for (int i = 0; i < 50; i++) o = h.step(in, DT);
    CHECK(o.turnY == 0 && o.turnX == 0 && o.lens == 0);
  }

  // Mood: wound up it swings past where it settles; calm it does not; sleepy it leans less.
  {
    HeadLife a, c, s;
    a.begin(3), c.begin(3), s.begin(3);
    HeadLife::In ia, ic, is;
    ia.arousal = 1;
    ic.arousal = -0.5f;
    is.sleepy = 1;
    float la = 0, lc = 0, ls = 0;
    const float pa = lean(a, ia, 4, &la), pc = lean(c, ic, 4, &lc);
    lean(s, is, 4, &ls);
    CHECK(pa > la * 1.05f);
    CHECK(pc < lc * 1.02f);
    CHECK(la > lc);
    CHECK(ls < 0.7f * lc);
  }

  // Boredom: turned to and fro for a while it follows less; left alone it is curious again.
  {
    HeadLife h;
    h.begin(4);
    float fresh = 0, tired = 0, again = 0;
    lean(h, HeadLife::In(), 2, &fresh);
    for (int i = 0; i < 8; i++) {
      quiet(h, 1);
      lean(h, HeadLife::In(), 1, &tired);
    }
    CHECK(h.bored() > 0.5f);
    CHECK(tired < 0.75f * fresh);
    quiet(h, 120);
    lean(h, HeadLife::In(), 2, &again);
    CHECK(again > 0.85f * fresh * 0.74f);  // (each lean has its own strength, 0.85..1.15: the worst case against the best)
    CHECK(again > tired);
  }

  // Chance: two leans, far apart, are not the same; two robots are not the same.
  {
    HeadLife h, g;
    h.begin(5), g.begin(6);
    float l[6], other = 0;
    bool differ = false;
    for (int i = 0; i < 6; i++) {
      lean(h, HeadLife::In(), 3, &l[i]);
      quiet(h, 200);
      if (i && fabsf(l[i] - l[0]) > 0.01f) differ = true;
    }
    lean(g, HeadLife::In(), 3, &other);
    CHECK(differ);
    CHECK(fabsf(other - l[0]) > 0.001f);
  }

  // Carried: the sensor's part lets go; its own part stays.
  {
    HeadLife h;
    h.begin(7);
    HeadLife::In in;
    in.sensor = true, in.carried = true, in.tiltX = 1;
    HeadLife::Out o = {};
    for (int i = 0; i < 50; i++) o = h.step(in, DT);
    CHECK(o.turnY == 0);
    in.headX = 0.9f;
    for (int i = 0; i < 100; i++) o = h.step(in, DT);
    CHECK(o.turnY > 0.3f);
  }

  // Its own turns, with no sensor at all: the head urge turns it, slowly; a small urge does nothing; asleep it goes back.
  {
    HeadLife h;
    h.begin(8);
    HeadLife::In in;
    in.headX = 0.1f, in.gazeX = 0.2f;
    HeadLife::Out o = {};
    for (int i = 0; i < 100; i++) o = h.step(in, DT);
    CHECK(o.turnY == 0);
    in.headX = -0.9f;
    o = h.step(in, DT);
    CHECK(fabsf(o.turnY) < 0.1f);
    for (int i = 0; i < 100; i++) o = h.step(in, DT);
    CHECK(o.turnY < -0.3f && o.turnY >= -0.6f);
    in.asleep = true;
    for (int i = 0; i < 100; i++) o = h.step(in, DT);
    CHECK(o.turnY == 0);
  }

  // The sensor and the mind net add up, and never pass the owner's angle.
  {
    HeadLife h, only;
    h.begin(9), only.begin(9);
    HeadLife::In in;
    in.headX = 0.9f;
    float both = 0, alone = 0;
    lean(h, in, 6, &both);
    lean(only, HeadLife::In(), 6, &alone);
    CHECK(both > alone + 0.1f || both == 1.f);
    CHECK(both <= 1.f);
  }

  // Curiosity tips the head to one side, and not always the same side.
  {
    HeadLife h;
    h.begin(10);
    int left = 0, right = 0;
    for (int n = 0; n < 40; n++) {
      HeadLife::In in;
      for (int i = 0; i < 30; i++) h.step(in, DT);
      in.curious = 0.8f;
      HeadLife::Out o = {};
      for (int i = 0; i < 40; i++) o = h.step(in, DT);
      CHECK(fabsf(o.roll) > 0.1f && fabsf(o.roll) <= 0.5f);
      (o.roll < 0 ? left : right)++;
    }
    CHECK(left > 5 && right > 5);
  }

  // Asleep and tilted: it lolls over, less far and more slowly than awake, with no swing back.
  {
    HeadLife h, w;
    h.begin(11), w.begin(11);
    HeadLife::In in;
    in.asleep = true;
    float last = 0, first = 0, wlast = 0, wfirst = 0;
    const float peak = lean(h, in, 5, &last, &first);
    lean(w, HeadLife::In(), 5, &wlast, &wfirst);
    CHECK(last > 0.15f && last < wlast);
    CHECK(first < wfirst);
    CHECK(peak < last * 1.01f);
  }

  // A long or broken time step does no harm.
  {
    HeadLife h;
    h.begin(12);
    HeadLife::In in;
    in.sensor = true, in.tiltX = 1, in.tiltY = -1, in.arousal = 1;
    for (float dt : {0.f, -1.f, 5.f, 0.5f, 0.001f, NAN}) {
      const HeadLife::Out o = h.step(in, dt);
      CHECK(o.turnX >= -1 && o.turnX <= 1 && o.turnY >= -1 && o.turnY <= 1);
    }
  }

  if (fails) return 1;
  printf("head_test: ok\n");
  return 0;
}
