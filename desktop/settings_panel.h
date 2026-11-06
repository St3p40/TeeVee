#pragma once
#include <stdio.h>
#include <string.h>
#include <string>
#include "engine.h"
#include "style.h"

enum class Act { None, Up, Down, Left, Right, Enter, Close };

struct SettingsPanel {
    int sel = 0, top = 0;
    bool open = false;
    static constexpr int VISIBLE = 10;
};

inline int settingsCount() { return STYLE_DEF_COUNT + 7; }
inline int rowPersonality() { return STYLE_DEF_COUNT; }
inline int rowWardrobe() { return STYLE_DEF_COUNT + 1; }
inline int rowLookChance() { return STYLE_DEF_COUNT + 2; }
inline int rowLookWhole() { return STYLE_DEF_COUNT + 3; }
inline int rowScreen() { return STYLE_DEF_COUNT + 4; }
inline int rowRandom() { return STYLE_DEF_COUNT + 5; }
inline int rowRandomAll() { return STYLE_DEF_COUNT + 6; }
inline int rowToDef(int row);

inline const char* settingsName(int i, char* buf, size_t n) {
  if (i < STYLE_DEF_COUNT)
    snprintf(buf, n, "%s", STYLE_DEFS[rowToDef(i)].key);
  else if (i == rowPersonality())
    snprintf(buf, n, "personality");
  else if (i == rowWardrobe())
    snprintf(buf, n, "own look changes");
  else if (i == rowLookChance())
    snprintf(buf, n, "look change chance");
  else if (i == rowLookWhole())
    snprintf(buf, n, "whole look share");
  else if (i == rowScreen())
    snprintf(buf, n, "look at the screen");
  else if (i == rowRandom())
    snprintf(buf, n, "randomize part");
  else
    snprintf(buf, n, "randomize all");
  return buf;
}

inline void settingsValue(const Engine& e, int i, char* buf, size_t n) {
  buf[0] = 0;
  if (i < STYLE_DEF_COUNT) {
    styleValue(e.style(), STYLE_DEFS[rowToDef(i)], buf, n, false);
    if (STYLE_DEFS[rowToDef(i)].type == SF_BOOL)
      snprintf(buf, n, "%s", *((const uint8_t*)&e.style() + STYLE_DEFS[rowToDef(i)].offset) ? "on" : "off");
  } else if (i == rowPersonality()) {
    static const char* const N[] = {"cheerful", "calm", "grumpy", "shy", "hyper"};
    snprintf(buf, n, "%s", N[e.personality() % 5]);
  } else if (i == rowWardrobe()) {
    snprintf(buf, n, "%s", e.wardrobe() ? "on" : "off");
  } else if (i == rowLookChance()) {
    snprintf(buf, n, "%d%%", (int)lroundf(e.lookChance() * 100));
  } else if (i == rowLookWhole()) {
    snprintf(buf, n, "%d%%", (int)lroundf(e.lookWhole() * 100));
  } else if (i == rowScreen()) {
    snprintf(buf, n, "%s", e.screenSense() ? "on" : "off");
  } else {
    snprintf(buf, n, "enter");
  }
}

// Readable names and sections for the rows (the keys themselves do not change: they are what the console and the web page use).
struct RowName {
    const char* key;
    const char* label;
};
inline const RowName ROW_NAMES[] = {
  {"hue", "Hue"}, {"sat", "Colour strength"}, {"val", "Brightness"}, {"bg", "Background"}, {"tint", "Colour follows mood"},
  {"daytint", "Day colour shift"}, {"eye", "Eye style"}, {"eye_w", "Eye width"}, {"eye_h", "Eye height"}, {"round", "Roundness"},
  {"eye_gap", "Eye spacing"}, {"eye_y", "Eye position"}, {"lid", "Sleepy lids"}, {"lashes", "Eyelashes"}, {"brows", "Eyebrows"},
  {"blush", "Blush"}, {"glow", "Glow"}, {"eye_split", "Own right eye"}, {"eye_r", "Right eye style"}, {"eye_w_r", "Right eye width"},
  {"eye_h_r", "Right eye height"}, {"lid_r", "Right eye lid"}, {"hue_r", "Right colour shift"}, {"brows_r", "Right eyebrow"},
  {"pupil", "Pupil shape"}, {"pupil_r", "Right pupil"}, {"pupil_ink", "Pupil colour"}, {"pupil_size", "Pupil size"},
  {"glint", "White glint"}, {"mouth", "Mouth style"}, {"mouth_solid", "Solid mouth"}, {"mouth_w", "Mouth width"},
  {"mouth_y", "Mouth position"}, {"line", "Line thickness"}, {"speed", "Motion speed"}, {"bounce", "Bounciness"},
  {"pixel", "Pixelate"}, {"glitch", "Glitch"}, {"split", "RGB split"},
};

inline const char* settingsLabel(int i, char* buf, size_t n) {
  if (i >= STYLE_DEF_COUNT) {
    snprintf(buf, n, "%s",
             i == rowPersonality()  ? "Personality"
             : i == rowWardrobe()   ? "Own look changes"
             : i == rowLookChance() ? "Chance of changing look"
             : i == rowLookWhole()  ? "Whole look (or a part)"
             : i == rowScreen()     ? "Look at the screen"
             : i == rowRandom()     ? "Randomize a part"
                                    : "Randomize everything");
    return buf;
  }
  for (const RowName& r : ROW_NAMES)
    if (!strcmp(r.key, STYLE_DEFS[rowToDef(i)].key)) {
      snprintf(buf, n, "%s", r.label);
      return buf;
    }
  snprintf(buf, n, "%s", STYLE_DEFS[rowToDef(i)].key);
  return buf;
}

inline const char* sectionOfDef(int d) {
  const char* k = STYLE_DEFS[d].key;
  static const char* const COLOUR[] = {"hue", "sat", "val", "bg", "tint", "daytint"};
  static const char* const PUPILS[] = {"pupil", "pupil_r", "pupil_ink", "pupil_size", "glint"};
  static const char* const MOUTH[] = {"mouth", "mouth_solid", "mouth_w", "mouth_y", "line"};
  static const char* const MOTION[] = {"speed", "bounce"};
  static const char* const EFFECTS[] = {"glow", "pixel", "glitch", "split"};
  for (const char* c : COLOUR)
    if (!strcmp(c, k)) return "Colour";
  for (const char* c : PUPILS)
    if (!strcmp(c, k)) return "Pupils";
  for (const char* c : MOUTH)
    if (!strcmp(c, k)) return "Mouth";
  for (const char* c : MOTION)
    if (!strcmp(c, k)) return "Motion";
  for (const char* c : EFFECTS)
    if (!strcmp(c, k)) return "Effects";
  return "Eyes";
}
inline int sectionRank(const char* sec) {
  static const char* const ORDER[] = {"Colour", "Eyes", "Pupils", "Mouth", "Motion", "Effects"};
  for (int k = 0; k < 6; k++)
    if (!strcmp(ORDER[k], sec)) return k;
  return 6;
}
inline int rowToDef(int row) {
  static int order[STYLE_DEF_COUNT];
  static bool ready = false;
  if (!ready) {
    for (int i = 0; i < STYLE_DEF_COUNT; i++) order[i] = i;
    for (int a = 1; a < STYLE_DEF_COUNT; a++)
      for (int b = a; b > 0 && sectionRank(sectionOfDef(order[b])) < sectionRank(sectionOfDef(order[b - 1]));
           b--) {
        const int t = order[b];
        order[b] = order[b - 1];
        order[b - 1] = t;
      }
    ready = true;
  }
  return row < STYLE_DEF_COUNT ? order[row] : row;
}

inline const char* settingsSection(int i) {
  return i >= STYLE_DEF_COUNT ? "Character" : sectionOfDef(rowToDef(i));
}

inline int enumCount(const char* opts) {
  int c = 0;
  char tmp[32];
  while (styleEnumName(opts, c, tmp, sizeof tmp)) c++;
  return c;
}

inline void settingsAdjust(Engine& e, int i, int dir) {
  if (i >= STYLE_DEF_COUNT) {
    if (i == rowPersonality())
      e.setPersonality(e.personality() + dir);
    else if (i == rowWardrobe())
      e.setWardrobe(!e.wardrobe());
    else if (i == rowLookChance())
      e.setLookChange(e.lookChance() + 0.1f * dir, e.lookWhole());
    else if (i == rowLookWhole())
      e.setLookChange(e.lookChance(), e.lookWhole() + 0.1f * dir);
    else if (i == rowScreen())
      e.setScreenSense(!e.screenSense());
    return;
  }
  const StyleDef& d = STYLE_DEFS[rowToDef(i)];
  const uint8_t* base = (const uint8_t*)&e.style() + d.offset;
  char val[64], line[96];
  switch (d.type) {
    case SF_FLOAT: {
      const float range = d.max - d.min, step = range >= 8 ? 1.f : range / 20.f;
      float v = *(const float*)base + dir * step;
      v = v < d.min ? d.min : v > d.max ? d.max : v;
      snprintf(val, sizeof val, "%g", v);
      snprintf(line, sizeof line, "%s %s", d.key, val);
      e.styleText(line);
      break;
    }
    case SF_BOOL:
      snprintf(line, sizeof line, "%s %d", d.key, *base ? 0 : 1);
      e.styleText(line);
      break;
    case SF_ENUM: {
      const int n = enumCount(d.options);
      if (n <= 0) break;
      const int next = (*base + dir + n) % n;
      char name[32];
      styleEnumName(d.options, next, name, sizeof name);
      snprintf(line, sizeof line, "%s %s", d.key, name);
      e.styleText(line);
      break;
    }
    case SF_RGB: {
      static const int PALETTE[][3] = {{0, 0, 0},   {8, 12, 26},  {24, 8, 24},    {4, 18, 12},
                                       {26, 16, 4}, {20, 20, 24}, {255, 255, 255}};
      const int n = (int)(sizeof PALETTE / sizeof PALETTE[0]);
      int cur = 0;
      for (int k = 0; k < n; k++)
        if (base[0] == PALETTE[k][0] && base[1] == PALETTE[k][1] && base[2] == PALETTE[k][2]) cur = k;
      const int next = (cur + dir + n) % n;
      snprintf(line, sizeof line, "%s %d %d %d", d.key, PALETTE[next][0], PALETTE[next][1], PALETTE[next][2]);
      e.styleText(line);
      break;
    }
    default: break;
  }
}

inline bool settingsAct(Engine& e, SettingsPanel& sp, Act a) {
  switch (a) {
    case Act::Up: sp.sel = (sp.sel + settingsCount() - 1) % settingsCount(); break;
    case Act::Down: sp.sel = (sp.sel + 1) % settingsCount(); break;
    case Act::Left: settingsAdjust(e, sp.sel, -1); break;
    case Act::Right: settingsAdjust(e, sp.sel, +1); break;
    case Act::Enter:
      if (sp.sel == rowRandom())
        e.randomLook(false);
      else if (sp.sel == rowRandomAll())
        e.randomLook(true);
      else
        settingsAdjust(e, sp.sel, +1);
      break;
    case Act::Close: sp.open = false; return true;
    default: return false;
  }
  if (sp.sel < sp.top) sp.top = sp.sel;
  if (sp.sel >= sp.top + SettingsPanel::VISIBLE) sp.top = sp.sel - SettingsPanel::VISIBLE + 1;
  return true;
}
