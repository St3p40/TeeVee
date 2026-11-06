#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include "app_vm.h"
#include "canvas.h"
#include "emotes.h"
#include "mapper.h"
#include "platform_lcd.h"
#include "renderer.h"
#include "spring.h"
#include "style.h"
struct P : AppPlatform {
    uint32_t t = 0;
    PAD mood{0.2f, 0.1f, 0};
    std::vector<std::pair<int, float>> ev;
    std::vector<int> em;
    uint32_t millis() override { return t; }
    bool localTime(AppTime&) override { return false; }
    float light() override { return 1; }
    size_t kvLoad(const char*, char*, size_t) override { return 0; }
    void kvSave(const char*, const char*, size_t) override {}
    bool httpStart(const char*, int, const char*) override { return false; }
    void log(const char*, const char*) override {}
    bool motion(float*) override { return false; }
    void requestExit() override {}
    void setCapture(bool) override {}
    void faceEvent(int type, float k) override { ev.push_back({type, k}); }
    void faceEmote(int e) override { em.push_back(e); }
    bool faceMood(float& v, float& a, float& d) override {
      v = mood.v;
      a = mood.a;
      d = mood.d;
      return true;
    }
};
static void save(const char* path) {
  static uint32_t argb[desktop::W * desktop::H];
  desktop::toArgb(argb);
  FILE* fp = fopen(path, "wb");
  fprintf(fp, "P6\n%d %d\n255\n", desktop::W, desktop::H);
  for (int i = 0; i < desktop::W * desktop::H; i++) {
    unsigned char c[3] = {(unsigned char)(argb[i] >> 16), (unsigned char)(argb[i] >> 8),
                          (unsigned char)argb[i]};
    fwrite(c, 1, 3, fp);
  }
  fclose(fp);
}
int main(int argc, char** argv) {
  std::string src;
  FILE* f = fopen(argv[1], "rb");
  char b[4096];
  size_t n;
  while ((n = fread(b, 1, sizeof b, f)) > 0) src.append(b, n);
  fclose(f);
  Lcd lcd;
  FaceRenderer r;
  r.begin(&lcd);
  r.setStyle(styleInitial());
  r.enterApp();
  P p;
  AppVM vm(&p);
  char err[200] = "";
  if (!vm.start("preview", src.data(), src.size(), err, sizeof err)) {
    printf("start failed: %s\n", err);
    return 1;
  }
  FaceParams happy = ANCHORS[A_HAPPY], sad = ANCHORS[A_SAD], neutral = ANCHORS[A_NEUTRAL];
  struct Step {
      int tapX, tapY;
      const FaceParams* face;
      const char* name;
      int ms;
  };
  std::vector<Step> steps = {{-1, -1, &neutral, "pick", 400},
                             {60, 190, &neutral, "shake", 400},
                             {-1, -1, &happy, "result_win", 1600},
                             {-1, -1, &sad, "result_sad", 600}};
  for (auto& s : steps) {
    if (s.tapX >= 0) {
      vm.touch(TOUCH_DOWN, s.tapX, s.tapY);
      vm.touch(TOUCH_UP, s.tapX, s.tapY);
    }
    for (int ms = 0; ms < s.ms; ms += 33) {
      p.t += 33;
      vm.tick(0.033f);
      r.setAppFace(*s.face);
      r.renderApp(vm.frame());
    }
    char path[300];
    snprintf(path, sizeof path, "%s/%s.ppm", argv[2], s.name);
    save(path);
    printf("%s: %zu events, %zu emotes so far\n", s.name, p.ev.size(), p.em.size());
  }
}
