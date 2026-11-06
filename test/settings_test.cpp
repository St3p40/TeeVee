#include <cmath>
#include <cstdio>
#include "screen_idle.h"
#include "settings.h"
#include "json.h"
int fails = 0;
#define CHECK(c)                                                                                             \
  do {                                                                                                       \
    if (!(c)) {                                                                                              \
      printf("FAIL line %d: %s\n", __LINE__, #c);                                                            \
      fails++;                                                                                               \
    }                                                                                                        \
  } while (0)
int main() {
  Settings v2 = settingsDefaults();
  v2.version = 2;
  v2.personality = 4;
  v2.brightness = 120;
  v2.auto_emotes = false;
  uint8_t blob[512];
  memcpy(blob, &v2, 32);
  Settings s;
  settingsLoad(s, blob, 32);
  CHECK(s.version == 3 && s.personality == 4 && s.brightness == 120 && !s.auto_emotes && s.ev_w[3] == 1 &&
        s.r_poke == -0.3f);
  CHECK(s.screen_off == 10);
  {  // the display going dark: only people count, a first press only wakes it, what must be seen keeps it on, 0 is never
    ScreenIdle d;
    d.activity(1000);
    CHECK(!d.update(1000 + 10 * 60000u - 1, 10, false) && d.update(1000 + 10 * 60000u, 10, false) && d.off());
    CHECK(d.wake(700000) && !d.off() && !d.wake(700001));
    CHECK(!d.update(700001 + 5 * 60000u, 10, false));
    CHECK(!d.update(700001 + 60 * 60000u, 10, true));
    CHECK(!d.update(700001 + 60 * 60000u + 5, 10, false));
    CHECK(d.update(700001 + 70 * 60000u + 5, 10, false));
    CHECK(!d.update(700001 + 70 * 60000u + 6, 0, false) && !d.off());  // set to never while dark: back on
    CHECK(!d.update(4000000000u, 0, false));
    d.activity(4294967000u);  // millis wraps after 49 days
    CHECK(!d.update(100, 10, false) && d.update(10 * 60000u, 10, false));
    d.activity(0);
    CHECK(d.update(120 * 60000u, 120, false));  // the longest setting does not overflow
  }
  blob[0] = 1;
  blob[30] = 0;
  settingsLoad(s, blob, 32);
  CHECK(s.auto_emotes);  // v1: padding -> default on
  blob[0] = 99;
  settingsLoad(s, blob, 32);
  CHECK(s.personality == 0);
  CHECK(settingSet(s, "w_poke", "0") && s.ev_w[1] == 0 && settingSet(s, "x_angry", "2") && s.expr_w[3] == 2 &&
        settingSet(s, "r_scold", "-0.5") && s.r_scold == -0.5f);
  {  // a damaged blob loads clamped values: no NaN, bools 0/1, enums inside their options
    Settings bad = settingsDefaults();
    uint8_t raw[sizeof bad];
    memcpy(raw, &bad, sizeof raw);
    const float nan = NAN, big = 1e30f;
    memcpy(raw + offsetof(Settings, sensitivity), &nan, 4);
    memcpy(raw + offsetof(Settings, recovery), &big, 4);
    raw[offsetof(Settings, personality)] = 200;
    raw[offsetof(Settings, night_dim)] = 205;
    raw[offsetof(Settings, brightness)] = 0;
    Settings t;
    settingsLoad(t, raw, sizeof raw);
    unsigned char b;
    memcpy(&b, &t.night_dim, 1);
    CHECK(t.sensitivity == settingsDefaults().sensitivity && t.recovery == settingDef("recovery")->max &&
          t.personality == 4 && b == 1 && t.brightness == 10);
    CHECK(!settingSet(t, "sensitivity", "nan") && !settingSet(t, "brightness", "inf") && t.brightness == 10);
  }
  static char out[12288];  // the same as the board's reply (protocol.cpp)
  size_t n = settingsJson(s, out, sizeof out, true);
  JsonView r;
  CHECK(n + 1 < sizeof out);
  CHECK(jsonParse(out, n, r) && r.get("meta").at(13).get("group").eq("events") &&
        r.get("settings").get("x_angry").num(0) == 2);
  printf("sizeof Settings %zu, %d settings, json %zu bytes: %s\n", sizeof(Settings), SETTING_COUNT, n,
         fails ? "FAILED" : "ok");
  return fails ? 1 : 0;
}
