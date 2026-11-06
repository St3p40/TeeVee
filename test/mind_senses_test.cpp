#include <cstdio>
#include <cmath>
#include "mind_senses.h"

static int fails = 0;
#define CHECK(c)                                                                                             \
  do {                                                                                                       \
    if (!(c)) {                                                                                              \
      printf("FAIL line %d: %s\n", __LINE__, #c);                                                            \
      fails++;                                                                                               \
    }                                                                                                        \
  } while (0)

int main() {
  IdleGen idle;
  Needs needs;
  Tastes tastes;
  const uint32_t now = 60000;

  // A board with no touch screen and no IMU: those inputs stay 0, even if the source says otherwise.
  {
    MindSources s;
    s.touching = true;
    s.tx = 0.5f;
    s.motion = 0.7f;
    s.tiltX = 0.3f;
    s.carried = true;
    MindInputs in;
    fillMindInputs(in, s, now, idle, needs, tastes);
    CHECK(!in.touching && in.tx == 0 && in.ty == 0);
    CHECK(in.motion == 0 && in.tiltX == 0 && !in.carried);
  }

  // A desktop: the same fields are read.
  {
    MindSources s;
    s.hasTouch = true;
    s.touching = true;
    s.tx = 0.5f;
    s.hasMotion = true;
    s.motion = 0.7f;
    s.tiltX = 0.3f;
    s.carried = true;
    MindInputs in;
    fillMindInputs(in, s, now, idle, needs, tastes);
    CHECK(in.touching && fabsf(in.tx - 0.5f) < 1e-6f);
    CHECK(fabsf(in.motion - 0.7f) < 1e-6f && fabsf(in.tiltX - 0.3f) < 1e-6f && in.carried);
  }

  // Times: 0 means never; otherwise seconds since the stamp.
  {
    MindSources s;
    MindInputs in;
    fillMindInputs(in, s, now, idle, needs, tastes);
    CHECK(in.sinceTouch == 1e9f && in.sinceEvent == 1e9f);
    s.lastTouch = 50000;
    s.lastEvent = now;
    fillMindInputs(in, s, now, idle, needs, tastes);
    CHECK(fabsf(in.sinceTouch - 10.f) < 1e-3f && fabsf(in.sinceEvent) < 1e-6f);
  }

  // Light: no reading is 0 in the net; 0..1 maps to -1..1.
  {
    MindSources s;
    MindInputs in;
    s.light = -1;
    fillMindInputs(in, s, now, idle, needs, tastes);
    CHECK(in.light == 0);
    s.light = 0;
    fillMindInputs(in, s, now, idle, needs, tastes);
    CHECK(in.light == -1);
    s.light = 1;
    fillMindInputs(in, s, now, idle, needs, tastes);
    CHECK(in.light == 1);
  }

  // Week: known only with a day.
  {
    MindSources s;
    MindInputs in;
    fillMindInputs(in, s, now, idle, needs, tastes);
    CHECK(!in.weekKnown);
    s.weekDay = 2;
    s.weekHour = 9;
    fillMindInputs(in, s, now, idle, needs, tastes);
    CHECK(in.weekKnown);
  }

  // Presence: 0 when never seen, 1 right when seen, falling away after.
  {
    MindSources s;
    MindInputs in;
    fillMindInputs(in, s, now, idle, needs, tastes);
    CHECK(in.presence == 0);
    s.presentAt = now;
    fillMindInputs(in, s, now, idle, needs, tastes);
    CHECK(fabsf(in.presence - 1.f) < 1e-6f);
    s.presentAt = now - 120000;
    fillMindInputs(in, s, now, idle, needs, tastes);
    CHECK(fabsf(in.presence - expf(-1.f)) < 1e-5f);
  }

  // Eyes: the openness is the mean of the two.
  {
    MindSources s;
    s.eyeOpenL = 1;
    s.eyeOpenR = 0;
    MindInputs in;
    fillMindInputs(in, s, now, idle, needs, tastes);
    CHECK(fabsf(in.openness - 0.5f) < 1e-6f);
  }

  if (fails) return 1;
  printf("mind_senses_test: ok\n");
  return 0;
}
