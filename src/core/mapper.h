#pragma once
#include <math.h>
#include "emotion.h"
#include "face_params.h"
#include "custom.h"

// Mapper: PAD -> base FaceParams by blending the anchor faces, each weighted by a Gaussian of its
// distance to the anchor's PAD point. Renderer and springs never see emotions.

inline const PAD ANCHOR_PAD[A_COUNT] = {
 { 0.0f,  0.0f,  0.0f},
 { 0.8f,  0.4f,  0.3f},
 {-0.7f, -0.4f, -0.4f},
 {-0.6f,  0.6f,  0.6f},
 { 0.1f,  0.9f, -0.1f},
 { 0.0f, -0.9f, -0.2f},
 { 0.7f,  0.9f,  0.4f},
 { 0.6f, -0.4f,  0.1f},
 {-0.6f,  0.7f, -0.7f},
 {-0.3f, -0.5f,  0.1f},
 {-0.2f,  0.2f,  0.6f},
 {-0.2f,  0.4f, -0.4f},
 { 0.5f,  0.0f,  0.8f},
};

static constexpr float MAP_SIGMA = 0.45f;

// Per-expression strength (settings x_<expression>): scales each anchor's pull, 0 = never shown.
// `inline` (not static) so every file shares one copy.
inline float* expressionWeights() {
  static float w[A_COUNT] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
  return w;
}

static inline void anchorWeights(const PAD& s, float w[A_COUNT]) {
  float sum = 0;
  const float* ew = expressionWeights();
  for (int i = 0; i < A_COUNT; i++) {
    float dv = s.v - ANCHOR_PAD[i].v, da = s.a - ANCHOR_PAD[i].a, dd = s.d - ANCHOR_PAD[i].d;
    w[i] = ew[i] * expf(-(dv * dv + da * da + dd * dd) / (MAP_SIGMA * MAP_SIGMA));
    sum += w[i];
  }
  for (int i = 0; i < A_COUNT; i++) w[i] = sum > 1e-6f ? w[i] / sum : (i == A_NEUTRAL);
}

inline const CustomStore*& customFaces() {
  static const CustomStore* store = nullptr;
  return store;
}

static inline FaceParams mapEmotion(const PAD& s) {
  constexpr int N = sizeof(FaceParams) / sizeof(float);
  float w[A_COUNT + MAX_CUSTOM];
  const FaceParams* src[A_COUNT + MAX_CUSTOM];
  int n = 0;
  float sum = 0;
  auto add = [&](const PAD& at, const FaceParams& face, float strength) {
    float dv = s.v - at.v, da = s.a - at.a, dd = s.d - at.d;
    w[n] = strength * expf(-(dv * dv + da * da + dd * dd) / (MAP_SIGMA * MAP_SIGMA));
    src[n++] = &face;
    sum += w[n - 1];
  };
  const float* ew = expressionWeights();
  for (int i = 0; i < A_COUNT; i++) add(ANCHOR_PAD[i], ANCHORS[i], ew[i]);
  if (const CustomStore* c = customFaces())
    for (int i = 0; i < c->nFaces; i++)
      if (c->faces[i].mood) add(c->faces[i].pad, c->faces[i].p, 1);
  if (sum < 1e-6f) return ANCHORS[A_NEUTRAL];
  FaceParams out{};
  float* po = reinterpret_cast<float*>(&out);
  for (int i = 0; i < n; i++) {
    const float* pa = reinterpret_cast<const float*>(src[i]);
    for (int k = 0; k < N; k++) po[k] += w[i] / sum * pa[k];
  }
  return out;
}
