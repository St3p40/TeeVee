#include <cstdio>
#include <cmath>
#include "hearing.h"

static int fails = 0;
#define CHECK(c)                                                                                             \
  do {                                                                                                       \
    if (!(c)) {                                                                                              \
      printf("FAIL line %d: %s\n", __LINE__, #c);                                                            \
      fails++;                                                                                               \
    }                                                                                                        \
  } while (0)

static float rmsOf(float db) { return powf(10.f, db / 20.f); }

int main() {
  const float dt = 0.02f;

  // A quiet room: nothing happens.
  {
    Hearing h;
    int loud = 0;
    for (int i = 0; i < 1000; i++) loud += h.update(rmsOf(30), dt).loud;
    CHECK(loud == 0 && !h.talking());
  }

  // A clap: one loud event, not one per block.
  {
    Hearing h;
    for (int i = 0; i < 500; i++) h.update(rmsOf(30), dt);
    int loud = 0;
    float k = 0;
    for (int i = 0; i < 5; i++) {
      HearingOut o = h.update(rmsOf(75), dt);
      loud += o.loud;
      if (o.loud) k = o.loudK;
    }
    for (int i = 0; i < 50; i++) loud += h.update(rmsOf(30), dt).loud;
    CHECK(loud == 1);
    CHECK(k >= 0.3f && k <= 1.f);
  }

  // A slow swell to the same level is not a startle.
  {
    Hearing h;
    for (int i = 0; i < 500; i++) h.update(rmsOf(30), dt);
    int loud = 0;
    for (int i = 0; i < 400; i++) loud += h.update(rmsOf(30 + 45 * i / 400.f), dt).loud;
    CHECK(loud == 0);
  }

  // Talking after a long quiet: talking, and one greeting; then a steady noise becomes the new floor.
  {
    Hearing h;
    for (int i = 0; i < 6000; i++) h.update(rmsOf(30), dt);  // two minutes of quiet
    int starts = 0;
    for (int i = 0; i < 150; i++) {
      // syllables: on and off, about 15 dB above the floor
      starts += h.update(rmsOf((i / 8) % 2 ? 45 : 33), dt).voiceStart;
    }
    CHECK(h.talking());
    CHECK(starts == 1);
    for (int i = 0; i < 6000; i++) h.update(rmsOf(45), dt);  // the TV stays on
    CHECK(!h.talking());
    CHECK(h.floorDb() > 40);
  }

  // Start-up: the codec gives near-silence first, then the room. No talking, no greeting, no startle.
  {
    Hearing h;
    int events = 0;
    for (int i = 0; i < 10; i++) events += h.update(0, dt).loud;
    for (int i = 0; i < 1500; i++) {
      HearingOut o = h.update(rmsOf(40), dt);
      events += o.loud + o.voiceStart;
      if (i > 150) CHECK(!h.talking());
    }
    CHECK(events == 0);
  }

  // After a pause (its own voice), coming back into a louder room is not a startle.
  {
    Hearing h;
    for (int i = 0; i < 500; i++) h.update(rmsOf(30), dt);
    h.pause();
    int loud = 0;
    for (int i = 0; i < 5; i++) loud += h.update(rmsOf(62), dt).loud;
    CHECK(loud == 0);
  }

  // More sensitivity hears quieter sounds.
  {
    Hearing a, b;
    b.setSensitivity(2.f);
    for (int i = 0; i < 500; i++) a.update(rmsOf(30), dt), b.update(rmsOf(30), dt);
    for (int i = 0; i < 100; i++) a.update(rmsOf(36), dt), b.update(rmsOf(36), dt);
    CHECK(!a.talking() && b.talking());
  }

  if (fails) return 1;
  printf("hearing_test: ok\n");
  return 0;
}
