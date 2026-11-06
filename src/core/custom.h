#pragma once
#include <stdio.h>
#include <string.h>
#include "emotes.h"
#include "emotion.h"
#include "face_params.h"

static constexpr int MAX_CUSTOM = 8;
static constexpr uint8_t EMOTE_AUTO = 255;
static constexpr uint8_t EMOTE_CUSTOM = 100;

struct CustomFace {
    char name[12];
    FaceParams p;
    PAD pad;
    bool mood;
};

struct CustomEmote {
    char name[12];
    char face[12];
    uint8_t eyes;
    uint8_t deco;
    char text[8];
    uint16_t ms;
    float wobble;
};

struct CustomStore {
    uint32_t version;
    uint8_t nFaces, nEmotes;
    CustomFace faces[MAX_CUSTOM];
    CustomEmote emotes[MAX_CUSTOM];
    uint8_t evMap[16];
};
static constexpr uint32_t CUSTOM_VERSION = 1;

static inline void customInit(CustomStore& c) {
  memset(&c, 0, sizeof c);
  c.version = CUSTOM_VERSION;
  memset(c.evMap, EMOTE_AUTO, sizeof c.evMap);
}

// After loading a saved store: a damaged blob must not leave names without their NUL, NaN or out-of-range faces, or event mappings
// to emotes that do not exist.
static inline void customSanitize(CustomStore& c) {
  auto fix = [](float& v, float lo, float hi) {
    v = v >= lo && v <= hi ? v : (v > hi ? hi : lo);
  };  // NaN -> lo
  if (c.nFaces > MAX_CUSTOM) c.nFaces = MAX_CUSTOM;
  if (c.nEmotes > MAX_CUSTOM) c.nEmotes = MAX_CUSTOM;
  for (CustomFace& f : c.faces) {
    f.name[sizeof f.name - 1] = 0;
    for (const FaceField& d : FACE_FIELDS) fix(faceField(f.p, d), d.min, d.max);
    fix(f.pad.v, -1, 1), fix(f.pad.a, -1, 1), fix(f.pad.d, -1, 1);
    f.mood = *(const uint8_t*)&f.mood != 0;
  }
  for (CustomEmote& e : c.emotes) {
    e.name[sizeof e.name - 1] = e.face[sizeof e.face - 1] = e.text[sizeof e.text - 1] = 0;
    if (e.eyes >= EF_COUNT) e.eyes = EF_NONE;
    if (e.deco >= DF_COUNT) e.deco = DF_NONE;
    fix(e.wobble, 0, 1);
  }
  for (uint8_t& m : c.evMap)
    if (m != EMOTE_AUTO && m >= EM_COUNT && !(m >= EMOTE_CUSTOM && m - EMOTE_CUSTOM < c.nEmotes))
      m = EMOTE_AUTO;
}

static inline bool validName(const char* n) {
  size_t len = strlen(n);
  if (!len || len >= 12) return false;
  for (const char* p = n; *p; p++)
    if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '-')) return false;
  return true;
}

static inline int customFace(const CustomStore& c, const char* name) {
  for (int i = 0; i < c.nFaces; i++)
    if (!strcmp(c.faces[i].name, name)) return i;
  return -1;
}
static inline int customEmote(const CustomStore& c, const char* name) {
  for (int i = 0; i < c.nEmotes; i++)
    if (!strcmp(c.emotes[i].name, name)) return i;
  return -1;
}

static inline bool faceByName(const CustomStore& c, const char* name, FaceParams& out) {
  for (int i = 0; i < A_COUNT; i++)
    if (!strcmp(name, ANCHOR_NAMES[i])) {
      out = ANCHORS[i];
      return true;
    }
  int k = customFace(c, name);
  if (k >= 0) out = c.faces[k].p;
  return k >= 0;
}

static inline bool saveFace(CustomStore& c, const CustomFace& f) {
  if (!validName(f.name)) return false;
  for (int i = 0; i < A_COUNT; i++)
    if (!strcmp(f.name, ANCHOR_NAMES[i])) return false;
  int k = customFace(c, f.name);
  if (k < 0) {
    if (c.nFaces >= MAX_CUSTOM) return false;
    k = c.nFaces++;
  }
  c.faces[k] = f;
  return true;
}
static inline bool saveEmote(CustomStore& c, const CustomEmote& e) {
  if (!validName(e.name) || e.eyes >= EF_COUNT || e.deco >= DF_COUNT) return false;
  for (int i = 1; i < EM_COUNT; i++)
    if (!strcmp(e.name, EMOTE_NAMES[i])) return false;
  int k = customEmote(c, e.name);
  if (k < 0) {
    if (c.nEmotes >= MAX_CUSTOM) return false;
    k = c.nEmotes++;
  }
  c.emotes[k] = e;
  return true;
}

static inline bool deleteFace(CustomStore& c, const char* name) {
  int k = customFace(c, name);
  if (k < 0) return false;
  memmove(&c.faces[k], &c.faces[k + 1], (c.nFaces - k - 1) * sizeof c.faces[0]);
  c.nFaces--;
  return true;
}
static inline bool deleteEmote(CustomStore& c, const char* name) {
  int k = customEmote(c, name);
  if (k < 0) return false;
  memmove(&c.emotes[k], &c.emotes[k + 1], (c.nEmotes - k - 1) * sizeof c.emotes[0]);
  c.nEmotes--;
  for (uint8_t& m : c.evMap) {
    if (m == EMOTE_CUSTOM + k)
      m = EMOTE_AUTO;
    else if (m > EMOTE_CUSTOM + k && m < EMOTE_AUTO)
      m--;
  }
  return true;
}

static inline int emoteId(const CustomStore& c, const char* name) {
  if (!strcmp(name, "auto")) return EMOTE_AUTO;
  if (!strcmp(name, "none")) return EM_NONE;
  int b = emoteIndex(name);
  if (b > 0) return b;
  int k = customEmote(c, name);
  return k >= 0 ? EMOTE_CUSTOM + k : -1;
}
static inline const char* emoteName(const CustomStore& c, int id) {
  if (id == EMOTE_AUTO) return "auto";
  if (id >= EMOTE_CUSTOM && id - EMOTE_CUSTOM < c.nEmotes) return c.emotes[id - EMOTE_CUSTOM].name;
  return id > 0 && id < EM_COUNT ? EMOTE_NAMES[id] : "none";
}
