#include <stdio.h>
#include <chrono>
#include "canvas.h"
#include "platform_lcd.h"
#include "renderer.h"
#include "style.h"
int main(int argc, char** argv) {
  Lcd lcd;
  FaceRenderer r;
  r.begin(&lcd);
  for (int a = 1; a < argc; a++) {
    FaceStyle st = styleInitial();
    styleApplyText(st, argv[a]);
    r.setStyle(st);
    FaceParams f = ANCHORS[0];
    r.render(f, Overlay{}, 0, true);
    auto t0 = std::chrono::steady_clock::now();
    for (unsigned now = 16; now < 16 * 2000; now += 16) r.render(f, Overlay{}, now, false);
    double us =
        std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / 1999;
    printf("%-30s %.1f us/frame (regions: %lu %lu %lu us total)\n", argv[a], us, (unsigned long)r.regionUs[0],
           (unsigned long)r.regionUs[1], (unsigned long)r.regionUs[2]);
  }
}
