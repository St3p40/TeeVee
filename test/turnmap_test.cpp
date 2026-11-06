#include <cstdio>
#include <cmath>
#include "mapper.h"
#include "renderer.h"
#include "render_common.h"
static uint16_t fb[240][320];
void Lcd::push(int x, int y, int w, int h, const uint16_t* p) {
  for (int r = 0; r < h; r++)
    for (int c = 0; c < w; c++) fb[y + r][x + c] = p[r * w + c];
}
void Lcd::wait() {}
void Lcd::fillScreen(uint16_t c) {
  for (auto& row : fb)
    for (auto& v : row) v = (uint16_t)(c >> 8 | c << 8);
}
void Lcd::begin(uint8_t) {}
void Lcd::setMadctl(uint8_t) {}
void Lcd::setBrightness(uint8_t) {}
void Lcd::cmd(uint8_t, const uint8_t*, int) {}
void Lcd::window(int, int, int, int) {}
static FaceRenderer r;
int main() {
  Lcd lcd;
  r.begin(&lcd);
  FaceStyle st;
  styleApplyPreset(st, "round");
  r.setStyle(st);
  int fails = 0;
  for (float turn : {30.f, -30.f}) {
    r.setTurn(0, turn);
    for (int i = 0; i < 80; i++) r.render(ANCHORS[A_NEUTRAL], Overlay{}, i * 16, true);
    const uint16_t bg = fb[2][2];
    int ex = -1, ey = rd::EYE_CY;
    for (int x = 319; x >= 170 && ex < 0; x--)
      if (fb[ey][x] != bg) ex = x;
    float fx, fy;
    const bool hit = r.screenToFace((float)ex, (float)ey, fx, fy);
    const float eyeL = rd::EYE_CX_R - st.eye_w / 2 - 2, eyeR = rd::EYE_CX_R + st.eye_w / 2 + 2;
    const bool inEye = hit && fx >= eyeL && fx <= eyeR;
    printf(
        "  turn %+.0f: eye edge on screen x %d -> face x %.1f (eye %.0f..%.0f) %s; the raw screen x %d %s the eye\n",
        turn, ex, fx, eyeL, eyeR, inEye ? "ok" : "MISS", ex,
        (ex >= eyeL && ex <= eyeR) ? "would be in" : "would miss");
    fails += !inEye;
    float cx, cy;
    r.screenToFace(160, 120, cx, cy);
    printf("  screen centre -> face (%.1f, %.1f)\n", cx, cy);
  }
  r.setTurn(0, 0);
  for (int i = 0; i < 80; i++) r.render(ANCHORS[A_NEUTRAL], Overlay{}, i * 16, true);
  float fx, fy;
  r.screenToFace(77, 55, fx, fy);
  printf("  flat: (77, 55) -> (%.0f, %.0f) %s\n", fx, fy, fx == 77 && fy == 55 ? "ok" : "WRONG");
  fails += !(fx == 77 && fy == 55);
  printf(fails ? "turnmap_test: %d FAILED\n" : "turnmap_test: ok\n", fails);
  return fails ? 1 : 0;
}
