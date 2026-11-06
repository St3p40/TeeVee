#include <cmath>
#include <cstdio>
#include "mapper.h"
int fails = 0;
#define CHECK(c)                                                                                             \
  do {                                                                                                       \
    if (!(c)) {                                                                                              \
      printf("FAIL line %d: %s\n", __LINE__, #c);                                                            \
      fails++;                                                                                               \
    }                                                                                                        \
  } while (0)
int main() {
  CustomStore c;
  customInit(c);
  CustomFace f{};
  strcpy(f.name, "grin");
  f.p = ANCHORS[A_HAPPY];
  f.p.mouth_curve = 1;
  f.pad = {0.9f, 0.9f, 0.9f};
  f.mood = true;
  CHECK(saveFace(c, f) && c.nFaces == 1);
  CustomFace bad = f;
  strcpy(bad.name, "happy");
  CHECK(!saveFace(c, bad));
  strcpy(bad.name, "Bad Name");
  CHECK(!saveFace(c, bad));
  FaceParams p;
  CHECK(faceByName(c, "grin", p) && p.mouth_curve == 1 && faceByName(c, "sad", p) &&
        !faceByName(c, "nope", p));
  CustomEmote e{};
  strcpy(e.name, "yay");
  strcpy(e.face, "grin");
  e.eyes = EF_STAR;
  e.deco = DF_TEXT;
  strcpy(e.text, "YAY");
  e.ms = 2000;
  CHECK(saveEmote(c, e));
  strcpy(e.name, "boo");
  CHECK(saveEmote(c, e) && c.nEmotes == 2);
  strcpy(e.name, "heart");
  CHECK(!saveEmote(c, e));
  c.evMap[EV_PET] = emoteId(c, "boo");
  c.evMap[EV_WIN] = emoteId(c, "yay");
  CHECK(c.evMap[EV_PET] == EMOTE_CUSTOM + 1 && !strcmp(emoteName(c, c.evMap[EV_PET]), "boo"));
  CHECK(deleteEmote(c, "yay") && c.evMap[EV_WIN] == EMOTE_AUTO && c.evMap[EV_PET] == EMOTE_CUSTOM);
  CHECK(emoteId(c, "auto") == EMOTE_AUTO && emoteId(c, "none") == 0 && emoteId(c, "star") == EM_STAR &&
        emoteId(c, "zzz9") == -1);
  for (int i = 0; i < 8; i++) {
    CustomFace g = f;
    snprintf(g.name, 12, "f%d", i);
    saveFace(c, g);
  }
  CHECK(c.nFaces == 8);
  customFaces() = &c;
  FaceParams m = mapEmotion({0.9f, 0.9f, 0.9f});
  CHECK(m.mouth_curve > 0.9f);
  customFaces() = nullptr;
  FaceParams m2 = mapEmotion({0.9f, 0.9f, 0.9f});
  CHECK(m2.mouth_curve < m.mouth_curve);
  {  // a damaged saved store is cleaned up when loaded
    CustomStore d = c;
    memset(d.faces[0].name, 'x', sizeof d.faces[0].name);
    d.faces[0].p.mouth_curve = NAN;
    d.faces[0].pad.v = 7;
    d.emotes[0].eyes = 200;
    d.emotes[0].wobble = NAN;
    d.evMap[EV_POKE] = EMOTE_CUSTOM + 5;
    d.evMap[EV_PET] = EM_COUNT;
    customSanitize(d);
    CHECK(strlen(d.faces[0].name) == 11 && d.faces[0].p.mouth_curve == d.faces[0].p.mouth_curve &&
          d.faces[0].pad.v == 1);
    CHECK(d.emotes[0].eyes == EF_NONE && d.emotes[0].wobble == 0 && d.evMap[EV_POKE] == EMOTE_AUTO &&
          d.evMap[EV_PET] == EMOTE_AUTO && d.evMap[EV_WIN] == c.evMap[EV_WIN]);
  }
  printf("sizeof CustomStore %zu: %s\n", sizeof(CustomStore), fails ? "FAILED" : "ok");
  return fails ? 1 : 0;
}
