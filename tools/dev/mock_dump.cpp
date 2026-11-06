#include <cstdio>
#include <string>
#include "settings.h"
#include "style.h"
#include "mind.h"
#include "diary.h"
static void out(const char* path, const char* s) {
  FILE* f = fopen(path, "w");
  fputs(s, f);
  fclose(f);
}
int main(int, char** argv) {
  std::string d = argv[1];
  static char buf[12000];
  Settings s = settingsDefaults();
  settingsJson(s, buf, sizeof buf, true);
  out((d + "/settings.json").c_str(), buf);
  FaceStyle st = styleInitial();
  styleJson(st, buf, sizeof buf, true);
  out((d + "/style.json").c_str(), buf);
  MindNet m;
  m.init(3);
  m.setRates(0.05f, 0.15f);
  MindInputs in;
  in.v = 0.3f;
  in.mood = 0.2f;
  in.hour = 15;
  in.sinceTouch = 30;
  in.sinceEvent = 40;
  in.idleSec = 120;
  in.needAttention = 0.5f;
  in.petRate = 1;
  for (int i = 0; i < 400; i++) m.step(in, 0.1f);
  for (int i = 0; i < 30; i++) {
    m.reward(0.8f);
    m.step(in, 0.1f);
  }
  mindJson(m, buf, sizeof buf);
  out((d + "/brain.json").c_str(), buf);
  Diary dy;
  dy.begin(5);
  for (int day = 0; day < 4; day++) {
    for (int h = 8; h < 23; h++) {
      for (int k = 0; k < (h % 5); k++) dy.event(EV_PET);
      dy.mood(0.6f * ((h * 7 + day * 3) % 11 - 4) / 5.f, h + 0.5f);
    }
    if (day < 3) dy.event(EV_POKE);
    if (day < 3) dy.newDay(6 + day);
  }
  dy.json(buf, sizeof buf);
  out((d + "/diary.json").c_str(), buf);
}
