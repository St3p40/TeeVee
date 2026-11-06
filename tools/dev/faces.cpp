#include <stdio.h>
#include <stdlib.h>
#include "canvas.h"
#include "platform_lcd.h"
#include "renderer.h"
#include "style.h"
int main(int argc, char** argv) {
  Lcd lcd;
  FaceRenderer r;
  r.begin(&lcd);
  FaceStyle st = styleInitial();
  styleApplyText(st, argv[2]);
  r.setStyle(st);
  FaceParams f = ANCHORS[0];
  f.mouth_curve = (float)atof(argv[4]);
  f.mouth_open = (float)atof(argv[5]);
  unsigned t = (unsigned)atoi(argv[3]);
  for (unsigned now = 0; now <= t; now += 16) r.render(f, Overlay{}, now, now == 0);
  static uint32_t argb[desktop::W * desktop::H];
  desktop::toArgb(argb);
  FILE* fp = fopen(argv[1], "wb");
  fprintf(fp, "P6\n%d %d\n255\n", desktop::W, desktop::H);
  for (int i = 0; i < desktop::W * desktop::H; i++) {
    unsigned char c[3] = {(unsigned char)(argb[i] >> 16), (unsigned char)(argb[i] >> 8),
                          (unsigned char)argb[i]};
    fwrite(c, 1, 3, fp);
  }
  fclose(fp);
}
