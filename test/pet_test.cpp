#include <cstdio>
#include <cmath>
#include "../desktop/pet.h"
static int fails = 0;
#define CHECK(c)                                                                                             \
  do {                                                                                                       \
    if (!(c)) {                                                                                              \
      printf("FAIL line %d: %s\n", __LINE__, #c);                                                            \
      fails++;                                                                                               \
    }                                                                                                        \
  } while (0)

static void run(PetWalker& p, const PetArea& a, float seconds, bool awake, float lively,
                float* landed = nullptr, int* walks = nullptr) {
  PetWalker::State was = p.state();
  for (float t = 0; t < seconds; t += 1 / 60.f) {
    const float l = p.step(1 / 60.f, a, awake, lively);
    if (landed && l > *landed) *landed = l;
    if (walks && p.state() == PetWalker::WALK && was != PetWalker::WALK) ++*walks;
    was = p.state();
    CHECK(p.x() >= a.left - 0.01f && p.x() <= a.right + 0.01f);
    CHECK(p.y() <= a.floor + 0.01f);
  }
}

int main() {
  const PetArea a = {0, 1600, 800};
  {
    PetWalker p;
    p.place(500, 100);
    p.release();
    float landed = 0;
    run(p, a, 2, true, 0.5f, &landed);
    CHECK(p.y() == a.floor);
    CHECK(landed > 1000);
  }
  {
    PetWalker p;
    p.place(800, a.floor);
    int walks = 0;
    run(p, a, 300, true, 1.f, nullptr, &walks);
    CHECK(walks >= 10);
    CHECK(p.y() == a.floor);
  }
  {
    PetWalker p;
    p.place(300, a.floor);
    run(p, a, 120, false, 1.f);
    CHECK(p.x() == 300);
    PetWalker q;
    q.place(300, a.floor);
    q.setWalking(false);
    run(q, a, 120, true, 1.f);
    CHECK(q.x() == 300);
  }
  {  // carried: does not move by itself, even in the air
    PetWalker p;
    p.place(200, 50);
    p.grab();
    run(p, a, 5, true, 1.f);
    CHECK(p.x() == 200 && p.y() == 50);
  }
  {
    PetWalker p;
    p.place(1500, a.floor);
    const PetArea small = {0, 1000, 600};
    run(p, small, 1, true, 0.f);
    CHECK(p.x() <= 1000 && p.y() == 600);
  }
  {  // a screen narrower than a walk: it never walks, and does not jitter
    PetWalker p;
    const PetArea narrow = {100, 150, 400};
    p.place(120, 400);
    run(p, narrow, 60, true, 1.f);
    CHECK(p.x() == 120);
  }
  {  // the walk cycle: hops while walking (never below the floor, never more than 8 px up), is down when it stops
    PetWalker p;
    p.place(800, a.floor);
    float top = 0, atStop = 0;
    PetWalker::State was = p.state();
    float lastHop = 0;
    for (int i = 0; i < 60 * 300; i++) {
      p.step(1 / 60.f, a, true, 1.f);
      CHECK(p.hop() >= 0 && p.hop() <= 8.01f);
      if (p.hop() > top) top = p.hop();
      if (was == PetWalker::WALK && p.state() != PetWalker::WALK && lastHop > atStop) atStop = lastHop;
      was = p.state();
      lastHop = p.hop();
    }
    CHECK(top > 6);
    CHECK(
        atStop <
        3);  // the frame before it stops is nearly down (whole hops; one frame of a hop is ~2.4 px), not a drop from the top
  }
  {
    PetWalker p;
    p.place(800, a.floor);
    p.release(2500, -1200);
    float maxX = 0, minY = 1e9f, landed = 0;
    int bounces = 0;
    for (int i = 0; i < 60 * 8; i++) {
      const float l = p.step(1 / 60.f, a, true, 0.5f);
      if (l > 0) bounces++, landed = l;
      if (p.x() > maxX) maxX = p.x();
      if (p.y() < minY) minY = p.y();
      CHECK(p.x() >= a.left - 0.01f && p.x() <= a.right + 0.01f && p.y() <= a.floor + 0.01f);
    }
    CHECK(maxX == a.right && minY < 600);
    CHECK(p.state() != PetWalker::FALL && p.y() == a.floor && bounces >= 2 && landed > 0);
  }
  {
    PetLedge L[1] = {{400, 900, 500}};
    PetArea la = a;
    la.ledges = L, la.nLedges = 1;
    PetWalker p;
    p.place(600, 100);
    p.release();
    run(p, la, 2, true, 0.5f);
    CHECK(p.y() == 500);
    run(p, la, 120, true, 1.f);
    CHECK(p.y() == 500 || p.y() == a.floor);
    p.place(600, 500);
    run(p, la, 0.2f, true, 1.f);
    la.nLedges = 0;
    run(p, la, 2, true, 1.f);
    CHECK(p.y() == a.floor);
    PetWalker q;
    q.place(600, a.floor);
    la.nLedges = 1;
    q.release(0, -1800);
    float top = 1e9f;
    for (int i = 0; i < 30; i++) {
      q.step(1 / 60.f, la, true, 0.5f);
      if (q.y() < top) top = q.y();
    }
    CHECK(top < 500);
  }
  {
    PetWalker p;
    p.place(200, a.floor);
    run(p, a, 1, true, 0.5f);
    CHECK(p.goTo(1000, a) && p.rushing());
    int t = 0;
    while (!p.arrived && t++ < 60 * 30) p.step(1 / 60.f, a, true, 0.5f);
    CHECK(p.arrived && fabsf(p.x() - 1000) < PetWalker::STRIDE + 0.01f && p.y() == a.floor);
    CHECK(!p.goTo(p.x() + 5, a));
  }
  {
    const PetRect z[] = {{300, 100, 400, 300}, {100, 200, 1000, 400}, {0, 0, 50, 20}, {500, 10, 300, 300}};
    PetLedge out[8];
    const int n = petLedges(z, 4, 320, 240, 40, -150, out, 8);
    CHECK(n == 3);
    CHECK(out[0].x0 == 300 - 160 && out[0].x1 == 700 - 160 && out[0].y == 100 - 240 + 40);
    CHECK(out[1].x0 == 100 - 160 && out[1].x1 == 300 - 160 && out[1].y == 200 - 200);
    CHECK(out[2].x0 == 700 - 160 && out[2].x1 == 1100 - 160);
  }
  if (fails) {
    printf("pet_test: %d failed\n", fails);
    return 1;
  }
  printf("pet_test: ok\n");
  return 0;
}
