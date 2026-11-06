#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "face_params.h"

enum Emote : uint8_t {
  EM_NONE,
  EM_HEART,
  EM_STAR,
  EM_DIZZY,
  EM_CRY,
  EM_SWEAT,
  EM_BUBBLE,
  EM_ANGRY,
  EM_QUESTION,
  EM_EXCLAIM,
  EM_MUSIC,
  EM_LAUGH,
  EM_LOVE,
  EM_THINK,
  EM_SHY,
  EM_RAGE,
  EM_MONEY,
  EM_GLITCH,
  EM_SPARKLE,
  EM_DEAD,
  EM_COUNT
};

inline const char* const EMOTE_NAMES[EM_COUNT] = {
  "none", "heart", "star", "dizzy", "cry", "sweat", "bubble", "angry", "question", "exclaim", "music", "laugh",
  "love", "think", "shy", "rage", "money", "glitch", "sparkle", "dead"};
inline const uint16_t EMOTE_MS[EM_COUNT] = {0,    2500, 2500, 2500, 3500, 3000, 9000, 2500, 3000, 1800,
                                            3500, 2500, 3000, 3500, 3000, 2500, 2500, 1500, 2500, 3000};

enum EyeFx : uint8_t { EF_NONE, EF_HEART, EF_STAR, EF_X, EF_XWOB, EF_COIN, EF_COUNT };
inline const char* const EYEFX_NAMES[EF_COUNT] = {"none", "heart", "star", "x", "dizzy", "coin"};
enum Deco : uint8_t {
  DF_NONE,
  DF_TEARS,
  DF_SWEAT,
  DF_BUBBLE,
  DF_VEIN,
  DF_QUESTION,
  DF_EXCLAIM,
  DF_NOTES,
  DF_HEARTS,
  DF_DOTS,
  DF_SHY,
  DF_STEAM,
  DF_SPARKLE,
  DF_GLITCH,
  DF_TONGUE,
  DF_TEXT,
  DF_GLASSES,
  DF_COUNT
};
inline const char* const DECO_NAMES[DF_COUNT] = {
    "none", "tears", "sweat", "bubble",  "vein",   "question", "exclaim", "notes",  "hearts",
    "dots", "shy",   "steam", "sparkle", "glitch", "tongue",   "text",    "glasses"};

struct EmoteParts {
    uint8_t eyes, deco;
};
inline const EmoteParts EMOTE_PARTS[EM_COUNT] = {
  {EF_NONE, DF_NONE},     {EF_HEART, DF_NONE}, {EF_STAR, DF_NONE},  {EF_XWOB, DF_NONE},   {EF_NONE, DF_TEARS},
  {EF_NONE, DF_SWEAT},    {EF_NONE, DF_BUBBLE},   {EF_NONE, DF_VEIN},  {EF_NONE, DF_QUESTION}, {EF_NONE, DF_EXCLAIM},
  {EF_NONE, DF_NOTES},    {EF_NONE, DF_NONE},  {EF_NONE, DF_HEARTS}, {EF_NONE, DF_GLASSES}, {EF_NONE, DF_SHY},
  {EF_NONE, DF_STEAM},    {EF_COIN, DF_NONE},  {EF_NONE, DF_GLITCH}, {EF_NONE, DF_SPARKLE}, {EF_X, DF_TONGUE},
};

struct EmoteRule {
    uint8_t n;
    uint8_t emote[6];
    float weight[6];
};

struct Overlay {
    uint8_t eyes, deco;
    char text[8];
    uint32_t start, duration;
};

static inline int nameIndex(const char* const* names, int n, const char* name) {
  for (int i = 0; i < n; i++)
    if (!strcmp(names[i], name)) return i;
  return -1;
}

static inline int emoteIndex(const char* name) {
  for (int i = 1; i < EM_COUNT; i++)
    if (!strcmp(name, EMOTE_NAMES[i])) return i;
  return -1;
}

static inline FaceParams emoteFace(uint8_t e, float t) {
  const float s = sinf(t * 6.2832f * 3);
  FaceParams p;
  switch (e) {
    case EM_HEART:
      p = ANCHORS[A_HAPPY];
      p.blush = 1;
      p.squash = 0.1f + 0.06f * s;
      break;
    case EM_STAR: p = ANCHORS[A_EXCITED]; break;
    case EM_DIZZY:
      p = ANCHORS[A_CONFUSED];
      p.mouth_skew = 0.6f * s;
      p.mouth_curve = -0.2f;
      break;
    case EM_CRY:
      p = ANCHORS[A_SAD];
      p.eye_open_l = p.eye_open_r = 0.45f;
      p.mouth_open = 0.25f + 0.05f * s;
      p.mouth_width = -0.3f;
      break;
    case EM_SWEAT:
      p = ANCHORS[A_NEUTRAL];
      p.mouth_curve = 0.25f;
      p.mouth_skew = 0.4f;
      p.brow_angle = -0.3f;
      p.pupil_x = -0.4f;
      break;
    case EM_BUBBLE:
      p = ANCHORS[A_SLEEPY];
      p.eye_open_l = p.eye_open_r = 0.05f;
      break;
    case EM_ANGRY:
      p = ANCHORS[A_ANGRY];
      p.squash = -0.1f + 0.05f * s;
      break;
    case EM_QUESTION:
      p = ANCHORS[A_CONFUSED];
      p.pupil_y = -0.4f;
      break;
    case EM_EXCLAIM: p = ANCHORS[A_SURPRISED]; break;
    case EM_MUSIC:
      p = ANCHORS[A_CONTENT];
      p.mouth_open = 0.35f;
      p.mouth_width = -0.85f;
      p.mouth_curve = 0;
      p.squash = 0.05f * s;
      p.pupil_y = -0.3f;
      break;
    case EM_LAUGH:
      p = ANCHORS[A_HAPPY];
      p.lid_bottom = 1;
      p.mouth_open = 0.7f + 0.15f * s;
      p.squash = 0.18f * s;
      break;
    case EM_LOVE:
      p = ANCHORS[A_CONTENT];
      p.blush = 1;
      p.squash = 0.05f * s;
      break;
    case EM_THINK:
      p = ANCHORS[A_NEUTRAL];
      p.pupil_x = 0.5f;
      p.pupil_y = -0.7f;
      p.mouth_curve = -0.1f;
      p.mouth_skew = 0.5f;
      p.brow_height = 0.3f;
      p.asym = 0.4f;
      break;
    case EM_SHY:
      p = ANCHORS[A_CONTENT];
      p.blush = 1;
      p.pupil_x = -0.6f;
      p.pupil_y = 0.7f;
      p.squash = -0.12f;
      p.lid_bottom = 0.2f;
      break;
    case EM_RAGE:
      p = ANCHORS[A_ANGRY];
      p.brow_angle = 1;
      p.mouth_open = 0.3f;
      p.squash = 0.12f * sinf(t * 6.2832f * 14);
      p.pupil_x = 0.15f * sinf(t * 6.2832f * 11);
      break;
    case EM_MONEY: p = ANCHORS[A_EXCITED]; break;
    case EM_GLITCH:
      p = ANCHORS[A_NEUTRAL];
      p.mouth_curve = 0;
      p.squash = 0.15f * sinf(t * 6.2832f * 9);
      break;
    case EM_SPARKLE:
      p = ANCHORS[A_EXCITED];
      p.mouth_open = 0.4f;
      break;
    case EM_DEAD:
      p = ANCHORS[A_NEUTRAL];
      p.mouth_curve = -0.2f;
      p.mouth_open = 0;
      break;
    default: p = ANCHORS[A_NEUTRAL];
  }
  return p;
}
