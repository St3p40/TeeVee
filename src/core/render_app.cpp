#include "render_common.h"

using namespace rd;

void FaceRenderer::enterApp() {
  _lcd->fillScreen(0);
  _appValid = false;
  forgetMargins();
}

void FaceRenderer::enterFace() {
  _lcd->fillScreen(BG);
  _valid = _wValid = false;
  forgetMargins();
}

void FaceRenderer::drawMiniFace(Canvas& s, int ox, int oy, const AppCmd& c, void* ctx) {
  const FaceParams& p = ((FaceRenderer*)ctx)->_appFace;
  const float k = c.w / 320.f, cx0 = (float)(c.x - ox), cy0 = (float)(c.y - oy);
  auto X = [&](float x) { return cx0 + (x - 160.f) * k; };
  auto Y = [&](float y) { return cy0 + (y - 135.f) * k; };
  const uint16_t fg = faceColor(p);
  for (int side = 0; side < 2; side++) {
    const EyeBox e = eyeBox(p, side == 0);
    const float w = fmaxf(e.w * k, 2.f), h = fmaxf(e.h * k, 2.f);
    const int rad = (int)(S.eye_round * fminf(w, h) / 2);
    s.fillRoundRectAA((int)lroundf(X((float)e.x)), (int)lroundf(Y((float)e.y)), (int)lroundf(w),
                      (int)lroundf(h), rad, fg);
    const uint8_t ps = pupilStyleOf(side == 0);
    if (ps >= 1 && ps != 6 && h > 8) {
      const float pr = fminf(w, h) * S.pupil_size, room = fminf(w, h) / 2 - pr - 1;
      s.fillCircleAA(X(e.x + e.w / 2.f) + clampf(p.pupil_x, -1, 1) * room,
                     Y(e.y + e.h / 2.f) + clampf(p.pupil_y, -1, 1) * room, pr, BG);
    }
  }
  const MouthShape m = mouthShape(p);
  const int n = (int)fmaxf(8.f, 2.f * m.hw * k);
  const float lr = fmaxf(1.f, S.line_r * k);
  const bool open = m.open > 0.03f;
  float px = 0, py = 0, qx = 0, qy = 0;
  for (int i = 0; i <= n; i++) {
    const float u = -1.f + 2.f * i / n, x = X(160.f + u * m.hw), yt = Y(m.up(u)), yb = Y(m.lo(u));
    if (open && yb - yt > 1.f)
      s.fillRect((int)lroundf(x), (int)lroundf(yt), 1, (int)lroundf(yb - yt) + 1,
                 S.mouth_solid ? fg : faceColor(p, 0.37f));
    if (i) {
      s.fillCapsule(px, py, x, yt, lr, fg);
      if (open) s.fillCapsule(qx, qy, x, yb, lr, fg);
    }
    px = x;
    py = yt;
    qx = x;
    qy = yb;
  }
}

// How the face looks, as a number: changes whenever anything visible in the mini face changes (quantised, so a settling spring stops repainting).
static uint32_t miniFaceKey(const FaceParams& p) {
  const float v[] = {p.eye_open_l, p.eye_open_r,  p.pupil_x, p.pupil_y, p.mouth_curve, p.mouth_open,
                     p.mouth_skew, p.mouth_width, p.squash,  p.asym,    p.hue};
  uint32_t h = 2166136261u + (uint32_t)S.hue + (uint32_t)S.mouth_style * 7u;
  for (float f : v) h = (h ^ (uint32_t)(int32_t)lroundf(f * 40.f)) * 16777619u;
  return h;
}

int FaceRenderer::renderApp(const AppFrame& f) {
  const uint32_t faceKey = miniFaceKey(_appFace);
  Canvas tile[2];
  for (int i = 0; i < 2; i++) tile[i].attach(_strip[i].buffer(), APP_TILE_W, APP_TILE_H);
  int painted = 0, cur = 0;
  for (int ty = 0; ty < APP_TILES_Y; ty++)
    for (int tx = 0; tx < APP_TILES_X; tx++) {
      const int idx = ty * APP_TILES_X + tx;
      const uint32_t h = appTileHash(f, tx, ty, faceKey);
      if (_appValid && h == _appHash[idx]) continue;
      Canvas& s = tile[cur];  // ping-pong: draw into one while the other goes out by DMA
      s.fill(0);
      appReplay(f, s, tx * APP_TILE_W, ty * APP_TILE_H, drawMiniFace, this);
      _lcd->push(tx * APP_TILE_W, ty * APP_TILE_H, APP_TILE_W, APP_TILE_H, s.buffer());
      cur ^= 1;
      _appHash[idx] = h;
      painted++;
    }
  _lcd->wait();
  _appValid = true;
  return painted;
}
