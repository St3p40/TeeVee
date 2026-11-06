#include <cstdio>
#include <utility>
#include "stimuli.h"
#include "touchzone.h"
static int fails = 0;
#define CHECK(c)                                                                                             \
  do {                                                                                                       \
    if (!(c)) {                                                                                              \
      printf("FAIL line %d: %s\n", __LINE__, #c);                                                            \
      fails++;                                                                                               \
    }                                                                                                        \
  } while (0)

static int drag(SwipeDetector& d, int x0, int y0, int x1, int y1, uint32_t ms) {
  uint32_t t = 1000;
  d.update(true, x0, y0, t);
  for (int i = 1; i <= 10; i++)
    d.update(true, x0 + (x1 - x0) * i / 10, y0 + (y1 - y0) * i / 10, t + ms * i / 10);
  return d.update(false, 0, 0, t + ms + 20);
}

int main() {
  SwipeDetector d;
  CHECK(drag(d, 250, 120, 80, 130, 300) == -1);
  CHECK(drag(d, 60, 100, 260, 110, 400) == 1);
  CHECK(drag(d, 100, 120, 150, 120, 200) == 0);
  CHECK(drag(d, 250, 40, 100, 200, 300) == 0);
  CHECK(drag(d, 250, 120, 80, 120, 1200) == 0);
  CHECK(drag(d, 160, 120, 160, 120, 100) == 0);
  CHECK(d.update(false, 0, 0, 5000) == 0);
  {
    FaceStyle st = styleInitial();
    CHECK(zoneAt(st, 104, 91) == Z_EYE_L && zoneAt(st, 216, 91) == Z_EYE_R);
    CHECK(zoneAt(st, 160, 30) == Z_FOREHEAD && zoneAt(st, 160, 190) == Z_MOUTH);
    CHECK(zoneAt(st, 90, 150) == Z_CHEEK_L && zoneAt(st, 235, 150) == Z_CHEEK_R);
    CHECK(zoneAt(st, 160, 130) == Z_NONE);
    auto tap = [&](int x, int y) {
      TouchGestures g;
      StimulusOut so;
      uint32_t now = 1000;
      for (int i = 0; i < 6; i++) g.update(true, x, y, now += 20, so);
      g.update(false, 0, 0, now += 20, so);
      g.update(false, 0, 0, now += 20, so);
      return std::make_pair(so, zoneAt(st, g.startX(), g.startY()));
    };
    auto r = tap(104, 91);
    ZoneFx fx = applyZone(r.second, r.first);
    CHECK(r.first.nEvents == 1 && r.first.events[0].type == EV_POKE && r.first.events[0].intensity > 0.85f &&
          fx.flinch == -1);
    r = tap(216, 91);
    fx = applyZone(r.second, r.first);
    CHECK(fx.flinch == 1);
    r = tap(160, 192);
    fx = applyZone(r.second, r.first);
    CHECK(r.first.events[0].type == EV_GREET && fx.emote == EM_LAUGH && fx.flinch == 0);
    r = tap(90, 150);
    fx = applyZone(r.second, r.first);
    CHECK(r.first.events[0].type == EV_PET && fx.emote == EM_SHY);
    r = tap(160, 30);
    fx = applyZone(r.second, r.first);
    CHECK(r.first.events[0].type == EV_POKE && fx.emote == EM_NONE && fx.flinch == 0);
    StimulusOut so;
    so.add(EV_PET, 0.5f);
    fx = applyZone(Z_FOREHEAD, so);
    CHECK(so.events[0].intensity > 0.75f);
    StimulusOut so2;
    so2.add(EV_PET, 0.5f);
    fx = applyZone(Z_EYE_L, so2);
    CHECK(so2.events[0].type == EV_POKE && fx.flinch == -1);
  }
  HoldDetector h;
  uint32_t t = 0;
  bool fired = false;
  for (int i = 0; i < 30; i++) {
    fired |= h.update(true, 100, 100, t += 100, 2000);
  }
  CHECK(fired);
  int count = 0;
  h.update(false, 0, 0, t += 10, 2000);
  for (int i = 0; i < 40; i++) count += h.update(true, 100 + (i % 2), 100, t += 100, 2000);
  CHECK(count == 1);
  h.update(false, 0, 0, t += 10, 2000);
  fired = false;
  for (int i = 0; i < 40; i++) fired |= h.update(true, 50 + i * 10, 100, t += 100, 2000);
  CHECK(!fired);
  printf("gesture tests: %s\n", fails ? "FAILED" : "ok");
  return fails;
}
