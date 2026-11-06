#include <cstdio>
#include <cstdlib>
#include <cmath>
#include "voice.h"

static int fails = 0;
#define CHECK(c)                                                                                             \
  do {                                                                                                       \
    if (!(c)) {                                                                                              \
      printf("FAIL line %d: %s\n", __LINE__, #c);                                                            \
      fails++;                                                                                               \
    }                                                                                                        \
  } while (0)

static const int RATE = 16000;

// Plays the voice to the end: the number of samples, and the loudest one.
static int playOut(Voice& v, float volume, int& peak) {
  int16_t buf[320];
  int total = 0;
  peak = 0;
  while (v.busy() && total < RATE * 5) {
    v.render(buf, 320, RATE, volume);
    for (int16_t s : buf) peak = abs(s) > peak ? abs(s) : peak;
    total += 320;
  }
  return total;
}

int main() {
  const PAD calm{0, 0, 0}, happy{0.8f, 0.6f, 0.2f}, sad{-0.7f, -0.3f, -0.2f};

  // Every event has a sound, in range, done within two seconds.
  for (int ev = 0; ev < EV_COUNT; ev++) {
    Voice v;
    v.say((uint8_t)ev, calm, 0.6f);
    CHECK(v.busy());
    int peak;
    const int n = playOut(v, 1.f, peak);
    CHECK(n > 0 && n < RATE * 2);
    CHECK(peak > 2000 && peak <= 32767);
  }

  // Volume 0 is silence; nothing queued is silence.
  {
    Voice v;
    v.say(EV_PRAISE, calm, 1.f);
    int peak;
    playOut(v, 0.f, peak);
    CHECK(peak == 0);
    int16_t buf[64];
    v.render(buf, 64, RATE, 1.f);
    for (int16_t s : buf) CHECK(s == 0);
  }

  // Never quite the same twice: the same event, said again, comes out a different length.
  {
    Voice v;
    int lengths[6], peak;
    for (int& l : lengths) {
      v.say(EV_PET, calm, 0.5f);
      l = playOut(v, 1.f, peak);
    }
    bool differ = false;
    for (int l : lengths) differ = differ || l != lengths[0];
    CHECK(differ);
  }

  // The mood colours it: a happy greeting is higher than a sad one (counted by zero crossings over the same time).
  {
    auto crossings = [](const PAD& mood) {
      Voice v;
      v.say(EV_GREET, mood, 0.5f);
      int16_t buf[1600];
      v.render(buf, 1600, RATE, 1.f);
      int c = 0;
      for (int i = 1; i < 1600; i++) c += (buf[i - 1] < 0) != (buf[i] < 0);
      return c;
    };
    CHECK(crossings(happy) > crossings(sad));
  }

  // The gate: silent when asleep; not the same event twice in a row; pets only now and then.
  {
    VoiceGate g;
    CHECK(!g.shouldSay(EV_GREET, 1.f, 1000, true, 0.f));
    CHECK(g.shouldSay(EV_GREET, 1.f, 1000, false, 0.f));
    CHECK(!g.shouldSay(EV_PRAISE, 1.f, 1500, false, 0.f));  // too soon after anything
    CHECK(g.shouldSay(EV_PRAISE, 1.f, 2500, false, 0.f));
    CHECK(!g.shouldSay(EV_PRAISE, 1.f, 3500, false, 0.f));  // too soon after itself
    CHECK(!g.shouldSay(EV_PET, 0.1f, 9000, false, 0.f));    // too weak to remark on
    CHECK(!g.shouldSay(EV_PET, 1.f, 9000, false, 0.9f));    // pets: only sometimes
    CHECK(g.shouldSay(EV_PET, 1.f, 9000, false, 0.2f));
  }

  if (fails) return 1;
  printf("voice_test: ok\n");
  return 0;
}
