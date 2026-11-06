#pragma once
#include <math.h>
#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Customisable look. Owned by core 0 (Serial/WS/NVS), copied to the renderer and springs on core 1.
// One table (STYLE_DEFS) describes every option, so the console, protocol, web page and NVS loading are generic.
// Geometry is clamped so features always stay inside their redraw regions. No Arduino deps.
struct FaceStyle {
    float hue, sat, val;
    uint8_t bg[3];
    float eye_w, eye_h;
    float eye_round;
    float mouth_w;
    float line_r;
    bool tint;
    bool blush;
    bool brows;
    bool pupils;
    bool hollow;
    uint8_t mouth_style;
    // New fields go at the end: a saved style is a prefix of this struct, missing fields load as defaults.
    float eye_gap;
    float eye_y;
    float mouth_y;
    float pupil_size;
    float glow;
    bool lashes;
    bool tongue;
    float lid;
    float speed;
    float bounce;
    bool glint;
    float pixel;
    float glitch;
    uint8_t pupil_style;
    float daytint;
    uint8_t eye_style;
    bool mouth_solid;
    bool eye_split;
    uint8_t eye_style_r;
    uint8_t pupil_style_r;
    float eye_w_r, eye_h_r;
    float lid_r;
    float hue_r;
    bool brows_r;
    uint8_t pupil_ink;
    float rgb_split;
};

inline const char* const MOUTH_STYLES[] = {"curve",  "cat", "none", "wave",  "zigzag",
                                           "matrix", "bar", "dot",  "fangs", "teeth"};
constexpr int MOUTH_STYLE_COUNT = sizeof MOUTH_STYLES / sizeof MOUTH_STYLES[0];
constexpr int MOUTH_CHOICES = MOUTH_STYLE_COUNT - 1;
static inline uint8_t mouthChoiceStyle(int c) { return (uint8_t)(c - 1 < 2 ? c - 1 : c); }
#define MOUTH_OPTIONS "curve|cat|none|wave|zigzag|matrix|bar|dot|fangs|teeth"
#define EYE_OPTIONS "solid|outline|bars|dots|spiral|sparkle|cross"
#define EYE_COUNT 7
#define PUPIL_OPTIONS "none|round|slit|heart|star|square|outline|cross|diamond|moon"
#define PUPIL_COUNT 10
#define PUPIL_INK_OPTIONS "dark|white|face"
#define PUPIL_INK_COUNT 3

inline const FaceStyle STYLE_DEFAULT = {
    357, 0.66f, 0.95f, {0, 0, 0}, 60,    84, 0.43f, 40,    3.0f, false, true, false, false, false,
    0,   130,   95,    192,       0.22f, 0,  false, false, 0,    1,     0.5f, false, 0,     0,
    1,   0,     0,     false,     false, 0,  1,     60,    84,   0,     0,    false, 0,     0};

enum StyleType : uint8_t { SF_FLOAT, SF_BOOL, SF_ENUM, SF_RGB };

struct StyleDef {
    const char* key;
    StyleType type;
    size_t offset;
    float min, max;
    const char* help;
    const char* options = nullptr;
};

#define SDEF(key, field, type, lo, hi, help) {key, type, offsetof(FaceStyle, field), lo, hi, help}
#define SDEFE(key, field, count, help, opts)                                                                 \
  {key, SF_ENUM, offsetof(FaceStyle, field), 0, (count) - 1, help, opts}
inline const StyleDef STYLE_DEFS[] = {
  SDEF("hue", hue, SF_FLOAT, 0, 359, "colour"),
  SDEF("sat", sat, SF_FLOAT, 0, 1, "colour strength"),
  SDEF("val", val, SF_FLOAT, 0.2f, 1, "face brightness"),
  SDEF("bg", bg, SF_RGB, 0, 255, "background"),
  SDEF("eye_w", eye_w, SF_FLOAT, 30, 68, "eye width"),
  SDEF("eye_h", eye_h, SF_FLOAT, 30, 90, "eye height"),
  SDEFE("eye", eye_style, EYE_COUNT, "eye: solid, outline, bars, dots, spiral, sparkle, cross", EYE_OPTIONS),
  SDEFE("pupil", pupil_style, PUPIL_COUNT, "pupil: none, round, slit, heart, star, square, outline (hollow eye), cross, diamond, moon", PUPIL_OPTIONS),
  SDEF("eye_split", eye_split, SF_BOOL, 0, 1, "separate settings for the right eye"),
  SDEFE("eye_r", eye_style_r, EYE_COUNT, "right eye: body (when split)", EYE_OPTIONS),
  SDEFE("pupil_r", pupil_style_r, PUPIL_COUNT, "right eye: pupil (when split)", PUPIL_OPTIONS),
  SDEF("eye_w_r", eye_w_r, SF_FLOAT, 30, 68, "right eye width (when split)"),
  SDEF("eye_h_r", eye_h_r, SF_FLOAT, 30, 90, "right eye height (when split)"),
  SDEF("lid_r", lid_r, SF_FLOAT, 0, 0.6f, "right eye resting lid (when split)"),
  SDEF("hue_r", hue_r, SF_FLOAT, -180, 180, "right eye colour shift in degrees (when split)"),
  SDEF("brows_r", brows_r, SF_BOOL, 0, 1, "right eye brow (when split)"),
  SDEFE("pupil_ink", pupil_ink, PUPIL_INK_COUNT, "pupil colour: dark, white, or the face colour", PUPIL_INK_OPTIONS),
  SDEF("round", eye_round, SF_FLOAT, 0, 1, "eye roundness"),
  SDEF("eye_gap", eye_gap, SF_FLOAT, 112, 150, "eye spacing"),
  SDEF("eye_y", eye_y, SF_FLOAT, 80, 103, "eye position (up/down)"),
  SDEF("pupil_size", pupil_size, SF_FLOAT, 0.12f, 0.38f, "pupil size"),
  SDEF("glint", glint, SF_BOOL, 0, 1, "white glint in the pupils"),
  SDEF("glow", glow, SF_FLOAT, 0, 1, "soft glow behind the whole face"),
  SDEF("lid", lid, SF_FLOAT, 0, 0.6f, "resting eyelid"),
  SDEF("lashes", lashes, SF_BOOL, 0, 1, "eyelashes"),
  SDEF("brows", brows, SF_BOOL, 0, 1, "eyebrows"),
  SDEFE("mouth", mouth_style, MOUTH_STYLE_COUNT, "mouth style", MOUTH_OPTIONS),
  SDEF("mouth_solid", mouth_solid, SF_BOOL, 0, 1, "solid mouth: the inside of an open mouth has the line's colour"),
  SDEF("mouth_w", mouth_w, SF_FLOAT, 14, 46, "mouth width"),
  SDEF("mouth_y", mouth_y, SF_FLOAT, 180, 196, "mouth position (up/down)"),
  SDEF("line", line_r, SF_FLOAT, 1.5f, 5, "line thickness"),
  SDEF("blush", blush, SF_BOOL, 0, 1, "blush"),
  SDEF("tint", tint, SF_BOOL, 0, 1, "colour follows mood"),
  SDEF("speed", speed, SF_FLOAT, 0.5f, 2, "motion speed"),
  SDEF("bounce", bounce, SF_FLOAT, 0, 1, "motion bounce"),
  SDEF("pixel", pixel, SF_FLOAT, 0, 8, "pixelate the whole face (block size in px, 0 = off)"),
  SDEF("daytint", daytint, SF_FLOAT, 0, 1, "colour follows the time of day (warm at dusk, dim and cool at night)"),
  SDEF("glitch", glitch, SF_FLOAT, 0, 10, "picture glitches: 0 off (always), 1 occasional .. 10 constant chaos; the mind net can raise it"),
  SDEF("split", rgb_split, SF_FLOAT, 0, 8, "RGB split: red slides left, blue right, by this many px (0 = off)"),
};
#undef SDEF
static constexpr int STYLE_DEF_COUNT = sizeof(STYLE_DEFS) / sizeof(STYLE_DEFS[0]);

static inline size_t styleFieldSize(StyleType t) {
  return t == SF_FLOAT ? sizeof(float) : t == SF_RGB ? 3 : 1;
}

struct StylePreset {
    const char* name;
    const char* changes;
};
inline const StylePreset STYLE_PRESETS[] = {
  {"TeeVee", "sat 0.5 eye_w 30 eye_h 80 round 1 eye_gap 112 eye_y 91 blush 0 eye solid pupil none mouth curve mouth_solid 1 speed 1.75 pixel 3 glitch 0.5 split 0.5"},
  {"round", "hue 190 sat 0.6 val 1 eye_w 64 eye_h 64 round 1 mouth_w 30 tint 1 "},
  {"visor", "hue 140 sat 0.8 eye_w 68 eye_h 40 round 0.25 mouth_w 46 line 2 blush 0 eye outline glow 0.7"},
  {"ghost", "hue 220 sat 0.2 bg 10 12 30 eye_w 50 eye_h 76 round 1 mouth_w 28 line 2.5 tint 1 blush 0 eye outline glow 0.4"},
  {"classic", "hue 40 sat 0.3 val 1 bg 16 18 28 eye_w 52 eye_h 66 round 1 mouth_w 34 tint 1 brows 1 "},
  {"anime", "hue 200 sat 0.5 val 1 bg 20 22 36 eye_w 50 eye_h 78 round 0.9 mouth_w 18 line 2.5 tint 1 "
            "pupil_size 0.34 lashes 1 eye_gap 140 eye_y 98 mouth_y 188"},
  {"robot", "hue 30 sat 0.9 val 1 eye_w 44 eye_h 44 round 0.15 mouth_w 40 line 2 blush 0 glow 1 speed 1.6 bounce 0.1"},
  {"monster", "hue 110 sat 0.8 val 1 bg 8 16 8 eye_w 56 eye_h 40 round 0.4 mouth_w 40 line 3 blush 0 brows 1 mouth fangs mouth_solid 1 glitch 1"},
  {"retro", "hue 125 sat 0.9 val 1 bg 0 12 0 eye_w 40 eye_h 52 round 0.1 mouth_w 36 line 3 blush 0 glow 0.6 mouth matrix pixel 4 glitch 2"},
  {"cat", "hue 85 sat 0.8 val 1 eye_w 58 eye_h 62 round 1 mouth_w 22 line 3 mouth cat eye slit pupil_size 0.3 tint 1 blush 0"},
  {"sweet", "hue 340 sat 0.55 val 1 bg 20 6 18 eye_w 56 eye_h 66 round 1 mouth_w 22 line 3.5 mouth cat eye heart pupil_size 0.3 glint 1 lashes 1 tint 1"},
  {"neon", "hue 300 sat 1 val 1 bg 6 0 14 eye_w 46 eye_h 60 round 0.5 glow 1 line 2.5 mouth_w 30 blush 0 brows 0 glitch 2"},
  {"noir", "hue 0 sat 0 val 0.95 eye_w 40 eye_h 72 round 0.3 eye outline line 2 mouth_w 30 blush 0 brows 1"},
  {"sunset", "hue 20 sat 0.85 val 1 bg 20 6 4 eye_w 58 eye_h 46 round 0.8 lid 0.25 mouth_w 36 tint 1 daytint 1 mouth_solid 1"},
  {"alien", "hue 120 sat 0.9 val 1 eye_w 68 eye_h 44 round 1 eye_gap 150 eye_y 100 eye slit pupil_size 0.34 mouth dot mouth_w 16 line 3 blush 0 pixel 2"},
  {"grump", "hue 5 sat 0.9 val 1 eye_w 62 eye_h 34 round 0.2 lid 0.3 brows 1 eye square pupil_size 0.28 mouth bar mouth_w 34 blush 0"},
  {"sparkle", "hue 50 sat 0.6 val 1 bg 14 10 4 eye_w 54 eye_h 70 round 1 eye star pupil_size 0.28 lashes 1 mouth cat mouth_w 22 glow 0.3 blush 1"},
  {"doll", "hue 350 sat 0.4 val 1 bg 24 14 18 eye_w 60 eye_h 66 round 1 eye_gap 140 eye solid pupil_size 0.28 glint 1 lashes 1 mouth dot mouth_w 16 line 3.5 blush 1"},
  {"vampire", "hue 350 sat 0.9 val 1 bg 8 0 4 eye_w 44 eye_h 54 round 0.6 eye slit pupil_size 0.3 mouth fangs mouth_w 30 brows 1 blush 0"},
  {"dreamy", "hue 270 sat 0.5 val 1 bg 8 6 20 eye_w 56 eye_h 58 round 1 lid 0.35 eye solid pupil_size 0.26 glint 1 glow 0.6 mouth cat mouth_w 20 speed 0.7 bounce 0.3 tint 1 daytint 1"},
  {"hypno", "hue 280 sat 0.8 val 1 bg 8 4 16 eye_w 60 eye_h 60 round 1 eye spiral pupil none eye_gap 130 mouth dot mouth_w 18 line 3 blush 0 glow 0.4"},
  {"starry", "hue 215 sat 0.7 val 1 bg 4 6 20 eye_w 56 eye_h 68 round 1 eye sparkle pupil none mouth cat mouth_w 22 blush 1 tint 1 glow 0.5"},
  {"matrix", "hue 130 sat 0.9 val 1 bg 0 6 0 eye_w 48 eye_h 62 round 0.8 eye dots mouth bar mouth_w 34 line 2.5 blush 0"},
  {"scan", "hue 195 sat 0.8 val 1 bg 0 4 8 eye_w 50 eye_h 64 round 0.7 eye bars mouth bar mouth_w 34 line 2.5 blush 0 glow 0.3"},
  // New looks (appended, so the older presets keep their numbers)
  {"moonlit", "hue 230 sat 0.5 val 1 bg 6 4 26 eye_w 48 eye_h 72 round 1 eye solid pupil moon pupil_size 0.3 glint 1 mouth curve mouth_w 26 line 2.5 blush 0 glow 0.9 speed 0.8 bounce 0.2 tint 1"},
  {"diamonds", "hue 185 sat 0.6 val 1 bg 2 14 22 eye_w 52 eye_h 60 round 0.6 eye solid pupil diamond pupil_size 0.3 glint 1 mouth curve mouth_w 26 line 3 blush 0"},
  {"lopsided", "hue 300 sat 0.7 val 1 bg 10 4 18 eye_w 52 eye_h 70 round 0.8 eye_split 1 eye_w_r 36 eye_h_r 56 eye_r sparkle pupil_r square lid_r 0.3 hue_r 60 brows 1 brows_r 0 mouth curve mouth_w 30 line 3 blush 0 glow 0.5"},
  {"twins", "hue 200 sat 0.6 val 1 bg 4 10 16 eye_w 46 eye_h 64 round 1 eye_split 1 eye_r solid pupil moon pupil_r moon hue_r 180 mouth cat mouth_w 24 line 3 blush 0 glow 0.3"},
  {"crosshair", "hue 0 sat 0 val 0.95 bg 0 0 0 eye_w 44 eye_h 44 round 0 eye cross pupil cross pupil_ink 1 mouth bar mouth_w 30 line 2.5 blush 0 glitch 1"},
  {"inkwell", "hue 300 sat 0.9 val 0.95 bg 20 4 12 eye_w 56 eye_h 68 round 0.7 eye outline pupil round pupil_ink 2 mouth fangs mouth_w 30 line 2.5 blush 0 brows 1 glow 0.4"},
  {"pirate", "hue 30 sat 0.7 val 1 bg 12 8 4 eye_w 56 eye_h 64 round 1 eye solid pupil round pupil_size 0.28 glint 1 eye_split 1 eye_r solid pupil_r none "
             "eye_w_r 50 eye_h_r 40 lid_r 0.6 brows_r 1 mouth teeth mouth_w 34 line 3 blush 0"},
  {"owl", "hue 35 sat 0.6 val 1 bg 14 10 6 eye_w 66 eye_h 66 round 1 eye_gap 136 eye outline pupil round pupil_ink 2 pupil_size 0.36 brows 1 mouth dot mouth_w 14 "
          "line 3.5 blush 0 lid 0.1"},
  {"vapor", "hue 290 sat 0.7 val 1 bg 24 6 34 eye_w 52 eye_h 60 round 0.5 eye bars mouth wave mouth_w 32 line 2.5 blush 0 glow 0.8 split 2 daytint 1"},
  {"static", "hue 180 sat 0.15 val 1 bg 2 2 4 eye_w 48 eye_h 58 round 0 eye solid pupil none mouth zigzag mouth_w 36 line 2.5 blush 0 pixel 2 glitch 3 split 3"},
  {"bashful", "hue 15 sat 0.5 val 1 bg 20 10 10 eye_w 40 eye_h 50 round 1 eye solid pupil round pupil_size 0.2 glint 1 lid 0.2 mouth dot mouth_w 14 line 3 blush 1 "
              "tint 1 speed 0.8"},
  // Added 2026-10-31 because nothing else looked like them. A new look earns its place: it must not be another one in a new colour.
  {"paper", "hue 260 sat 0.35 val 0.3 bg 228 220 207 eye_w 50 eye_h 64 round 1 eye solid pupil none mouth curve mouth_w 30 line 3 blush 0"},
  {"goof", "hue 95 sat 0.55 val 1 bg 6 10 4 eye_w 30 eye_h 30 round 1 eye_gap 150 eye_y 88 eye solid pupil none mouth teeth mouth_w 46 line 3 blush 0 bounce 0.9"},
  {"deadpan", "hue 210 sat 0.1 val 0.9 bg 12 12 14 eye_w 68 eye_h 30 round 1 lid 0.5 pupil none mouth bar mouth_w 20 line 2 blush 0 speed 0.6 bounce 0"},
  {"stare", "hue 175 sat 0.7 val 1 bg 0 8 10 eye_w 68 eye_h 90 round 1 eye_gap 150 pupil round pupil_size 0.12 mouth dot mouth_w 14 line 2 blush 0"},
  {"lypa", "hue 106 sat 0.48 val 0.904 bg 150 49 89 eye_w 68 eye_h 86 eye solid pupil slit eye_split 0 eye_r solid pupil_r round eye_w_r 63 eye_h_r 90 lid_r 0 hue_r 0 brows_r 0 pupil_ink white round 0.74 eye_gap 150 eye_y 103 pupil_size 0.281 glint 0 glow 0.83 lid 0 lashes 1 brows 1 mouth wave mouth_solid 1 mouth_w 46 mouth_y 180 line 1.99 blush 1 tint 1 speed 1.52 bounce 0.55 pixel 3 daytint 0 glitch 6"},  // the owner's own, from their share link
};
static constexpr int STYLE_PRESET_COUNT = sizeof(STYLE_PRESETS) / sizeof(STYLE_PRESETS[0]);

static constexpr int STYLE_PRESETS_OLD_COUNT = 44;
inline const char* const STYLE_PRESETS_OLD[STYLE_PRESETS_OLD_COUNT] = {
    "TeeVee",  "round",   "cute",     "visor",   "ghost",   "classic",  "anime",    "robot",  "sleepy",
    "monster", "retro",   "cat",      "sweet",   "neon",    "mint",     "noir",     "sunset", "alien",
    "bubble",  "grump",   "sparkle",  "doll",    "vampire", "dreamy",   "crt",      "hypno",  "starry",
    "arcade",  "matrix",  "scan",     "crossed", "moonlit", "diamonds", "lopsided", "twins",  "crosshair",
    "sunbeam", "inkwell", "anaglyph", "pirate",  "owl",     "vapor",    "static",   "bashful"};

static inline bool styleNameEq(const char* a, const char* b) {
  for (;; a++, b++) {
    const char x = *a >= 'A' && *a <= 'Z' ? (char)(*a + 32) : *a,
               y = *b >= 'A' && *b <= 'Z' ? (char)(*b + 32) : *b;
    if (x != y) return false;
    if (!x) return true;
  }
}
static inline int styleFindPreset(const char* name) {
  for (int i = 0; i < STYLE_PRESET_COUNT; i++)
    if (styleNameEq(STYLE_PRESETS[i].name, name)) return i;
  return -1;
}
static inline int styleOldPreset(int oldIndex) {
  if (oldIndex < 0 || oldIndex >= STYLE_PRESETS_OLD_COUNT) return -1;
  for (int i = 0; i < STYLE_PRESET_COUNT; i++)
    if (!strcmp(STYLE_PRESETS[i].name, STYLE_PRESETS_OLD[oldIndex])) return i;
  return -1;
}

static inline bool styleEnumName(const char* opts, int idx, char* out, size_t n) {
  for (int i = 0; *opts; i++) {
    const char* end = strchr(opts, '|');
    size_t len = end ? (size_t)(end - opts) : strlen(opts);
    if (i == idx) {
      snprintf(out, n, "%.*s", (int)len, opts);
      return true;
    }
    opts += len + (end ? 1 : 0);
  }
  return false;
}
static inline int styleEnumIndex(const char* opts, const char* name) {
  const size_t want = strlen(name);
  for (int i = 0; *opts; i++) {
    const char* end = strchr(opts, '|');
    size_t len = end ? (size_t)(end - opts) : strlen(opts);
    if (len == want && !strncmp(opts, name, len)) return i;
    opts += len + (end ? 1 : 0);
  }
  return -1;
}

static inline float styleClamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static inline const StyleDef* styleDef(const char* key) {
  for (const StyleDef& d : STYLE_DEFS)
    if (!strcmp(d.key, key)) return &d;
  return nullptr;
}

static inline bool styleSet(FaceStyle& s, const char* key, const char* val) {
  if (val && *val && (!strcmp(key, "pupils") || !strcmp(key, "hollow"))) {
    const bool on = strcmp(val, "0") && strcmp(val, "off") && strcmp(val, "false") && strcmp(val, "no");
    if (key[0] == 'h') {
      if (on)
        s.eye_style = 1;
      else if (s.eye_style == 1)
        s.eye_style = 0;
    }
    return true;
  }
  if (val && *val && !strcmp(key, "eye")) {
    static const char* const shapes[] = {"slit", "heart", "star", "square"};
    for (int i = 0; i < 4; i++)
      if (!strcmp(val, shapes[i])) {
        s.eye_style = 0;
        s.pupil_style = (uint8_t)(i + 2);
        return true;
      }
  }
  const StyleDef* d = styleDef(key);
  if (!d || !val || !*val) return false;
  uint8_t* base = (uint8_t*)&s + d->offset;
  if (d->type == SF_RGB) {
    int c[3];
    if (sscanf(val, "%d %d %d", &c[0], &c[1], &c[2]) != 3) return false;
    for (int i = 0; i < 3; i++) base[i] = (uint8_t)styleClamp(c[i], 0, 255);
    return true;
  }
  float f;
  char* end;
  f = strtof(val, &end);
  bool numeric = end != val;
  if (d->type == SF_BOOL && !numeric) {
    if (!strcmp(val, "on") || !strcmp(val, "true") || !strcmp(val, "yes")) f = 1, numeric = true;
    if (!strcmp(val, "off") || !strcmp(val, "false") || !strcmp(val, "no")) f = 0, numeric = true;
  }
  if (d->type == SF_ENUM && !numeric && d->options) {
    const int idx = styleEnumIndex(d->options, val);
    if (idx >= 0) f = (float)idx, numeric = true;
  }
  if (!numeric || !isfinite(f)) return false;  // "nan" / "inf" parse as numbers
  if (d->key[0] == 'h' && !strcmp(d->key, "hue")) {  // hue wraps instead of clamping
    f = f - 360.f * (int)(f / 360.f);
    if (f < 0) f += 360.f;
  }
  f = styleClamp(f, d->min, d->max);
  if (d->type == SF_FLOAT)
    *(float*)base = f;
  else if (d->type == SF_BOOL)
    *base = f != 0;
  else
    *base = (uint8_t)(f + 0.5f);
  return true;
}

static inline int styleApplyText(FaceStyle& s, const char* text) {
  int n = 0;
  char key[16], val[24];
  while (*text) {
    int used = 0;
    if (sscanf(text, " %15s%n", key, &used) != 1) break;
    text += used;
    const StyleDef* d = styleDef(key);
    if (d && d->type == SF_RGB) {
      int r, g, b;
      if (sscanf(text, " %d %d %d%n", &r, &g, &b, &used) != 3) return -1;
      snprintf(val, sizeof val, "%d %d %d", r, g, b);
    } else if (sscanf(text, " %23s%n", val, &used) != 1) {
      return -1;
    }
    text += used;
    if (!styleSet(s, key, val)) return -1;
    n++;
  }
  return n;
}

// The looks of the year. Kept apart from STYLE_PRESETS: the look net and the wardrobe size themselves by that table, so a new row there resets what they learned.
inline const StylePreset STYLE_SEASONS[] = {
  {"winter", "hue 200 sat 0.35 val 1 bg 8 12 26 eye_w 54 eye_h 70 round 1 mouth_w 24 line 3 blush 0 glow 0.3 glint 1"},
  {"spring", "hue 120 sat 0.5 val 1 bg 4 18 12 eye_w 56 eye_h 72 round 1 mouth_w 26 line 3 tint 1 lashes 1 glint 1"},
  {"summer", "hue 40 sat 0.85 val 1 bg 26 16 4 eye_w 60 eye_h 66 round 0.8 mouth_w 34 line 4 eye sparkle glow 0.25"},
  {"autumn", "hue 22 sat 0.75 val 0.95 bg 24 12 6 eye_w 58 eye_h 60 round 0.6 mouth_w 28 line 3 lid 0.12 daytint 0.5"},
  {"halloween", "hue 28 sat 1 val 1 bg 10 2 18 eye_w 60 eye_h 74 round 0.35 eye outline pupil slit mouth fangs mouth_w 34 line 3 blush 0 glow 0.6"},
  {"newyear", "hue 50 sat 0.8 val 1 bg 8 8 30 eye_w 58 eye_h 72 round 1 eye sparkle mouth_w 34 line 3 glow 0.5 glint 1"},
};
static inline const char* styleSeasonName(int month, int day) {
  if ((month == 10 && day >= 20) || (month == 11 && day == 1)) return "halloween";
  if ((month == 12 && day >= 25) || (month == 1 && day <= 2)) return "newyear";
  return month == 12 || month <= 2 ? "winter" : month <= 5 ? "spring" : month <= 8 ? "summer" : "autumn";
}

static inline bool styleApplyPreset(FaceStyle& s, const char* name) {
  const int found = styleFindPreset(name);
  if (found >= 0) {
    s = STYLE_DEFAULT;
    styleApplyText(s, STYLE_PRESETS[found].changes);
    return true;
  }
  for (const StylePreset& p : STYLE_SEASONS)
    if (styleNameEq(p.name, name)) {
      s = STYLE_DEFAULT;
      styleApplyText(s, p.changes);
      return true;
    }
  return false;
}

static inline void styleSanitize(FaceStyle& s);
static inline uint32_t styleRnd(uint32_t& r) {
  r ^= r << 13;
  r ^= r >> 17;
  r ^= r << 5;
  return r;
}
static inline void styleRandomize(FaceStyle& s, uint32_t& rng) {
  if (!rng) rng = 0x9E3779B9u;
  auto f = [&](float a, float b) { return a + (b - a) * (styleRnd(rng) >> 8) * (1.f / 16777216.f); };
  auto chance = [&](float p) { return f(0, 1) < p; };
  const FaceStyle keep = s;
  s = STYLE_DEFAULT;
  s.pixel = keep.pixel;
  s.glitch = keep.glitch;
  s.daytint = keep.daytint;
  s.rgb_split = keep.rgb_split;
  s.speed = keep.speed;
  s.bounce = keep.bounce;
  s.hue = f(0, 359);
  s.sat = chance(0.15f) ? f(0.1f, 0.3f) : f(0.4f, 1.f);
  s.val = f(0.9f, 1.f);
  if (chance(0.5f)) {
    const float k = f(0.04f, 0.1f);
    s.bg[0] = (uint8_t)(255 * k * (1 + sinf(s.hue * 0.01745f)) * 0.5f);
    s.bg[1] = (uint8_t)(255 * k * (1 + sinf(s.hue * 0.01745f + 2.09f)) * 0.5f);
    s.bg[2] = (uint8_t)(255 * k * (1 + sinf(s.hue * 0.01745f + 4.19f)) * 0.5f);
  }
  s.eye_w = f(32, 66);
  s.eye_h = f(34, 86);
  s.eye_round = chance(0.55f) ? 1.f : f(0.1f, 0.9f);
  s.eye_gap = f(114, 146);
  s.eye_y = f(88, 100);
  s.mouth_style = chance(0.1f) ? 2 : (uint8_t)(styleRnd(rng) % MOUTH_STYLE_COUNT);
  if (s.mouth_style == 2 && chance(0.7f)) s.mouth_style = 0;
  s.mouth_w = f(16, 40);
  s.mouth_y = f(184, 194);
  s.line_r = f(2, 4);
  s.mouth_solid = chance(0.25f);
  s.eye_style = chance(0.5f) ? (uint8_t)(styleRnd(rng) % EYE_COUNT) : 0;
  s.pupil_style = chance(0.5f) ? (uint8_t)(styleRnd(rng) % PUPIL_COUNT) : 1;
  s.pupil_size = f(0.2f, 0.34f);
  s.glint = chance(0.5f);
  s.glow = chance(0.3f) ? f(0.2f, 0.9f) : 0.f;
  s.lashes = chance(0.25f);
  s.brows = chance(0.25f);
  s.blush = chance(0.5f);
  s.tint = chance(0.5f);
  s.lid = chance(0.3f) ? f(0.1f, 0.35f) : 0.f;
  styleSanitize(s);
}

enum StyleGroup { SG_COLOUR, SG_EYES, SG_PUPILS, SG_MOUTH, SG_COUNT };
static inline void styleCopyGroup(FaceStyle& to, const FaceStyle& from, int group) {
  switch (group) {
    case SG_COLOUR:
      to.hue = from.hue, to.sat = from.sat, to.val = from.val, to.tint = from.tint, to.hue_r = from.hue_r,
      to.glow = from.glow;
      for (int i = 0; i < 3; i++) to.bg[i] = from.bg[i];
      break;
    case SG_EYES:
      to.eye_w = from.eye_w, to.eye_h = from.eye_h, to.eye_round = from.eye_round, to.eye_gap = from.eye_gap,
      to.eye_y = from.eye_y, to.lid = from.lid;
      to.lashes = from.lashes, to.brows = from.brows, to.blush = from.blush, to.eye_style = from.eye_style,
      to.eye_split = from.eye_split;
      to.eye_style_r = from.eye_style_r, to.eye_w_r = from.eye_w_r, to.eye_h_r = from.eye_h_r,
      to.lid_r = from.lid_r, to.brows_r = from.brows_r;
      break;
    case SG_PUPILS:
      to.pupil_style = from.pupil_style, to.pupil_style_r = from.pupil_style_r,
      to.pupil_size = from.pupil_size, to.glint = from.glint, to.pupil_ink = from.pupil_ink;
      break;
    case SG_MOUTH:
      to.mouth_w = from.mouth_w, to.mouth_y = from.mouth_y, to.line_r = from.line_r,
      to.mouth_style = from.mouth_style, to.mouth_solid = from.mouth_solid, to.tongue = from.tongue;
      break;
    default: break;
  }
}
static inline void styleRandomizeSome(FaceStyle& s, uint32_t& rng) {
  FaceStyle r = s;
  styleRandomize(r, rng);
  const int first = styleRnd(rng) % SG_COUNT;
  styleCopyGroup(s, r, first);
  if (styleRnd(rng) % 2) styleCopyGroup(s, r, (first + 1 + styleRnd(rng) % (SG_COUNT - 1)) % SG_COUNT);
}

static inline FaceStyle styleInitial() {
  FaceStyle s;
  styleApplyPreset(s, "TeeVee");
  return s;
}

// Re-clamps everything (e.g. after loading from flash).
static inline void styleSanitize(FaceStyle& s) {
  char v[24];
  for (const StyleDef& d : STYLE_DEFS) {
    const uint8_t* base = (const uint8_t*)&s + d.offset;
    if (d.type == SF_RGB) continue;
    if (d.type == SF_FLOAT)
      snprintf(v, sizeof v, "%g", *(const float*)base);
    else
      snprintf(v, sizeof v, "%u", d.type == SF_BOOL ? (unsigned)(*base ? 1 : 0) : (unsigned)*base);
    styleSet(s, d.key, v);
  }
}

// Loads a saved style of `len` bytes: it's a prefix of FaceStyle, options added since keep their defaults.
static inline void styleLoad(FaceStyle& s, const void* blob, size_t len) {
  s = STYLE_DEFAULT;
  if (len > sizeof s) len = sizeof s;
  // The first format (40 bytes) ended at `blush`; its last 2 bytes were padding where brows/pupils now live.
  if (len == 40) len = offsetof(FaceStyle, brows);
  FaceStyle saved = STYLE_DEFAULT;
  memcpy(&saved, blob, len);
  for (const StyleDef& d : STYLE_DEFS)
    if (d.offset + styleFieldSize(d.type) <= len)
      memcpy((uint8_t*)&s + d.offset, (const uint8_t*)&saved + d.offset, styleFieldSize(d.type));
  if (offsetof(FaceStyle, pupil_style) + 1 > len) {  // an older look: pupils / hollow become the pupil shape
    const uint8_t* raw = (const uint8_t*)&saved;
    const bool pu = offsetof(FaceStyle, pupils) + 1 <= len && raw[offsetof(FaceStyle, pupils)],
               ho = offsetof(FaceStyle, hollow) + 1 <= len && raw[offsetof(FaceStyle, hollow)];
    s.pupil_style = ho ? 6 : pu ? 1 : 0;
  }
  styleSanitize(s);
}

static inline void styleValue(const FaceStyle& s, const StyleDef& d, char* out, size_t n, bool json) {
  const uint8_t* base = (const uint8_t*)&s + d.offset;
  switch (d.type) {
    case SF_FLOAT: snprintf(out, n, "%.3g", *(const float*)base); break;
    case SF_BOOL: snprintf(out, n, "%d", *base ? 1 : 0); break;
    case SF_ENUM: {
      char name[16] = "?";
      if (d.options) styleEnumName(d.options, *base, name, sizeof name);
      snprintf(out, n, json ? "\"%s\"" : "%s", name);
      break;
    }
    case SF_RGB: snprintf(out, n, json ? "[%u,%u,%u]" : "%u %u %u", base[0], base[1], base[2]); break;
  }
}

// Room for styleDescribe's text (~380 chars today): every buffer it is written to uses this, so a look never loses its last options
// (a cut-off "key" without its value makes the whole text fail to load). style_test checks the margin.
constexpr size_t STYLE_TEXT_MAX = 640;

static inline int styleDescribe(const FaceStyle& s, char* buf, size_t n) {
  size_t k = 0;
  for (const StyleDef& d : STYLE_DEFS) {
    char v[24];
    styleValue(s, d, v, sizeof v, false);
    if (k < n) k += snprintf(buf + k, n - k, "%s%s %s", k ? " " : "", d.key, v);
  }
  return (int)k;
}

// The name of the preset this look is exactly, or nullptr if it was changed since. Asked rarely: it describes every preset.
static inline const char* stylePresetNow(const FaceStyle& s) {
  char* now = (char*)malloc(2 * STYLE_TEXT_MAX);
  if (!now) return nullptr;
  char* other = now + STYLE_TEXT_MAX;
  styleDescribe(s, now, STYLE_TEXT_MAX);
  const char* found = nullptr;
  for (const StylePreset& p : STYLE_PRESETS) {
    FaceStyle t;
    styleApplyPreset(t, p.name);
    styleDescribe(t, other, STYLE_TEXT_MAX);
    if (!strcmp(now, other)) {
      found = p.name;
      break;
    }
  }
  free(now);
  return found;
}

static inline size_t styleJson(const FaceStyle& s, char* out, size_t n, bool meta) {
  static const char* const TYPES[] = {"float", "bool", "enum", "rgb"};
  size_t k = snprintf(out, n, "{\"style\":{");
  for (int i = 0; i < STYLE_DEF_COUNT && k < n; i++) {
    char v[24];
    styleValue(s, STYLE_DEFS[i], v, sizeof v, true);
    k += snprintf(out + k, n - k, "%s\"%s\":%s", i ? "," : "", STYLE_DEFS[i].key, v);
  }
  if (k < n) k += snprintf(out + k, n - k, "}");
  if (meta) {
    if (k < n) k += snprintf(out + k, n - k, ",\"meta\":[");
    for (int i = 0; i < STYLE_DEF_COUNT && k < n; i++) {
      const StyleDef& d = STYLE_DEFS[i];
      k += snprintf(out + k, n - k,
                    "%s{\"key\":\"%s\",\"type\":\"%s\",\"min\":%g,\"max\":%g,\"help\":\"%s\"%s%s%s}",
                    i ? "," : "", d.key, TYPES[d.type], d.min, d.max, d.help,
                    d.type == SF_ENUM ? ",\"options\":\"" : "",
                    d.type == SF_ENUM && d.options ? d.options : "", d.type == SF_ENUM ? "\"" : "");
    }
    if (k < n) k += snprintf(out + k, n - k, "],\"presets\":[");
    for (int i = 0; i < STYLE_PRESET_COUNT && k < n; i++)
      k += snprintf(out + k, n - k, "%s\"%s\"", i ? "," : "", STYLE_PRESETS[i].name);
    if (k < n) k += snprintf(out + k, n - k, "]");
  }
  if (k < n) k += snprintf(out + k, n - k, "}");
  return k < n ? k : n - 1;
}
