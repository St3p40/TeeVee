#include <cstdio>
#include <string>
#include <vector>
#include "motion_sim.h"
static int fails = 0;
#define CHECK(c)                                                                                             \
  do {                                                                                                       \
    if (!(c)) {                                                                                              \
      printf("FAIL line %d: %s\n", __LINE__, #c);                                                            \
      fails++;                                                                                               \
    }                                                                                                        \
  } while (0)

struct Ev {
    float t;
    uint8_t e;
    float k;
};
static std::vector<Ev> run(int scenario, float sens = 1.f, float seconds = -1) {
  MotionClassifier c;
  c.setSensitivity(sens);
  std::vector<Ev> out;
  float total = seconds > 0 ? seconds : SIM_SECONDS[scenario];
  for (float t = 0; t < total; t += 0.01f) {
    MotionOut o = c.update(simSample(scenario, t), 0.01f);
    if (o.event != ME_NONE) out.push_back({t, o.event, o.intensity});
  }
  return out;
}
static int count(const std::vector<Ev>& v, uint8_t e) {
  int n = 0;
  for (auto& x : v) n += x.e == e;
  return n;
}
static float when(const std::vector<Ev>& v, uint8_t e) {
  for (auto& x : v)
    if (x.e == e) return x.t;
  return -1;
}
static std::string list(const std::vector<Ev>& v) {
  std::string s;
  char b[64];
  for (auto& x : v) {
    snprintf(b, sizeof b, " %s@%.2f", MOTION_EVENT_NAMES[x.e], x.t);
    s += b;
  }
  return s.empty() ? " (none)" : s;
}

int main() {
  for (int s = 0; s < SIM_COUNT; s++) printf("  %-10s ->%s\n", SIM_NAMES[s], list(run(s)).c_str());

  CHECK(run(SIM_STILL).empty());
  {
    auto v = run(SIM_KNOCK);
    CHECK(v.size() == 1 && v[0].e == ME_KNOCK && v[0].t > 0.99f && v[0].t < 1.1f && v[0].k > 0.3f);
  }
  {
    auto v = run(SIM_SHAKE);
    CHECK(count(v, ME_SHAKE) == 1 && when(v, ME_SHAKE) > 0.5f && when(v, ME_SHAKE) < 1.6f &&
          count(v, ME_KNOCK) == 0);
  }
  {
    auto v = run(SIM_PICKUP);
    CHECK(count(v, ME_PICKUP) == 1 && count(v, ME_PUTDOWN) == 1 && when(v, ME_PICKUP) < when(v, ME_PUTDOWN) &&
          when(v, ME_PICKUP) < 1.2f);
  }
  {
    auto v = run(SIM_FALL);
    CHECK(count(v, ME_FALL) == 1 && when(v, ME_FALL) > 0.5f && when(v, ME_FALL) < 0.8f &&
          count(v, ME_PICKUP) == 0 && v.size() == 1);
  }
  {
    auto v = run(SIM_FACEDOWN);
    CHECK(count(v, ME_FACEDOWN) == 1 && when(v, ME_PICKUP) >= 0 &&
          when(v, ME_PUTDOWN) < when(v, ME_FACEDOWN));
  }
  {
    auto v = run(SIM_UPSIDEDOWN);
    CHECK(count(v, ME_UPSIDEDOWN) == 1 && when(v, ME_PICKUP) >= 0);
  }
  {
    auto v = run(SIM_ROCK);
    CHECK(count(v, ME_ROCK) >= 2 && when(v, ME_ROCK) > 2.f && count(v, ME_SHAKE) == 0 && count(v, ME_SPIN) == 0);
  }
  {
    auto v = run(SIM_SPIN);
    CHECK(count(v, ME_SPIN) == 1 && when(v, ME_SPIN) < 2.f && count(v, ME_ROCK) == 0 && count(v, ME_SHAKE) == 0);
  }
  for (int sc : {SIM_KNOCK, SIM_SHAKE, SIM_PICKUP, SIM_FALL, SIM_FACEDOWN, SIM_UPSIDEDOWN}) {
    auto v = run(sc);
    CHECK(count(v, ME_ROCK) == 0 && count(v, ME_SPIN) == 0);
  }
  {  // tilt is the change from rest: it leans while turned, and settles back when held still
    MotionClassifier c;
    for (int i = 0; i < 300; i++) c.update({0, 1, 0, 0, 0, 0}, 0.01f);
    for (int i = 0; i < 100; i++) c.update({0.25f, 0.97f, 0, 0, 0, 0}, 0.01f);  // turned to the right by 0.25 g
    CHECK(c.tiltX() > 0.3f);
    for (int i = 0; i < 300; i++) c.update({0.25f, 0.97f, 0, 0, 0, 0}, 0.01f);  // held still in that pose for 3 s
    CHECK(fabsf(c.tiltX()) < 0.1f);
  }
  {  // flat, tipped towards its right edge: downhill is to the right; standing, turned clockwise: the roll is negative
    MotionClassifier c;
    float g[3];
    motion_sim::gravity(90.f, 0, g);  // screen up
    const float tip = 0.3f;
    for (int i = 0; i < 300; i++) c.update({-tip, 0, sqrtf(1 - tip * tip), 0, 0, 0}, 0.01f);
    CHECK(c.flatX() > 0.5f && fabsf(c.flatY()) < 0.1f);
    MotionClassifier u;
    for (int i = 0; i < 300; i++) u.update({-0.26f, 0.97f, 0, 0, 0, 0}, 0.01f);  // standing, 15 degrees clockwise
    CHECK(u.rollDeg() < -10.f && u.rollDeg() > -20.f);
  }
  CHECK(count(run(SIM_KNOCK, 0.3f), ME_KNOCK) == 0);
  CHECK(count(run(SIM_KNOCK, 3.f), ME_KNOCK) == 1);
  {
    MotionClassifier c;
    std::vector<uint8_t> seq;
    float t = 0;
    auto feed = [&](int sc, float tt) {
      MotionOut o = c.update(simSample(sc, tt), 0.01f);
      if (o.event != ME_NONE) seq.push_back(o.event);
    };
    for (; t < 5.f; t += 0.01f) feed(SIM_FACEDOWN, t);
    for (float tt = 0; tt < 4.f; tt += 0.01f) {
      MotionSample s = simSample(SIM_FACEDOWN, 5.f);
      float back = -90.f * (1 - motion_sim::smooth(tt, 0.5f, 1.4f));
      float g[3];
      motion_sim::gravity(back, 0, g);
      s.ax = g[0];
      s.ay = g[1];
      s.az = g[2];
      if (tt > 0.5f && tt < 2.0f) {
        s.ay += 0.2f * sinf(6.2832f * 3.f * tt);
        s.gx = 90.f * sinf(6.2832f * 2.f * tt);
      }
      MotionOut o = c.update(s, 0.01f);
      if (o.event != ME_NONE) seq.push_back(o.event);
    }
    int faceup = 0;
    for (auto e : seq) faceup += e == ME_FACEUP;
    CHECK(faceup == 1);
    CHECK(c.orientation() == OR_UPRIGHT);
  }
  {  // turning leans the eyes the way it turns (x right, y down)
    MotionClassifier c;
    for (int i = 0; i < 300; i++) c.update({0.f, 1.f, 0.f, 0, 0, 0}, 0.01f);
    for (int i = 0; i < 50; i++) c.update({0.3f, 0.95f, 0.0f, 0, 0, 0}, 0.01f);
    CHECK(c.tiltX() > 0.05f && fabsf(c.tiltY()) < 0.05f);
    for (int i = 0; i < 50; i++) c.update({0.f, 0.9f, 0.4f, 0, 0, 0}, 0.01f);
    CHECK(c.tiltY() < -0.05f);
  }
  printf("motion tests: %s\n", fails ? "FAILED" : "ok");
  return fails;
}
