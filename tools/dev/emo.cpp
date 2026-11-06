#include <stdio.h>
#include <stdlib.h>
#include "canvas.h"
#include "platform_lcd.h"
#include "renderer.h"
#include "style.h"
#include "emotes.h"
int main(int argc, char** argv) {
  Lcd lcd;
  FaceRenderer r;
  r.begin(&lcd);
  FaceStyle st = styleInitial();
  styleApplyText(st, argv[2]);
  r.setStyle(st);
  int id = emoteIndex(argv[3]);
  unsigned t = (unsigned)atoi(argv[4]);
  FaceParams f = emoteFace((uint8_t)id, t / (float)EMOTE_MS[id]);
  Overlay ov{EMOTE_PARTS[id].eyes, EMOTE_PARTS[id].deco, "", 0, EMOTE_MS[id]};
  r.render(f, ov, 0, true);
  for (unsigned now = 16; now <= t; now += 16)
    r.render(emoteFace((uint8_t)id, now / (float)EMOTE_MS[id]), ov, now, false);
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
