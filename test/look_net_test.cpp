#include <cstdio>
#include <cmath>
#include "look_net.h"

static int fails = 0;
#define CHECK(c)                                                                                             \
  do {                                                                                                       \
    if (!(c)) {                                                                                              \
      printf("FAIL line %d: %s\n", __LINE__, #c);                                                            \
      fails++;                                                                                               \
    }                                                                                                        \
  } while (0)

static void features(int eye, int mouth, int preset, float* f) {
  FaceStyle s = FaceStyle();
  s.eye_style = eye;
  s.mouth_style = mouth;
  s.pupil_style = 3;
  lookFeatures(s, preset, f);
}

int main() {
  //==============================
  // size: a bigger net than the mind net (about 3,000 weights)
  //==============================
  CHECK(LookNet::WEIGHTS > 3000);
  CHECK(LookNet::H1 > 32 && LookNet::H2 > 16);

  //==============================
  // features are finite and inside the expected ranges
  //==============================
  {
    float f[LookNet::CAND + LookNet::CTX];
    features(6, 9, 3, f);
    for (int i = 0; i < LookNet::CAND; i++) CHECK(std::isfinite(f[i]));
    CHECK(f[0] == 1.f && f[1] == 1.f && f[10] > 0.33f && f[10] < 0.34f);
    CHECK(f[11 + 3] == 1.f && f[11 + 2] == 0.f);
  }

  //==============================
  // learns: praised look goes up, scolded look goes down
  //==============================
  {
    LookNet net;
    float like[LookNet::IN] = {}, dislike[LookNet::IN] = {};
    float ctx[LookNet::CTX] = {0.5f, 0.2f, 0.f, 0.f, 0.1f, 0.4f, 0.f, 0.f, 0.3f, 0.f, 0.f, 0.f};
    float fl[LookNet::CAND], fd[LookNet::CAND];
    features(5, 3, 2, fl);
    features(1, 7, 7, fd);
    for (int i = 0; i < LookNet::CTX; i++) like[i] = dislike[i] = ctx[i];
    for (int i = 0; i < LookNet::CAND; i++) {
      like[LookNet::CTX + i] = fl[i];
      dislike[LookNet::CTX + i] = fd[i];
    }

    float before = net.score(like) - net.score(dislike);
    for (int t = 0; t < 3000; t++) {
      net.train(like, 1.f, 0.01f);
      net.train(dislike, -1.f, 0.01f);
    }
    float after = net.score(like) - net.score(dislike);
    printf("  like-minus-dislike: before %.3f after %.3f\n", before, after);
    CHECK(after > before + 0.5f);
    CHECK(after > 0.5f);
    CHECK(std::isfinite(net.score(like)) && std::isfinite(net.score(dislike)));
    CHECK(net.updates == 6000);
  }

  {
    static LookNet was;
    was.train((float[LookNet::IN]){1.f}, 1.f, 0.02f);
    static float now[LookNet::WEIGHTS], old[LookNet::OLD_WEIGHTS];
    was.flatten(now);
    int k = 0;
    const int base = LookNet::CTX + 11;
    for (int j = 0; j < LookNet::H1; j++)
      for (int i = 0; i < LookNet::OLD_IN; i++) {
        const int to = i < base ? i : styleOldPreset(i - base) < 0 ? -1 : base + styleOldPreset(i - base);
        old[k++] =
            to < 0 ? 99.f : now[j * LookNet::IN + to];  // (99: a look that is gone: must not land anywhere)
      }
    for (int i = LookNet::H1 * LookNet::IN; i < LookNet::WEIGHTS; i++) old[k++] = now[i];
    CHECK(k == LookNet::OLD_WEIGHTS && LookNet::oldInputsOf(k) == LookNet::OLD_IN &&
          LookNet::oldInputsOf(k + 1) == -1);
    static LookNet moved;
    moved.unflattenOld(old, LookNet::OLD_IN);
    static float back[LookNet::WEIGHTS];
    moved.flatten(back);
    CHECK(!memcmp(back, now, sizeof back));
  }
  //==============================
  // a net saved when there were 3 fewer presets carries over: the old inputs keep their weights, the new presets keep fresh ones
  //==============================
  {
    LookNet trained;
    float x[LookNet::IN] = {};
    for (int i = 0; i < LookNet::IN - 3; i++) x[i] = (i % 5) * 0.2f;
    for (int t = 0; t < 500; t++) trained.train(x, 1.f, 0.02f);
    static float full[LookNet::WEIGHTS], old[LookNet::WEIGHTS];
    trained.flatten(full);
    const int oldIn = LookNet::IN - 3;
    int k = 0;
    for (int j = 0; j < LookNet::H1; j++)
      for (int i = 0; i < LookNet::IN; i++)
        if (i < oldIn) old[k++] = full[j * LookNet::IN + i];
    for (int i = LookNet::H1 * LookNet::IN; i < LookNet::WEIGHTS; i++) old[k++] = full[i];
    CHECK(LookNet::inputsOf(k) == oldIn && LookNet::inputsOf(LookNet::WEIGHTS) == LookNet::IN);
    CHECK(LookNet::inputsOf(k - 1) == -1 && LookNet::inputsOf(LookNet::WEIGHTS + LookNet::H1) == -1 &&
          LookNet::inputsOf(10) == -1);
    LookNet moved;
    moved.unflattenFrom(old, oldIn);
    CHECK(fabsf(moved.score(x) - trained.score(x)) < 1e-5f);
  }

  //==============================
  // stays finite after a long run of one-sided reactions
  //==============================
  {
    LookNet net;
    float x[LookNet::IN] = {};
    for (int i = 0; i < LookNet::IN; i++) x[i] = 0.5f;
    for (int t = 0; t < 20000; t++) net.train(x, 1.f, 0.01f);
    CHECK(std::isfinite(net.score(x)));
  }

  printf(fails ? "look_net_test: %d FAILED\n" : "look_net_test: all passed\n", fails);
  return fails ? 1 : 0;
}
