#pragma once
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

struct Settings {
    uint32_t version;
    uint8_t personality;
    float sensitivity;
    float recovery;
    uint8_t brightness;
    bool night_dim;
    uint8_t sleep_after;  // minutes idle before dozing, 0 = never
    bool night_sleep;
    bool clock_24h;
    uint8_t temp_unit;
    float behaviours;  // x how often idle antics happen, 0 = never
    bool learner;
    bool panels;
    bool auto_emotes;
    // v3 (fields are only ever appended: a saved blob is a prefix and loads with newer fields at their defaults)
    float ev_w[13];
    float expr_w[13];  // per-expression strength (Anchor order), 0 = never shown
    float squash_amt;
    float motion_amt;
    float gaze_amt;
    float learn_rate;
    float explore;
    float r_pet, r_praise, r_love, r_greet, r_poke, r_annoy, r_scold;
    // motion sensor (older saved blobs load these at their defaults)
    float motion_sens;
    bool motion;
    bool auto_rotate;
    float mind_amount;
    bool wardrobe;
    float drift;
    bool wake_up;
    float head_turn;
    float fisheye;
    float look_chance;
    float look_whole;
    uint8_t screen_off;  // minutes with nobody doing anything before the display goes dark, 0 = never
    // A float first: a bool right after screen_off would sit in the old struct's padding, which saved blobs include, and
    // load as 0 instead of its default.
    float hearing_sens;
    bool hearing;  // react to sound (boards with a microphone)
    float volume;  // the voice, 0..1 (a float first again: see hearing_sens)
    bool sound;    // chirps and trills (boards with a speaker)
    float level;   // 0..1: the face stays level when it is turned round (boards with a motion sensor and the warp)
    float parallax;  // how far the eyes move with the tilt, 0 = still, 1 = normal, 2 = far
    bool dark_mode;  // only every second pixel is lit, in a chequer pattern (the AMOLED board: darker, less power, kinder to the panel)
};

static constexpr uint32_t SETTINGS_VERSION = 3;
static inline Settings settingsDefaults() {
  Settings s{};
  s.version = SETTINGS_VERSION;
  s.sensitivity = s.recovery = s.behaviours = 1.f;
  s.brightness = 200;
  s.night_dim = s.night_sleep = s.clock_24h = s.learner = s.panels = s.auto_emotes = true;
  s.screen_off = 10;
  s.sleep_after = 4;
  for (float& w : s.ev_w) w = 1;
  for (float& w : s.expr_w) w = 1;
  s.squash_amt = s.motion_amt = s.gaze_amt = 1;
  s.learn_rate = 0.05f;
  s.explore = 0.15f;
  s.r_pet = 0.8f;
  s.r_praise = 1;
  s.r_love = 1;
  s.r_greet = 0.4f;
  s.r_poke = -0.3f;
  s.r_annoy = -1;
  s.r_scold = -1;
  s.motion_sens = 1;
  s.auto_rotate = true;  // motion stays off until a sensor is wired
  s.mind_amount = 1;
  s.drift = 1;
  s.look_chance = 0.5f;
  s.look_whole = 0.3f;
  s.wake_up = true;
  s.head_turn = 0;
  s.fisheye = 0;
  s.hearing = true;
  s.hearing_sens = 1;
  s.volume = 0.6f;
  s.sound = true;
  s.level = 1;
  s.parallax = 1;
  return s;
}
#define SETTINGS_DEFAULTS settingsDefaults()

enum SettingType : uint8_t { ST_INT, ST_FLOAT, ST_BOOL, ST_ENUM };

struct SettingDef {
    const char* key;
    SettingType type;
    size_t offset;
    float min, max;
    const char* options;
    const char* help;
    const char* group;
};

inline const SettingDef SETTING_DEFS[] = {
  {"personality", ST_ENUM, offsetof(Settings, personality), 0, 4, "cheerful|calm|grumpy|shy|hyper", "base mood and temperament", "general"},
  {"sensitivity", ST_FLOAT, offsetof(Settings, sensitivity), 0.3f, 2.f, nullptr, "how strongly events move it", "general"},
  {"recovery", ST_FLOAT, offsetof(Settings, recovery), 0.3f, 3.f, nullptr, "how long feelings last (x)", "general"},
  {"brightness", ST_INT, offsetof(Settings, brightness), 10, 255, nullptr, "backlight", "general"},
  {"night_dim", ST_BOOL, offsetof(Settings, night_dim), 0, 1, nullptr, "dim when asleep or at night", "general"},
  {"sleep_after", ST_INT, offsetof(Settings, sleep_after), 0, 60, nullptr, "minutes idle before dozing (0 = never)", "general"},
  {"night_sleep", ST_BOOL, offsetof(Settings, night_sleep), 0, 1, nullptr, "doze after 1 min idle at night", "general"},
  {"clock_24h", ST_BOOL, offsetof(Settings, clock_24h), 0, 1, nullptr, "24-hour clock", "general"},
  {"temp_unit", ST_ENUM, offsetof(Settings, temp_unit), 0, 1, "C|F", "temperature unit", "general"},
  {"behaviours", ST_FLOAT, offsetof(Settings, behaviours), 0.f, 3.f, nullptr, "how often it does idle antics (0 = never)", "general"},
  {"learner", ST_BOOL, offsetof(Settings, learner), 0, 1, nullptr, "the mind net drives the face and learns what you like", "general"},
  {"panels", ST_BOOL, offsetof(Settings, panels), 0, 1, nullptr, "show clock / weather / CI corners", "general"},
  {"auto_emotes", ST_BOOL, offsetof(Settings, auto_emotes), 0, 1, nullptr, "react with emotes (hearts, !, ?, Zs...)", "general"},
  {"w_pet", ST_FLOAT, offsetof(Settings, ev_w[0]), 0, 2, nullptr, "pet: how much it moves the mood", "events"},
  {"w_poke", ST_FLOAT, offsetof(Settings, ev_w[1]), 0, 2, nullptr, "poke: how much it moves the mood", "events"},
  {"w_praise", ST_FLOAT, offsetof(Settings, ev_w[2]), 0, 2, nullptr, "praise: how much it moves the mood", "events"},
  {"w_scold", ST_FLOAT, offsetof(Settings, ev_w[3]), 0, 2, nullptr, "scold: how much it moves the mood", "events"},
  {"w_startle", ST_FLOAT, offsetof(Settings, ev_w[4]), 0, 2, nullptr, "startle: how much it moves the mood", "events"},
  {"w_win", ST_FLOAT, offsetof(Settings, ev_w[5]), 0, 2, nullptr, "win: how much it moves the mood", "events"},
  {"w_fail", ST_FLOAT, offsetof(Settings, ev_w[6]), 0, 2, nullptr, "fail: how much it moves the mood", "events"},
  {"w_greet", ST_FLOAT, offsetof(Settings, ev_w[7]), 0, 2, nullptr, "greet: how much it moves the mood", "events"},
  {"w_tired", ST_FLOAT, offsetof(Settings, ev_w[8]), 0, 2, nullptr, "tired: how much it moves the mood", "events"},
  {"w_annoy", ST_FLOAT, offsetof(Settings, ev_w[9]), 0, 2, nullptr, "annoy: how much it moves the mood", "events"},
  {"w_love", ST_FLOAT, offsetof(Settings, ev_w[10]), 0, 2, nullptr, "love: how much it moves the mood", "events"},
  {"w_confuse", ST_FLOAT, offsetof(Settings, ev_w[11]), 0, 2, nullptr, "confuse: how much it moves the mood", "events"},
  {"w_bore", ST_FLOAT, offsetof(Settings, ev_w[12]), 0, 2, nullptr, "bore: how much it moves the mood", "events"},
  {"x_neutral", ST_FLOAT, offsetof(Settings, expr_w[0]), 0, 2, nullptr, "neutral: how strongly it shows (0 = never)", "expressions"},
  {"x_happy", ST_FLOAT, offsetof(Settings, expr_w[1]), 0, 2, nullptr, "happy: how strongly it shows (0 = never)", "expressions"},
  {"x_sad", ST_FLOAT, offsetof(Settings, expr_w[2]), 0, 2, nullptr, "sad: how strongly it shows (0 = never)", "expressions"},
  {"x_angry", ST_FLOAT, offsetof(Settings, expr_w[3]), 0, 2, nullptr, "angry: how strongly it shows (0 = never)", "expressions"},
  {"x_surprised", ST_FLOAT, offsetof(Settings, expr_w[4]), 0, 2, nullptr, "surprised: how strongly it shows (0 = never)", "expressions"},
  {"x_sleepy", ST_FLOAT, offsetof(Settings, expr_w[5]), 0, 2, nullptr, "sleepy: how strongly it shows (0 = never)", "expressions"},
  {"x_excited", ST_FLOAT, offsetof(Settings, expr_w[6]), 0, 2, nullptr, "excited: how strongly it shows (0 = never)", "expressions"},
  {"x_content", ST_FLOAT, offsetof(Settings, expr_w[7]), 0, 2, nullptr, "content: how strongly it shows (0 = never)", "expressions"},
  {"x_scared", ST_FLOAT, offsetof(Settings, expr_w[8]), 0, 2, nullptr, "scared: how strongly it shows (0 = never)", "expressions"},
  {"x_bored", ST_FLOAT, offsetof(Settings, expr_w[9]), 0, 2, nullptr, "bored: how strongly it shows (0 = never)", "expressions"},
  {"x_skeptical", ST_FLOAT, offsetof(Settings, expr_w[10]), 0, 2, nullptr, "skeptical: how strongly it shows (0 = never)", "expressions"},
  {"x_confused", ST_FLOAT, offsetof(Settings, expr_w[11]), 0, 2, nullptr, "confused: how strongly it shows (0 = never)", "expressions"},
  {"x_smug", ST_FLOAT, offsetof(Settings, expr_w[12]), 0, 2, nullptr, "smug: how strongly it shows (0 = never)", "expressions"},
  {"squash_amt", ST_FLOAT, offsetof(Settings, squash_amt), 0, 2, nullptr, "squash and stretch", "motion"},
  {"motion_amt", ST_FLOAT, offsetof(Settings, motion_amt), 0, 2, nullptr, "idle drift and twitches", "motion"},
  {"gaze_amt", ST_FLOAT, offsetof(Settings, gaze_amt), 0, 2, nullptr, "how far it glances around", "motion"},
  {"wake_up", ST_BOOL, offsetof(Settings, wake_up), 0, 1, nullptr, "start asleep and wake up at power-on", "general"},
  {"head_turn", ST_FLOAT, offsetof(Settings, head_turn), 0.f, 35.f, nullptr, "the head turns like a box, driven by the mind net's gaze (degrees, 0 = off)", "general"},
  {"fisheye", ST_FLOAT, offsetof(Settings, fisheye), 0.f, 1.f, nullptr, "fisheye lens on the picture (0 = off, 1 = strong)", "general"},
  {"wardrobe", ST_BOOL, offsetof(Settings, wardrobe), 0, 1, nullptr, "it changes its own look now and then", "learner"},
  {"look_chance", ST_FLOAT, offsetof(Settings, look_chance), 0.f, 1.f, nullptr, "when it feels like a new look, how likely it really changes", "learner"},
  {"look_whole", ST_FLOAT, offsetof(Settings, look_whole), 0.f, 1.f, nullptr, "of those changes, how many are the whole look (the rest change only a part)", "learner"},
  {"drift", ST_FLOAT, offsetof(Settings, drift), 0.f, 3.f, nullptr, "how fast its temperament follows how it is treated (0 = off)", "learner"},
  {"mind_amount", ST_FLOAT, offsetof(Settings, mind_amount), 0.f, 2.f, nullptr, "how strongly the mind net drives the face", "learner"},
  {"learn_rate", ST_FLOAT, offsetof(Settings, learn_rate), 0.005f, 0.2f, nullptr, "how fast it learns from your reactions", "learner"},
  {"explore", ST_FLOAT, offsetof(Settings, explore), 0, 0.5f, nullptr, "how much it experiments (smoothly varied urges)", "learner"},
  {"r_pet", ST_FLOAT, offsetof(Settings, r_pet), -1, 1, nullptr, "reward when you pet it", "learner"},
  {"r_praise", ST_FLOAT, offsetof(Settings, r_praise), -1, 1, nullptr, "reward when you praise it", "learner"},
  {"r_love", ST_FLOAT, offsetof(Settings, r_love), -1, 1, nullptr, "reward when you love it", "learner"},
  {"r_greet", ST_FLOAT, offsetof(Settings, r_greet), -1, 1, nullptr, "reward when you greet it", "learner"},
  {"r_poke", ST_FLOAT, offsetof(Settings, r_poke), -1, 1, nullptr, "reward when you poke it", "learner"},
  {"r_annoy", ST_FLOAT, offsetof(Settings, r_annoy), -1, 1, nullptr, "reward when you annoy it", "learner"},
  {"r_scold", ST_FLOAT, offsetof(Settings, r_scold), -1, 1, nullptr, "reward when you scold it", "learner"},
  {"motion", ST_BOOL, offsetof(Settings, motion), 0, 1, nullptr, "use the motion sensor, if the board has one", "sensor"},
  {"motion_sens", ST_FLOAT, offsetof(Settings, motion_sens), 0.3f, 3.f, nullptr, "how readily it reacts to motion", "sensor"},
  {"auto_rotate", ST_BOOL, offsetof(Settings, auto_rotate), 0, 1, nullptr, "flip the picture when turned upside down", "sensor"},
  {"screen_off", ST_INT, offsetof(Settings, screen_off), 0, 120, nullptr, "minutes idle before the screen goes dark (0 = never)", "general"},
  {"hearing", ST_BOOL, offsetof(Settings, hearing), 0, 1, nullptr, "react to sound: claps, talking (only loudness, nothing recorded)", "sound"},
  {"hearing_sens", ST_FLOAT, offsetof(Settings, hearing_sens), 0.3f, 3.f, nullptr, "how readily it hears", "sound"},
  {"sound", ST_BOOL, offsetof(Settings, sound), 0, 1, nullptr, "chirps and trills when it feels something (never while asleep)", "sound"},
  {"volume", ST_FLOAT, offsetof(Settings, volume), 0, 1, nullptr, "how loud its voice is", "sound"},
  {"level", ST_FLOAT, offsetof(Settings, level), 0, 1, nullptr, "the face stays level when turned round (0 = off)", "sensor"},
  {"parallax", ST_FLOAT, offsetof(Settings, parallax), 0, 2, nullptr, "the eyes and mouth move with the tilt, the eyes more (3D without the turn); 0 = still", "sensor"},
  {"dark_mode", ST_BOOL, offsetof(Settings, dark_mode), 0, 1, nullptr, "light only every second pixel (AMOLED board: darker, saves power and the panel)", "general"},
};
static constexpr int SETTING_COUNT = sizeof(SETTING_DEFS) / sizeof(SETTING_DEFS[0]);

static inline size_t settingSize(SettingType t) { return t == ST_FLOAT ? sizeof(float) : 1; }

// Loads a saved blob of `len` bytes: a prefix of Settings; fields added since keep their defaults.
static inline void settingsLoad(Settings& s, const void* blob, size_t len);

static inline bool settingOption(const char* options, int i, char* out, size_t n) {
  for (const char* p = options; p && *p; i--) {
    const char* bar = strchr(p, '|');
    size_t len = bar ? (size_t)(bar - p) : strlen(p);
    if (i == 0) {
      if (len >= n) len = n - 1;
      memcpy(out, p, len);
      out[len] = 0;
      return true;
    }
    p = bar ? bar + 1 : nullptr;
  }
  return false;
}

static inline const SettingDef* settingDef(const char* key) {
  for (const SettingDef& d : SETTING_DEFS)
    if (!strcmp(d.key, key)) return &d;
  return nullptr;
}

static inline void settingGet(const Settings& s, const SettingDef& d, char* out, size_t n) {
  const uint8_t* base = (const uint8_t*)&s + d.offset;
  switch (d.type) {
    case ST_INT: snprintf(out, n, "%u", (unsigned)*base); break;
    case ST_FLOAT: snprintf(out, n, "%.2f", *(const float*)base); break;
    case ST_BOOL: snprintf(out, n, "%d", *base ? 1 : 0); break;
    case ST_ENUM:
      if (!settingOption(d.options, *base, out, n)) snprintf(out, n, "%u", (unsigned)*base);
      break;
  }
}

static inline bool settingSet(Settings& s, const char* key, const char* val) {
  const SettingDef* d = settingDef(key);
  if (!d || !val || !*val) return false;
  uint8_t* base = (uint8_t*)&s + d->offset;
  char* end;
  float f = strtof(val, &end);
  bool numeric = end != val;
  if (d->type == ST_BOOL) {
    if (!strcasecmp(val, "on") || !strcasecmp(val, "true") || !strcasecmp(val, "yes")) f = 1, numeric = true;
    if (!strcasecmp(val, "off") || !strcasecmp(val, "false") || !strcasecmp(val, "no")) f = 0, numeric = true;
  }
  if (d->type == ST_ENUM && !numeric) {
    char opt[24];
    for (int i = 0; settingOption(d->options, i, opt, sizeof opt); i++)
      if (!strcasecmp(opt, val)) f = (float)i, numeric = true;
  }
  if (!numeric || !isfinite(f)) return false;  // "nan" / "inf" parse as numbers
  f = f < d->min ? d->min : (f > d->max ? d->max : f);
  switch (d->type) {
    case ST_INT:
    case ST_ENUM: *base = (uint8_t)(f + 0.5f); break;
    case ST_FLOAT: *(float*)base = f; break;
    case ST_BOOL: *base = f != 0; break;
  }
  return true;
}

static inline size_t settingsJson(const Settings& s, char* out, size_t n, bool meta) {
  static const char* const TYPES[] = {"int", "float", "bool", "enum"};
  size_t k = snprintf(out, n, "{\"settings\":{");
  for (int i = 0; i < SETTING_COUNT && k < n; i++) {
    char v[24];
    settingGet(s, SETTING_DEFS[i], v, sizeof v);
    bool str = SETTING_DEFS[i].type == ST_ENUM;
    k += snprintf(out + k, n - k, "%s\"%s\":%s%s%s", i ? "," : "", SETTING_DEFS[i].key, str ? "\"" : "", v,
                  str ? "\"" : "");
  }
  if (k < n) k += snprintf(out + k, n - k, "}");
  if (meta) {
    if (k < n) k += snprintf(out + k, n - k, ",\"meta\":[");
    for (int i = 0; i < SETTING_COUNT && k < n; i++) {
      const SettingDef& d = SETTING_DEFS[i];
      k += snprintf(
          out + k, n - k,
          "%s{\"key\":\"%s\",\"type\":\"%s\",\"min\":%g,\"max\":%g,\"options\":\"%s\",\"help\":\"%s\",\"group\":\"%s\"}",
          i ? "," : "", d.key, TYPES[d.type], d.min, d.max, d.options ? d.options : "", d.help, d.group);
    }
    if (k < n) k += snprintf(out + k, n - k, "]");
  }
  if (k < n) k += snprintf(out + k, n - k, "}");
  return k < n ? k : n - 1;
}

static inline void settingsLoad(Settings& s, const void* blob, size_t len) {
  s = settingsDefaults();
  if (len < sizeof(uint32_t)) return;
  Settings saved = settingsDefaults();
  memcpy(&saved, blob, len < sizeof saved ? len : sizeof saved);
  if (saved.version < 1 || saved.version > SETTINGS_VERSION) return;
  for (const SettingDef& d : SETTING_DEFS)
    if (d.offset + settingSize(d.type) <= len) {
      // Copied value by value and clamped: a damaged blob must not load NaN, a bool byte other than 0/1 or an enum past its options.
      uint8_t* dst = (uint8_t*)&s + d.offset;
      const uint8_t* src = (const uint8_t*)&saved + d.offset;
      if (d.type == ST_FLOAT) {
        float f;
        memcpy(&f, src, sizeof f);
        if (f == f) *(float*)dst = f < d.min ? d.min : (f > d.max ? d.max : f);  // NaN keeps the default
      } else if (d.type == ST_BOOL)
        *dst = *src != 0;
      else
        *dst = *src < d.min ? (uint8_t)d.min : (*src > d.max ? (uint8_t)d.max : *src);
    }
  if (saved.version == 1) s.auto_emotes = true;  // v1 had padding where auto_emotes is now
  s.version = SETTINGS_VERSION;
}
