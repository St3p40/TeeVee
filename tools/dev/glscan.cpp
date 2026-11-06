#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "canvas.h"
#include "platform_lcd.h"
#include "renderer.h"
#include "style.h"
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
  Lcd lcd;
  FaceRenderer r;
  r.begin(&lcd);
  FaceStyle st = styleInitial();
  styleApplyText(st, argv[2]);
  r.setStyle(st);
  FaceParams f = ANCHORS[0];
  uint64_t base = 0;
  int shots = 0, bursts = 0;
  bool inB = false;
  unsigned burstStart = 0;
  for (unsigned now = 0; now <= 20000; now += 16) {
    r.render(f, Overlay{}, now, now == 0);
    const uint16_t* fb = desktop::framebuffer();
    uint64_t h = 1469598103934665603ull;
    for (int i = 0; i < desktop::W * desktop::H; i++) h = (h ^ fb[i]) * 1099511628211ull;
    if (now == 2000) base = h;
    if (now > 2000) {
      bool diff = h != base;
      if (diff && !inB) {
        bursts++;
        burstStart = now;
      }
      if (!diff && inB) printf("burst %d: %u..%u ms\n", bursts, burstStart, now);
      inB = diff;
      if (diff && shots < 12 && (now - burstStart) % 32 == 0) {
        char p[400];
        snprintf(p, sizeof p, "%s/s%d.ppm", argv[1], shots++);
        save(p);
      }
    }
  }
  printf("bursts in 20 s: %d\n", bursts);
}
