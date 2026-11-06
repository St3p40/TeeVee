#pragma once
#include <stddef.h>
#include <stdint.h>

struct FaceParams {
    float eye_open_l;
    float eye_open_r;
    float pupil_x;
    float pupil_y;
    float brow_angle;
    float brow_height;
    float mouth_curve;
    float mouth_open;
    float squash;
    float blush;
    float hue;
    float lid_bottom;
    float asym;
    float mouth_skew;
    float mouth_width;
};

enum Anchor : uint8_t {
  A_NEUTRAL,
  A_HAPPY,
  A_SAD,
  A_ANGRY,
  A_SURPRISED,
  A_SLEEPY,
  A_EXCITED,
  A_CONTENT,
  A_SCARED,
  A_BORED,
  A_SKEPTICAL,
  A_CONFUSED,
  A_SMUG,
  A_COUNT
};

inline const FaceParams ANCHORS[A_COUNT] = {
 {1.00f, 1.00f, 0.0f,  0.0f,  0.0f,  0.0f,  0.2f,  0.0f,  0.0f,  0.0f,  0,     0.0f, 0.0f,  0.0f,  0.0f},
 {0.75f, 0.75f, 0.0f,  0.1f, -0.2f,  0.3f,  1.0f,  0.35f, 0.1f,  0.6f,  25,    0.45f, 0.0f, 0.0f,  0.1f},
 {0.60f, 0.60f, 0.0f,  0.5f, -0.7f, -0.2f, -0.8f,  0.05f, -0.15f, 0.0f, -150,  0.0f, 0.0f,  0.0f, -0.2f},
 {0.70f, 0.70f, 0.0f,  0.0f,  0.9f, -0.4f, -0.5f,  0.15f, -0.1f, 0.0f, -10,    0.0f, 0.0f,  0.0f,  0.0f},
 {1.00f, 1.00f, 0.0f,  0.0f,  0.0f,  1.0f,  0.0f,  0.9f,  0.25f, 0.0f,  40,    0.0f, 0.0f,  0.0f, -0.5f},
 {0.15f, 0.20f, 0.0f,  0.3f, -0.1f, -0.5f,  0.0f,  0.0f, -0.2f,  0.2f, -90,    0.0f, 0.0f,  0.0f,  0.0f},
 {1.00f, 1.00f, 0.0f, -0.2f, -0.3f,  0.7f,  1.0f,  0.7f,  0.3f,  0.4f,  30,    0.2f, 0.0f,  0.0f,  0.2f},
 {0.55f, 0.55f, 0.0f,  0.1f, -0.2f,  0.1f,  0.7f,  0.0f, -0.05f, 0.7f, -20,    0.7f, 0.0f,  0.0f, -0.1f},
 {1.00f, 0.95f, 0.0f,  0.2f, -0.8f,  0.6f, -0.6f,  0.3f, -0.3f,  0.0f, -120,   0.0f, 0.0f,  0.0f, -0.3f},
 {0.45f, 0.45f, 0.5f,  0.2f,  0.0f, -0.3f, -0.1f,  0.0f, -0.1f,  0.0f, -60,    0.0f, 0.0f,  0.3f, -0.2f},
 {0.80f, 0.80f, -0.3f, 0.0f,  0.2f,  0.2f, -0.2f,  0.0f,  0.0f,  0.0f,  15,    0.0f, 0.8f,  0.6f, -0.1f},
 {0.90f, 0.75f, 0.0f, -0.2f, -0.3f,  0.4f, -0.2f,  0.1f,  0.0f,  0.0f, -100,   0.0f, -0.6f, -0.5f, -0.3f},
 {0.55f, 0.55f, 0.2f,  0.0f,  0.3f, -0.1f,  0.5f,  0.0f,  0.0f,  0.2f,  10,    0.3f, 0.3f,  0.7f,  0.0f},
};

inline const char* const ANCHOR_NAMES[A_COUNT] = {"neutral",   "happy",    "sad",     "angry",  "surprised",
                                                  "sleepy",    "excited",  "content", "scared", "bored",
                                                  "skeptical", "confused", "smug"};

struct FaceField {
    const char* name;
    size_t offset;
    float min, max;
};
inline const FaceField FACE_FIELDS[] = {
  {"eye_l", offsetof(FaceParams, eye_open_l), 0, 1},     {"eye_r", offsetof(FaceParams, eye_open_r), 0, 1},
  {"look_x", offsetof(FaceParams, pupil_x), -1, 1},      {"look_y", offsetof(FaceParams, pupil_y), -1, 1},
  {"brow_angle", offsetof(FaceParams, brow_angle), -1, 1}, {"brow_height", offsetof(FaceParams, brow_height), -1, 1},
  {"smile", offsetof(FaceParams, mouth_curve), -1, 1},   {"mouth_open", offsetof(FaceParams, mouth_open), 0, 1},
  {"squash", offsetof(FaceParams, squash), -1, 1},       {"blush", offsetof(FaceParams, blush), 0, 1},
  {"hue", offsetof(FaceParams, hue), -180, 180},         {"lid_bottom", offsetof(FaceParams, lid_bottom), 0, 1},
  {"asym", offsetof(FaceParams, asym), -1, 1},           {"skew", offsetof(FaceParams, mouth_skew), -1, 1},
  {"mouth_width", offsetof(FaceParams, mouth_width), -1, 1},
};
static constexpr int FACE_FIELD_COUNT = sizeof(FACE_FIELDS) / sizeof(FACE_FIELDS[0]);

static inline float& faceField(FaceParams& p, const FaceField& f) {
  return *(float*)((uint8_t*)&p + f.offset);
}
static inline float faceField(const FaceParams& p, const FaceField& f) {
  return *(const float*)((const uint8_t*)&p + f.offset);
}
