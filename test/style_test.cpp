#include <cstdio>
#include "style.h"
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
  FaceStyle s;
  {
    CHECK(styleFindPreset("TeeVee") == 0 && styleFindPreset("teevee") == 0 && styleFindPreset("nonsense") == -1);
    CHECK(styleFindPreset("led") == -1 && styleFindPreset("cute") == -1);  // no aliases: the old names are gone
    for (int i = 0; i < STYLE_PRESET_COUNT; i++)
      for (int j = i + 1; j < STYLE_PRESET_COUNT; j++)
        CHECK(!styleNameEq(STYLE_PRESETS[i].name, STYLE_PRESETS[j].name));
    bool seen[STYLE_PRESET_COUNT] = {};
    int kept = 0;
    for (int i = 0; i < STYLE_PRESETS_OLD_COUNT; i++)
      if (const int to = styleOldPreset(i); to >= 0) {
        CHECK(!seen[to]);
        seen[to] = true;
        kept++;
      }
    CHECK(kept + 5 == STYLE_PRESET_COUNT);
    for (int i = kept; i < STYLE_PRESET_COUNT; i++) CHECK(!seen[i]);
    CHECK(styleOldPreset(0) == 0 && styleOldPreset(2) == -1 && styleOldPreset(-1) == -1 &&
          styleOldPreset(99) == -1);
  }
  {  // which preset the look is now: exactly that one, and none once it is changed
    FaceStyle x;
    for (const StylePreset& p : STYLE_PRESETS) { CHECK(styleApplyPreset(x, p.name)); const char* now = stylePresetNow(x); CHECK(now && !strcmp(now, p.name)); }
    styleApplyPreset(x, "round"); CHECK(!strcmp(stylePresetNow(x), "round"));
    styleApplyText(x, "hue 11"); CHECK(stylePresetNow(x) == nullptr);
  }
  for (const StylePreset& p : STYLE_SEASONS) {
    FaceStyle t = STYLE_DEFAULT;
    CHECK(styleApplyText(t, p.changes) > 0);
    FaceStyle u;
    CHECK(styleApplyPreset(u, p.name));
  }
  CHECK(!strcmp(styleSeasonName(10, 31), "halloween") && !strcmp(styleSeasonName(11, 2), "autumn") &&
        !strcmp(styleSeasonName(12, 31), "newyear") && !strcmp(styleSeasonName(1, 3), "winter") &&
        !strcmp(styleSeasonName(4, 1), "spring") && !strcmp(styleSeasonName(7, 1), "summer"));
  for (const StylePreset& p : STYLE_PRESETS) {
    FaceStyle t = STYLE_DEFAULT;
    int n = styleApplyText(t, p.changes);
    if (n < 0) {
      printf("preset %s bad\n", p.name);
      fails++;
    }
  }
  CHECK(styleApplyPreset(s, "sweet") && s.hue == 340 && s.mouth_style == 1 && s.lashes && s.bg[0] == 20 &&
        s.eye_gap == 130);
  CHECK(styleApplyPreset(s, "anime") && s.eye_gap == 140 && s.pupil_size > 0.33f);
  CHECK(!styleApplyPreset(s, "nope"));
  s = STYLE_DEFAULT;
  CHECK(styleApplyText(s, "hue 400 eye_w 999 bg 300 1 2 tint on mouth none") == 5);
  CHECK(s.hue == 40 && s.eye_w == 68 && s.bg[0] == 255 && s.tint && s.mouth_style == 2);
  CHECK(styleApplyText(s, "hue") == -1 && styleApplyText(s, "nope 1") == -1);
  FaceStyle old = STYLE_DEFAULT;
  styleApplyPreset(old, "sweet");
  uint8_t blob[sizeof(FaceStyle)];
  memcpy(blob, &old, sizeof old);
  blob[38] = 0xAB;
  blob[39] = 0xCD;
  styleLoad(s, blob, 40);
  CHECK(s.hue == 340 && !s.brows && s.eye_gap == 130 && s.speed == 1);
  styleLoad(s, blob, 44);
  CHECK(s.hue == 340 && s.mouth_style == 1 && s.eye_gap == 130 && !s.lashes);
  {
    uint32_t rng = 12345;
    FaceStyle a = STYLE_DEFAULT;
    a.pixel = 4;
    a.glitch = 3;
    a.daytint = 0.5f;
    FaceStyle prev = a;
    int different = 0;
    for (int i = 0; i < 200; i++) {
      styleRandomize(a, rng);
      CHECK(a.pixel == 4 && a.glitch == 3 && a.daytint == 0.5f);
      CHECK(a.eye_w >= 30 && a.eye_w <= 68 && a.eye_h >= 30 && a.eye_h <= 90 &&
            a.mouth_style < MOUTH_STYLE_COUNT && a.pupil_style < PUPIL_COUNT);
      different += memcmp(&a, &prev, sizeof a) != 0;
      prev = a;
    }
    CHECK(different >= 195);
  }
  {
    FaceStyle s = STYLE_DEFAULT;
    CHECK(styleApplyText(s, "hollow 1") == 1 && s.eye_style == 1 && styleApplyText(s, "hollow 0") == 1 &&
          s.eye_style == 0);
    CHECK(styleApplyText(s, "eye spiral") == 1 && s.eye_style == 4 && styleApplyText(s, "pupils 1") == 1 &&
          s.eye_style == 4);
    CHECK(styleApplyText(s, "eye star") == 1 && s.eye_style == 0 && s.pupil_style == 4 &&
          styleApplyText(s, "eye outline") == 1 && s.eye_style == 1);
    CHECK(styleApplyText(s, "pupil square") == 1 && s.pupil_style == 5 &&
          styleApplyText(s, "pupil none") == 1 && s.pupil_style == 0);
  }
  {
    FaceStyle t = STYLE_DEFAULT;
    CHECK(!styleSet(t, "eye_w", "nan") && !styleSet(t, "hue", "inf") && !styleSet(t, "hue", "-inf") &&
          t.eye_w == STYLE_DEFAULT.eye_w);
    uint8_t raw[sizeof(FaceStyle)];
    memcpy(raw, &STYLE_DEFAULT, sizeof raw);
    memset(raw + offsetof(FaceStyle, tint), 205, 5);
    styleLoad(t, raw, sizeof raw);
    unsigned char b;
    memcpy(&b, &t.blush, 1);
    CHECK(b == 1);
  }
  {
    uint32_t rng = 99;
    size_t longest = 0;
    static char t[2048];
    for (int i = 0; i < 500; i++) {
      FaceStyle a = STYLE_DEFAULT;
      styleRandomize(a, rng);
      a.hue = 355.f;
      a.daytint = 0.123f;
      styleDescribe(a, t, sizeof t);
      longest = strlen(t) > longest ? strlen(t) : longest;
      FaceStyle b = STYLE_DEFAULT;
      CHECK(styleApplyText(b, t) > 0);
      char t2[2048];
      styleDescribe(b, t2, sizeof t2);
      CHECK(!strcmp(t, t2));
    }
    CHECK(longest + 120 < STYLE_TEXT_MAX);
    printf("longest look text %zu of %zu\n", longest, STYLE_TEXT_MAX);
  }
  char out[4800];
  size_t n = styleJson(s, out, sizeof out, true);
  JsonView r;
  CHECK(jsonParse(out, n, r) && r.get("style").get("mouth").eq("cat") &&
        r.get("style").get("bg").at(0).num(0) == 20 && r.get("presets").at(8).eq(STYLE_PRESETS[8].name));
  char d[600];
  styleDescribe(s, d, sizeof d);
  {
    const auto same = [](const FaceStyle& a, const FaceStyle& b, int group) {
      FaceStyle t = a;
      styleCopyGroup(t, b, group);
      return !memcmp(&t, &a, sizeof t);
    };
    uint32_t rng = 12345;
    int most = 0, least = 9, partsTouched[SG_COUNT] = {};
    for (int i = 0; i < 300; i++) {
      FaceStyle base = STYLE_DEFAULT, s = base;
      base.pixel = s.pixel = 3;
      base.glitch = s.glitch = 2;
      styleRandomizeSome(s, rng);
      int changed = 0;
      for (int g = 0; g < SG_COUNT; g++)
        if (!same(s, base, g)) {
          changed++;
          partsTouched[g]++;
        }
      if (changed > most) most = changed;
      if (changed < least) least = changed;
      CHECK(s.pixel == 3 && s.glitch == 2);
      FaceStyle t = base;
      for (int g = 0; g < SG_COUNT; g++) styleCopyGroup(t, s, g);
      CHECK(!memcmp(&t, &s, sizeof t));
    }
    CHECK(most <= 2 && least >= 1);
    for (int g = 0; g < SG_COUNT; g++) CHECK(partsTouched[g] > 30);
    printf("randomize a part: %d..%d parts change; ", least, most);
  }
  printf("sizeof FaceStyle %zu; json %zu bytes; describe %zu chars; %s\n", sizeof(FaceStyle), n, strlen(d),
         fails ? "FAILED" : "ok");
  return fails ? 1 : 0;
}
