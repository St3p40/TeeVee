#include "render_common.h"

using namespace rd;

static void fitText(Canvas& s, const char* t, int cx, int cy, int maxW, const BitmapFont& f, uint16_t c) {
  if (Canvas::textWidth(t, f) <= maxW) return s.drawText(t, cx, cy, f, c);
  char a[40], b[40];
  int best = -1, bestW = 1 << 30;
  const int n = (int)strlen(t);
  for (int i = 1; i < n && i < (int)sizeof a - 1; i++) {
    if (t[i] != ' ' && t[i] != '-' && t[i] != '.') continue;
    const int cut = t[i] == ' ' ? i : i + 1;
    snprintf(a, sizeof a, "%.*s", cut, t);
    snprintf(b, sizeof b, "%s", t + i + 1);
    const int wa = Canvas::textWidth(a, f), wb = Canvas::textWidth(b, f), w = wa > wb ? wa : wb;
    if (w <= maxW && w < bestW) best = i, bestW = w;
  }
  if (best < 0) return s.drawText(t, cx, cy, f, c);
  snprintf(a, sizeof a, "%.*s", t[best] == ' ' ? best : best + 1, t);
  snprintf(b, sizeof b, "%s", t + best + 1);
  const int half = (f.height >> 1) + 1;
  s.drawText(a, cx, cy - half, f, c);
  s.drawText(b, cx, cy + half, f, c);
}

int FaceRenderer::renderWidgets(const Widgets& in) {
#ifdef TEEVEE_BOARD_AMOLED
  // The panel has room above and below the face: the info goes there, the corners stay clear. The bands are outside the face,
  // so they are kept up to date while the head is turned too (the corner panels below are not: the turned face covers them).
  return renderMargins(in);
#endif
  if (_cubeNow) return 0;
  Widgets w = in;
  if (!w.panels) {
    w.clock[0] = w.leftSub[0] = w.temp[0] = w.weather[0] = w.ciName[0] = 0;
    w.ci = CI_NONE;
    w.pomoActive = false;
  }
  const Widgets& q = _lastW;
  bool left = !_wValid || strcmp(w.clock, q.clock) || strcmp(w.leftSub, q.leftSub) ||
              w.pomoActive != q.pomoActive || w.panels != q.panels;
  bool right = !_wValid || strcmp(w.temp, q.temp) || strcmp(w.weather, q.weather) || w.ci != q.ci ||
               strcmp(w.ciName, q.ciName) || w.setup != q.setup || strcmp(w.apSsid, q.apSsid) ||
               strcmp(w.apPass, q.apPass) || strcmp(w.pin, q.pin);
  if (!left && !right) return 0;
  Canvas& s = _wsprite;
  if (left) {
    drawPanel(s, w, false, 0);
    _lcd->push(0, 240 - WID_H, WID_W, WID_H, s.buffer());
    _lcd->wait();
  }
  if (right) {
    drawPanel(s, w, true, 0);
    _lcd->push(320 - WID_W, 240 - WID_H, WID_W, WID_H, s.buffer());
  }
  _lcd->wait();
  _lastW = w;
  _wValid = true;
  return left + right;
}

// One corner panel into s, whose top row is panel row oy (the glitch pass draws it a strip at a time).
void FaceRenderer::drawPanel(Canvas& s, const Widgets& w, bool right, int oy) const {
  const uint16_t fg = hsv565(S.hue, S.sat, S.val), dim = hsv565(S.hue, S.sat * 0.75f, S.val * 0.58f);
  const int lx0 = _visX0, lx1 = WID_W, rx0 = 0, rx1 = _visX1 - (320 - WID_W);
  const int lcx = (lx0 + lx1) / 2, lw = lx1 - lx0 - 4, rcx = (rx0 + rx1) / 2, rw = rx1 - rx0 - 4;
  s.fill(BG);
  if (!right) {
    if (w.panels) s.drawText(w.clock[0] ? w.clock : "--:--", lcx, 24 - oy, FONT_LARGE, w.clock[0] ? fg : dim);
    fitText(s, w.leftSub, lcx, 54 - oy, lw, FONT_SMALL, w.pomoActive ? fg : dim);
  } else if (w.setup) {
    s.drawText(Canvas::textWidth("Wi-Fi setup", FONT_SMALL) <= rw ? "Wi-Fi setup" : "Wi-Fi", rcx, 12 - oy,
               FONT_SMALL, dim);
    fitText(s, w.apSsid, rcx, 34 - oy, rw, FONT_SMALL, fg);
    fitText(s, w.apPass, rcx, 56 - oy, rw, FONT_SMALL, fg);
  } else if (w.pin[0]) {  // web page login in progress
    s.drawText("web PIN", rcx, 14 - oy, FONT_SMALL, dim);
    s.drawText(w.pin, rcx, 42 - oy, FONT_LARGE, fg);
  } else {
    if (w.temp[0]) {
      s.drawText(w.temp, rcx, 16 - oy, FONT_LARGE, fg);
      fitText(s, w.weather, rcx, 39 - oy, rw, FONT_SMALL, dim);
    }
    if (w.ci != CI_NONE) {
      static const char* const CI_TEXT[] = {"", "pass", "FAIL", "running"};
      const uint16_t ciCol[] = {dim, Canvas::rgb(80, 220, 120), Canvas::rgb(255, 40, 40),
                                Canvas::rgb(255, 180, 40)};
      char buf[32];
      snprintf(buf, sizeof buf, "%s %s", w.ciName[0] ? w.ciName : "CI", CI_TEXT[w.ci]);
      s.drawText(buf, rcx, 59 - oy, FONT_SMALL, ciCol[w.ci]);
    }
  }
}

#ifdef TEEVEE_BOARD_AMOLED
// AMOLED only: the panel is 368 x 448 and the face sits in the middle, so the bands of 104 rows above and below it are free.
// The clock goes in the top band; the weather, the CI and the Wi-Fi setup or web PIN in the bottom one. Each band is sent in
// strips of 26 rows (even, as the panel needs). The face keeps its corners.
namespace {
constexpr int MB_W = 368, MB_BAND = 104, MB_STRIP = 26, MB_BOTTOM = 448 - MB_BAND;
}

// One strip of a band into s. oy is the strip's top row within the band; the items are placed in band rows.
void FaceRenderer::drawMarginStrip(Canvas& s, const Widgets& w, bool bottom, int oy) const {
  const uint16_t fg = hsv565(S.hue, S.sat, S.val), dim = hsv565(S.hue, S.sat * 0.75f, S.val * 0.58f);
  const int cx = MB_W / 2;
  s.fill(BG);
  if (!bottom) {
    if (w.panels) s.drawText(w.clock[0] ? w.clock : "--:--", cx, 40 - oy, FONT_LARGE, w.clock[0] ? fg : dim);
    fitText(s, w.leftSub, cx, 84 - oy, MB_W - 40, FONT_SMALL, w.pomoActive ? fg : dim);
    if (w.battery) {
      // The battery, top right (clear of the panel's round corner): a small cell that fills with the charge, and the number under
      // it. Red when nearly empty; a bar beside the cell while it charges.
      const int pct = w.battery - 1, bx = MB_W - 78, by = 22 - oy, bw = 30, bh = 14;
      const uint16_t col = pct <= 15 && !w.charging ? Canvas::rgb(255, 40, 40) : dim;
      s.fillRect(bx, by, bw, bh, col);
      s.fillRect(bx + 2, by + 2, bw - 4, bh - 4, BG);
      s.fillRect(bx + bw, by + 4, 3, bh - 8, col);
      const int fill = (bw - 6) * pct / 100;
      if (fill > 0) s.fillRect(bx + 3, by + 3, fill, bh - 6, pct <= 15 && !w.charging ? col : fg);
      if (w.charging) s.fillRect(bx - 6, by + 2, 3, bh - 4, fg);
      char t[8];
      snprintf(t, sizeof t, "%d%%", pct);
      s.drawText(t, bx + bw / 2, 52 - oy, FONT_SMALL, col);
    }
  } else if (w.setup) {
    s.drawText(Canvas::textWidth("Wi-Fi setup", FONT_SMALL) <= MB_W - 40 ? "Wi-Fi setup" : "Wi-Fi", cx, 14 - oy,
               FONT_SMALL, dim);
    fitText(s, w.apSsid, cx, 44 - oy, MB_W - 40, FONT_SMALL, fg);
    fitText(s, w.apPass, cx, 74 - oy, MB_W - 40, FONT_SMALL, fg);
  } else if (w.pin[0]) {  // web page login in progress
    s.drawText("web PIN", cx, 22 - oy, FONT_SMALL, dim);
    s.drawText(w.pin, cx, 60 - oy, FONT_LARGE, fg);
  } else {
    if (w.temp[0]) {
      s.drawText(w.temp, cx, 26 - oy, FONT_LARGE, fg);
      fitText(s, w.weather, cx, 58 - oy, MB_W - 40, FONT_SMALL, dim);
    }
    if (w.ci != CI_NONE) {
      static const char* const CI_TEXT[] = {"", "pass", "FAIL", "running"};
      const uint16_t ciCol[] = {dim, Canvas::rgb(80, 220, 120), Canvas::rgb(255, 40, 40),
                                Canvas::rgb(255, 180, 40)};
      char buf[32];
      snprintf(buf, sizeof buf, "%s %s", w.ciName[0] ? w.ciName : "CI", CI_TEXT[w.ci]);
      s.drawText(buf, cx, 84 - oy, FONT_SMALL, ciCol[w.ci]);
    }
  }
}

int FaceRenderer::renderMargins(const Widgets& in) {
  Widgets w = in;
  if (!w.panels) {
    w.clock[0] = w.leftSub[0] = w.temp[0] = w.weather[0] = w.ciName[0] = 0;
    w.ci = CI_NONE;
    w.pomoActive = false;
  }
  const Widgets& q = _marginW;
  const bool top = !_marginValid || strcmp(w.clock, q.clock) || strcmp(w.leftSub, q.leftSub) ||
                   w.pomoActive != q.pomoActive || w.panels != q.panels || w.battery != q.battery || w.charging != q.charging;
  const bool bottom = !_marginValid || strcmp(w.temp, q.temp) || strcmp(w.weather, q.weather) || w.ci != q.ci ||
                      strcmp(w.ciName, q.ciName) || w.setup != q.setup || strcmp(w.apSsid, q.apSsid) ||
                      strcmp(w.apPass, q.apPass) || strcmp(w.pin, q.pin);
  if (!top && !bottom) return 0;
  if (!_msprite.buffer() && !_msprite.create(MB_W, MB_STRIP)) return 0;
  for (int band = 0; band < 2; band++) {
    if (!(band ? bottom : top)) continue;
    const int y0 = band ? MB_BOTTOM : 0;
    for (int oy = 0; oy < MB_BAND; oy += MB_STRIP) {
      drawMarginStrip(_msprite, w, band, oy);
      _lcd->pushRows(y0 + oy, MB_STRIP, _msprite.buffer());
      _lcd->wait();  // the strip is drawn again next time: it has to be sent first
    }
  }
  _marginW = w;
  _marginValid = true;
  return top + bottom;
}
#endif
