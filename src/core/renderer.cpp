#include "renderer.h"
#include "platform.h"
#include "render_common.h"
#include <stdlib.h>

using namespace rd;

void FaceRenderer::begin(Lcd* lcd) {
  _lcd = lcd;
  for (auto& s : _strip) s.create(REG_W, STRIP_H);
  _wsprite.create(WID_W, WID_H);
  _style = S;
  BG = Canvas::rgb(S.bg[0], S.bg[1], S.bg[2]);
}

void FaceRenderer::setStyle(const FaceStyle& st) {
  if (!memcmp(&st, &_style, sizeof st)) return;
  _style = S = st;
  BG = Canvas::rgb(S.bg[0], S.bg[1], S.bg[2]);
  layout();
  _lcd->fillScreen(BG);
  _valid = _wValid = false;
  forgetMargins();
}

bool FaceRenderer::regionChanged(Region r, const FaceParams& p) const {
  const FaceParams& q = _last;
  bool shared = p.hue != q.hue || _glitchOn || _lastGlitch || _dayChanged || _pupilChanged;
  if (r == R_MOUTH)
    return shared || (S.mouth_style == 3 && MOUTH_PHASE != _lastPhase) || _deco == DF_TONGUE || _lastTongue ||
           p.mouth_curve != q.mouth_curve || p.mouth_open != q.mouth_open || p.mouth_skew != q.mouth_skew ||
           p.mouth_width != q.mouth_width;
  float open = r == R_EYE_L ? p.eye_open_l : p.eye_open_r;
  float lastOpen = r == R_EYE_L ? q.eye_open_l : q.eye_open_r;
  const uint8_t es = eyeStyleOf(r == R_EYE_L);
  return shared || ((es == 4 || es == 5) && MOUTH_PHASE != _lastPhase) || _eyes != EF_NONE ||
         _deco != DF_NONE || _lastActive || open != lastOpen || p.pupil_x != q.pupil_x ||
         p.pupil_y != q.pupil_y || p.brow_angle != q.brow_angle || p.brow_height != q.brow_height ||
         p.squash != q.squash || p.blush != q.blush || p.lid_bottom != q.lid_bottom || p.asym != q.asym;
}

void FaceRenderer::contentRows(Region r, const FaceParams& p, int& y0, int& y1) const {
  if (r == R_MOUTH && (_deco == DF_TONGUE || _lastTongue || _glitchOn || _lastGlitch)) {
    y0 = REGIONS[r].y;
    y1 = REGIONS[r].y + REGIONS[r].h;
  } else if (r == R_MOUTH) {
    MouthShape m = mouthShape(p);
    float k = fabsf(m.K);
    float top = MOUTH_Y - m.A / 2 + fminf(0.f, m.A) - k - m.wob,
          bot = MOUTH_Y - m.A / 2 + fmaxf(0.f, m.A) + k + m.open * MOUTH_DROP + m.wob;
    const int extra = m.kind == 5 ? matrixDot() : 0;
    y0 = (int)floorf(top) - (int)S.line_r - 2 - extra;
    y1 = (int)ceilf(bot) + (int)S.line_r + 3 + extra;
  } else {
    if (_eyes != EF_NONE || _deco != DF_NONE || _lastActive || _glitchOn || _lastGlitch) {
      y0 = REGIONS[r].y;
      y1 = REGIONS[r].y + REGIONS[r].h;
      return;
    }
    EyeBox e = eyeBox(p, r == R_EYE_L);
    y0 = e.y - 3;
    y1 = e.y + e.h + 2;
    if (S.lashes) y0 -= 8;
    if (S.brows) {
      Brow b = brow(p, e, r == R_EYE_L);
      y0 = min(y0, (int)floorf(fminf(b.yo, b.yi) - b.r) - 2);
    }
    if (p.blush > 0.02f) {
      y0 = min(y0, EYE_CY + BLUSH_DY - BLUSH_RY - 1);
      y1 = max(y1, EYE_CY + BLUSH_DY + BLUSH_RY + 2);
    }
  }
}

static constexpr float GLOW_CX = 160, GLOW_CY = 120, GLOW_RX = 122, GLOW_RY = 130;
// The glow's ring, 0..127, from whole numbers: the ellipse's squared distance times RX^2 * RY^2 is (x-160)^2 * RY^2 + (y-120)^2 * RX^2,
// and the high word of that times M (= 127 * 2^32 / (RX^2 * RY^2)) is 127 * d2 with no divide. About twice as fast as the float form on the ESP32-S3 at -O2.
static constexpr uint32_t GLOW_A = 130 * 130, GLOW_B = 122 * 122;  // RY^2 and RX^2
static constexpr uint32_t GLOW_M = (uint32_t)((127ull << 32) / (GLOW_A * GLOW_B));
static inline uint32_t glowCol(int x) {
  const int32_t d = x - (int)GLOW_CX;
  return (uint32_t)(d * d) * GLOW_A;
}
static inline uint32_t glowRow(int y) {
  const int32_t d = y - (int)GLOW_CY;
  return (uint32_t)(d * d) * GLOW_B;
}
static inline int glowRing(uint32_t sum) {
  const uint32_t i = (uint32_t)(((uint64_t)sum * GLOW_M) >> 32);
  return i >= 127 ? 127 : (int)i;
}
static inline int glowStep(int x, int y) { return glowRing(glowCol(x) + glowRow(y)); }

void FaceRenderer::buildGlow(uint16_t fg) {
  for (int i = 0; i < 128; i++) {
    const float t = fmaxf(0.f, 1.f - sqrtf(i / 127.f));
    _glowTab[i] = mix565(BG, fg, S.glow * t * t * 0.5f);
  }
}

// glowStep() for every pixel of the block, with its column and row terms worked out once each: no divisions per pixel (the ESP32's FPU has no
// divide instruction). Same arithmetic on the same values, so the same pixels. Columns go in chunks to keep the stack small.
void FaceRenderer::glowBlock(uint16_t* buf, int x0, int y0, int w, int h, int stride) const {
  constexpr int CHUNK = 64;
  uint32_t colT[CHUNK];
  for (int i0 = 0; i0 < w; i0 += CHUNK) {
    const int n = min(CHUNK, w - i0);
    for (int i = 0; i < n; i++) colT[i] = glowCol(x0 + i0 + i);
    for (int j = 0; j < h; j++) {
      const uint32_t rowT = glowRow(y0 + j);
      uint16_t* row = buf + j * stride + i0;
      for (int i = 0; i < n; i++) {
        const uint16_t c = _glowTab[glowRing(colT[i] + rowT)];
        row[i] = (uint16_t)(c >> 8 | c << 8);  // the canvas stores byte-swapped pixels
      }
    }
  }
}

uint16_t FaceRenderer::glowAt(int x, int y) const {
  return glowOn() ? _glowTab[glowStep(x, y)] : BG;
}

static constexpr uint16_t HOLE_MARK = 0x0841;  // (natural colour; the canvas stores it byte-swapped)

uint16_t FaceRenderer::holeColour() const { return glowOn() ? HOLE_MARK : BG; }

void FaceRenderer::resolveHoles(uint16_t* buf, int x0, int y0, int w, int h, int stride) const {
  if (!glowOn()) return;
  const uint16_t mark = (uint16_t)(HOLE_MARK >> 8 | HOLE_MARK << 8);
  for (int j = 0; j < h; j++)
    for (int i = 0; i < w; i++)
      if (buf[j * stride + i] == mark) {
        const uint16_t c = _glowTab[glowStep(x0 + i, y0 + j)];
        buf[j * stride + i] = (uint16_t)(c >> 8 | c << 8);
      }
}

// Paints a rectangle that no dirty region covers (strip by strip, through the same DMA buffers).
void FaceRenderer::paintGlowBlock(int x, int y, int w, int h) {
  for (int yy = y; yy < y + h; yy += STRIP_H) {
    const int hh = min(STRIP_H, y + h - yy);
    Canvas& s = _strip[_cur];
    glowBlock(s.buffer(), x, yy, w, hh, w);
    _lcd->push(x, yy, w, hh, s.buffer());
    _cur ^= 1;
  }
}

int FaceRenderer::render(const FaceParams& p, const Overlay& ov, uint32_t nowMs, bool force) {
  // The eyes and mouth moved since the last frame (the tilt): every region is drawn again.
  const bool shifted = _parEX != _parShownEX || _parEY != _parShownEY || _parMX != _parShownMX || _parMY != _parShownMY;
  _parShownEX = _parEX, _parShownEY = _parEY, _parShownMX = _parMX, _parShownMY = _parMY;
  const bool fullRedraw = force || _wasCube || shifted, leftCube = _wasCube;
  _wasCube = false;
  _cubeNow = false;
  const bool on = (ov.eyes != EF_NONE || ov.deco != DF_NONE) && ov.duration && nowMs - ov.start < ov.duration;
  _eyes = on ? ov.eyes : (uint8_t)EF_NONE;
  _deco = on ? ov.deco : (uint8_t)DF_NONE;
  memcpy(_text, ov.text, sizeof _text);
  _text[sizeof _text - 1] = 0;
  _emT = on ? (nowMs - ov.start) / (float)ov.duration : 0;
  _turnXShown += (_turnXWant - _turnXShown) * 0.2f;
  _turnYShown += (_turnYWant - _turnYShown) * 0.2f;
  if (fabsf(_turnXShown) < 0.02f && fabsf(_turnXWant) < 0.02f) _turnXShown = 0;
  if (fabsf(_turnYShown) < 0.02f && fabsf(_turnYWant) < 0.02f) _turnYShown = 0;
  _rollShown += (_rollWant - _rollShown) * 0.2f;
  if (fabsf(_rollShown) < 0.02f && fabsf(_rollWant) < 0.02f) _rollShown = 0;
  const int dayKey = (int)lroundf(_dayHue) * 64 + (int)lroundf(_dayVal * 40);
  _dayChanged = dayKey != _dayKey;
  if (_dayChanged) {
    _dayKey = dayKey;
    DAY_HUE = (float)lroundf(_dayHue);
    DAY_VAL = lroundf(_dayVal * 40) / 40.f;
  }
  const float pm = lroundf(_pupilAsk * 20.f) / 20.f;  // 1/20 steps, so a slow change redraws rarely
  _pupilChanged = pm != PUPIL_MUL;
  PUPIL_MUL = pm;
  MOUTH_PHASE = (nowMs / 40) * 40 * 0.0065f;
  updateGlitch(nowMs);
#ifndef TEEVEE_NO_WARP
  if (_turnXShown != 0.f || _turnYShown != 0.f || _rollShown != 0.f || _fisheye > 0.f) return renderWarped(p);
#endif
  // Back from the turned face: the whole screen once (not on every turned frame: that cleared 368 x 448 for nothing)
  if (leftCube) {
    _wValid = false;
    _lcd->fillScreen(BG);
    forgetMargins();
  }
  int n = 0;
  bool dirty[R_COUNT];
  for (int r = 0; r < R_COUNT; r++) {
    dirty[r] = fullRedraw || !_valid || regionChanged((Region)r, p);
    n += dirty[r];
  }
  if (!n) return 0;
  const bool glitchEnd = _lastGlitch && !_glitchOn;
  if (glowOn()) buildGlow(faceColor(p));
  if (_glitchOn || glitchEnd) {  // the whole width; once a glitch ends, the usual boxes go over it too
    drawFullRows(p, 0, APP_H);
    if (glitchEnd) _wValid = false;  // renderWidgets puts the panels back
    if (_glitchOn) {
      for (int r = 0; r < R_COUNT; r++) {
        int c0, c1;
        contentRows((Region)r, p, c0, c1);
        _drawn[r][0] = c0;
        _drawn[r][1] = c1;
        dirty[r] = false;
      }
      _bridgeShown = _deco == DF_GLASSES;
    }
  }
  if (glowOn()) {
    const uint16_t fg = faceColor(p);
    if (fullRedraw || !_valid || fg != _glowFg || glitchEnd) {
      const int gapX = REGIONS[R_EYE_L].x + REG_W,
                gapW = REGIONS[R_EYE_R].x - gapX;  // (the eye regions move with eye_gap)
      if (gapW > 0 && gapW <= REG_W) paintGlowBlock(gapX, 0, gapW, 170);
      paintGlowBlock(39, 170, 65, 70);
      paintGlowBlock(216, 170, 65, 70);
    }
    _glowFg = fg;
  }
  const int gapX = REGIONS[R_EYE_L].x + REG_W, gapW = REGIONS[R_EYE_R].x - gapX;
  if (!_glitchOn && gapW > 0 && gapW <= REG_W && (_deco == DF_GLASSES || _bridgeShown)) {
    const bool on = _deco == DF_GLASSES;
    Canvas gap;
    gap.attach(_strip[_cur].buffer(), gapW, STRIP_H);
    if (glowOn())
      glowBlock(gap.buffer(), gapX, BRIDGE_Y, gapW, STRIP_H, gapW);
    else
      gap.fill(BG);
    if (on) {
      const float y = glassesY(eyeBox(p, true)) - BRIDGE_Y;
      gap.fillCapsule(-2, y, gapW + 2.f, y, 1.5f, GLASSES);
    }
    _lcd->push(gapX, BRIDGE_Y, gapW, STRIP_H, gap.buffer());
    _cur ^= 1;
    _bridgeShown = on;
  }
  for (int r = 0; r < R_COUNT; r++)
    if (dirty[r]) {
      uint32_t t0 = tvMicros();
      const Rect& rc = REGIONS[r];
      int y0, y1;
      contentRows((Region)r, p, y0, y1);
      int d0 = y0, d1 = y1;
      if (fullRedraw || !_valid) {
        d0 = rc.y;
        d1 = rc.y + rc.h;
      } else {
        d0 = min(d0, (int)_drawn[r][0]);
        d1 = max(d1, (int)_drawn[r][1]);
      }
      const int N = pixelSize();
      if (N) {  // dirty rows on whole pixel blocks, so a block is never half redrawn
        d0 = d0 / N * N;
        d1 = (d1 + N - 1) / N * N;
      }
      drawRegion((Region)r, p, max(d0, (int)rc.y), min(d1, rc.y + rc.h));
      _drawn[r][0] = y0;
      _drawn[r][1] = y1;
      regionUs[r] += tvMicros() - t0;
    }
  _lcd->wait();  // last DMA transfer done before params change
  _last = p;
  _lastActive = _eyes != EF_NONE || _deco != DF_NONE;
  _lastTongue = _deco == DF_TONGUE;
  _lastGlitch = _glitchOn;
  _lastPhase = MOUTH_PHASE;
  _valid = true;
  return n;
}

int pixelSize() {
  const float v = S.pixel;
  if (v < 1.f) return 0;
  return v < 3.f ? 2 : v < 4.5f ? 4 : v < 6.5f ? 5 : 8;
}

uint32_t FaceRenderer::glRnd() {
  _glRng ^= _glRng << 13;
  _glRng ^= _glRng >> 17;
  _glRng ^= _glRng << 5;
  return _glRng;
}

enum GlKind : uint8_t { GL_SLIP, GL_INVERT, GL_SNOW, GL_SMEAR, GL_ROLL, GL_KINDS };

void FaceRenderer::updateGlitch(uint32_t now) {
  // style.glitch is the master switch: 0 = never, whatever the mind net wants. Above 0 it is the baseline level, and the mind net's glitch urge
  // (only when it is clearly up: a calm net sits just above zero) can raise it up to 10.
  const float net = _drive > 0.15f ? 1.f + 9.f * _drive : 0.f;
  const bool quiet = _glScale < 0.3f && net <= 0.f;
  const float level = S.glitch <= 0.f ? 0.f
                      : quiet         ? fminf(S.glitch * _glScale, 0.3f)
                                      : fmaxf(S.glitch * _glScale, net);
  if (level <= 0.02f) {
    _glitchOn = false;
    _glNext = 0;
    _glSched = 0;
    return;
  }
  const float t = level <= 1.f ? 0.f : fminf(1.f, (level - 1.f) / 9.f);
  const float rare = level < 1.f ? level : 1.f;
  auto lerp = [&](float a, float b) { return a + (b - a) * t; };
  auto gap = [&]() {
    return now + (uint32_t)((lerp(2500.f, 900.f) + glRnd() % (uint32_t)lerp(6000.f, 3600.f)) / rare);
  };
  if (!_glNext || level > 1.6f * _glSched) {
    _glNext = gap();
    _glSched = level;
  }
  if (!_glitchOn && (int32_t)(now - _glNext) >= 0) {
    const bool tic = glRnd() % 100 < 40 * t || quiet;  // a short one-band tic instead of a burst
    _glitchOn = true;
    _glBig = !tic;
    _glEnd =
        now + (tic ? 40 + glRnd() % 50 : (uint32_t)lerp(150 + glRnd() % 200, (200 + glRnd() % 250) * 1.7f));
    _glRoll = 0;
  } else if (_glitchOn && (int32_t)(now - _glEnd) >= 0) {
    _glitchOn = false;
    _glNext = gap();
    _glSched = level;
  }
  if (_glitchOn && (int32_t)(now - _glRoll) >= 0) {
    _glBands = _glBig ? 1 + glRnd() % (3 + (int)lroundf(2 * t)) : 1;
    for (int i = 0; i < _glBands; i++) {
      const int y0 = i == 0 ? EYE_CY - 40 + (int)(glRnd() % 80)
                            : EYE_CY - 50 + (int)(glRnd() % (MOUTH_Y - EYE_CY + 70)),
                h = 4 + (int)(glRnd() % (uint32_t)lerp(16, 26));
      const uint32_t k = glRnd() % 100, slip = (uint32_t)lerp(100, 40), rest = (100 - slip) / 4 + 1;
      const uint32_t other = 1 + (k - slip) / rest;
      const uint8_t kind = i == 0 || k < slip ? (uint8_t)GL_SLIP : (uint8_t)(other > 4 ? 4 : other);
      _glBand[i] = {(int16_t)y0,
                    (int16_t)(y0 + h),
                    (int8_t)((glRnd() & 1 ? 1 : -1) * (6 + (int)(glRnd() % (uint32_t)lerp(17, 37)))),
                    (int8_t)(1 + glRnd() % 5),
                    kind,
                    (uint8_t)glRnd()};
    }
    if (quiet) {
      _glBand[0].dx = (int8_t)((glRnd() & 1 ? 1 : -1) * (1 + (int)(glRnd() % 3)));
      _glBand[0].split = 1;
    }
    _glFlash = _glBig && glRnd() % 100 < 35 * t ? 1 + glRnd() % 2 : 0;
    _glRoll = now + 45 + glRnd() % 40;
  }
  if (!_glitchOn) _glFlash = 0;
}

void FaceRenderer::postProcess(uint16_t* buf, int ox, int oy, int w, int h, bool split, bool pixelated) const {
  if (const int N = pixelated ? 0 : pixelSize()) {  // pixelated: the caller has already made the blocks (the same ones)
    for (int j = 0; j < h;) {
      const int rows = min(N - (oy + j) % N, h - j), sr = j + (rows >> 1);
      for (int i = 0; i < w;) {
        const int cols = min(N - (ox + i) % N, w - i);
        const uint16_t c = buf[sr * w + i + (cols >> 1)];
        for (int y = j; y < j + rows; y++)
          for (int x = i; x < i + cols; x++) buf[y * w + x] = c;
        i += cols;
      }
      j += rows;
    }
  }
  static uint16_t row[APP_W];
  auto sw = [](uint16_t c) { return (uint16_t)(c >> 8 | c << 8); };  // the buffer holds byte-swapped RGB565
  if (const int d = split ? min((int)lroundf(S.rgb_split), w - 1) : 0) {
    for (int y = 0; y < h; y++) {
      uint16_t* line = buf + y * w;
      for (int x = 0; x < w; x++) {
        const uint16_t rc = sw(line[min(x + d, w - 1)]), gc = sw(line[x]), bc = sw(line[max(x - d, 0)]);
        row[x] = sw((uint16_t)((rc & 0xF800) | (gc & 0x07E0) | (bc & 0x001F)));
      }
      memcpy(line, row, w * sizeof(uint16_t));
    }
  }
  if (!_glitchOn) return;
  // A negative that keeps the brightness: each colour becomes its opposite hue (max + min - c), so black stays black and the background does
  // not flash white (a plain ~c turned the whole dark picture white, and the see-through desktop pet into a white box).
  auto neg = [](uint16_t c) {
    const int r = (c >> 11) << 1, g = (c >> 5) & 63, b = (c & 31) << 1;
    const int hi = max(r, max(g, b)), lo = min(r, min(g, b));
    return (uint16_t)(((hi + lo - r) >> 1) << 11 | (hi + lo - g) << 5 | (hi + lo - b) >> 1);
  };
  const uint16_t bgS = sw(BG);
  if (_glFlash) {
    for (int i = 0; i < w * h; i++) {
      if (buf[i] == bgS) continue;
      const uint16_t c = sw(buf[i]);
      const uint16_t o = _glFlash == 1 ? (uint16_t)((c & 0x07E0) | (c >> 11) | (c & 0x1F) << 11) : neg(c);
      buf[i] = sw(o);
    }
  }
  for (int b = 0; b < _glBands; b++) {
    const GlBand& g = _glBand[b];
    for (int y = max((int)g.y0, oy); y < min((int)g.y1, oy + h); y++) {
      uint16_t* line = buf + (y - oy) * w;
      switch (g.kind) {
        case GL_SLIP:
          for (int x = 0; x < w; x++) {
            const int sx = x - g.dx, rx = sx - g.split, bx = sx + g.split;
            const uint16_t gc = line[min(max(sx, 0), w - 1)], rc = line[min(max(rx, 0), w - 1)],
                           bc = line[min(max(bx, 0), w - 1)];  // (past the edge: the edge colour runs on)
            row[x] = sw((uint16_t)((sw(rc) & 0xF800) | (sw(gc) & 0x07E0) | (sw(bc) & 0x001F)));
          }
          break;
        case GL_INVERT:
          for (int x = 0; x < w; x++) row[x] = line[x] == bgS ? line[x] : sw(neg(sw(line[x])));
          break;
        case GL_SNOW:
          for (int x = 0; x < w; x++) {
            uint32_t hsh =
                (uint32_t)((x >> 1) * 2654435761u) ^ (uint32_t)(y * 40503u) ^ ((uint32_t)g.seed * 9973u);
            hsh ^= hsh >> 13;
            hsh *= 0x5bd1e995u;
            hsh ^= hsh >> 15;
            row[x] = hsh & 3 ? line[x] : (hsh & 4 ? (uint16_t)0xFFFF : bgS);
          }
          break;
        case GL_SMEAR: {
          const int from = (g.seed % (w >> 1)) + (w >> 3), len = 16 + g.seed % 40;
          for (int x = 0; x < w; x++) row[x] = x >= from && x < from + len ? line[from] : line[x];
          break;
        }
        default: {
          const int sy = min(max(y - oy + (g.dx > 0 ? 6 : -6), 0), h - 1);
          memcpy(row, buf + sy * w, w * sizeof(uint16_t));
        }
      }
      memcpy(line, row, w * sizeof(uint16_t));
    }
  }
}

// Rows as tall as a whole-screen-wide line fits in a strip buffer, on whole pixel blocks.
static int fullRowH() {
  const int N = pixelSize(), h = REG_W * STRIP_H / APP_W;
  return N ? max(h / N * N, N) : h;
}

// The screen drawn in whole-width rows, so a glitch band or flash runs edge to edge instead of stopping at the boxes. Each eye, the mouth and
// the corner panels are still drawn in their own box (in the other strip) and copied in, so nothing shows only while glitching.
void FaceRenderer::drawFullRows(const FaceParams& p, int y0, int y1) {
  const int H = fullRowH(), gapX = REGIONS[R_EYE_L].x + REG_W, gapW = REGIONS[R_EYE_R].x - gapX;
  const int panelY = APP_H - WID_H;
  for (int y = y0; y < y1; y += H) {
    const int h = min(H, y1 - y);
    uint16_t* wide = _strip[_cur].buffer();
    Canvas row;
    row.attach(wide, APP_W, h);
    if (glowOn())
      glowBlock(wide, 0, y, APP_W, h, APP_W);
    else
      row.fill(BG);
    _lcd->wait();  // the other strip may still be going out
    Canvas box;
    auto paste = [&](int x, int w, int top, int bottom) {
      const int a = max(x, 0), b = min(x + w, APP_W);
      for (int j = max(top - y, 0); j < min(bottom - y, h) && a < b; j++)
        memcpy(wide + j * APP_W + a, box.buffer() + j * w + (a - x), (size_t)(b - a) * sizeof(uint16_t));
    };
    for (int r = 0; r < R_COUNT; r++) {
      const Rect& rc = REGIONS[r];
      if (y >= rc.y + rc.h || y + h <= rc.y) continue;
      box.attach(_strip[_cur ^ 1].buffer(), REG_W, h);
      if (glowOn())
        glowBlock(box.buffer(), rc.x, y, REG_W, h, REG_W);
      else
        box.fill(BG);
      if (r == R_MOUTH)
        drawMouth(box, rc.x - _parMX, y - _parMY, p);
      else
        drawEye(box, rc.x - _parEX, y - _parEY, p, r == R_EYE_L);
      resolveHoles(box.buffer(), rc.x, y, REG_W, h, REG_W);
      paste(rc.x, REG_W, rc.y, rc.y + rc.h);
    }
    if (_wValid && y + h > panelY)  // the panels as renderWidgets left them
      for (int right = 0; right < 2; right++) {
        box.attach(_strip[_cur ^ 1].buffer(), WID_W, h);
        drawPanel(box, _lastW, right, y - panelY);
        paste(right ? APP_W - WID_W : 0, WID_W, panelY, APP_H);
      }
    if (_deco == DF_GLASSES && gapW > 0 && gapW <= REG_W) {
      const float gy = glassesY(eyeBox(p, true)) - y;
      row.fillCapsule(gapX - 2.f, gy, gapX + gapW + 2.f, gy, 1.5f, GLASSES);
    }
    postProcess(wide, 0, y, APP_W, h);
    _lcd->push(0, y, APP_W, h, wide);
    _cur ^= 1;
  }
}

void FaceRenderer::drawRegion(Region r, const FaceParams& p, int y0, int y1) {
  const Rect& rc = REGIONS[r];
  for (int y = y0; y < y1; y += STRIP_H) {
    int h = min(STRIP_H, y1 - y);
    Canvas& s = _strip[_cur];
    if (glowOn())
      glowBlock(s.buffer(), rc.x, y, REG_W, h, REG_W);
    else
      s.fill(BG);
    if (r == R_MOUTH)
      drawMouth(s, rc.x - _parMX, y - _parMY, p);
    else
      drawEye(s, rc.x - _parEX, y - _parEY, p, r == R_EYE_L);
    resolveHoles(s.buffer(), rc.x, y, REG_W, h, REG_W);
    postProcess(s.buffer(), rc.x, y, REG_W, h);
    _lcd->push(rc.x, y, REG_W, h, s.buffer());
    _cur ^= 1;
  }
}

static bool inEye(float x, float y, int ex, int ey, int iw, int ih, int rad) {
  const float cx = fminf(fmaxf(x, ex + rad), ex + iw - rad), cy = fminf(fmaxf(y, ey + rad), ey + ih - rad);
  return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= (float)rad * rad + 0.5f && x >= ex && x <= ex + iw &&
         y >= ey && y <= ey + ih;
}

static void fillSpark(Canvas& s, float cx, float cy, float r, uint16_t c) {
  const float w = r * 0.24f;
  const int x = (int)lroundf(cx), y = (int)lroundf(cy), ri = (int)lroundf(r),
            wi = (int)fmaxf(1.f, lroundf(w));
  s.fillTriangle(x, y - ri, x - wi, y, x + wi, y, c);
  s.fillTriangle(x, y + ri, x - wi, y, x + wi, y, c);
  s.fillTriangle(x - ri, y, x, y - wi, x, y + wi, c);
  s.fillTriangle(x + ri, y, x, y - wi, x, y + wi, c);
}

void FaceRenderer::drawEyeBody(Canvas& s, int ox, int oy, int ex, int ey, int iw, int ih, int rad,
                               uint16_t fg, uint8_t style) {
  switch (style) {
    case 3: {
      const int P = 8;
      for (int gy = ey + P / 2; gy < ey + ih; gy += P)
        for (int gx = ex + P / 2; gx < ex + iw; gx += P)
          if (inEye((float)gx, (float)gy, ex, ey, iw, ih, rad)) s.fillCircleAA(gx - ox, gy - oy, 3.2f, fg);
      break;
    }
    case 6: {
      s.fillRoundRectAA(ex + iw / 3 - ox, ey - oy, iw - 2 * (iw / 3), ih, iw / 6, fg);
      s.fillRoundRectAA(ex - ox, ey + ih / 3 - oy, iw, ih - 2 * (ih / 3), ih / 6, fg);
      break;
    }
    default: s.fillRoundRectAA(ex - ox, ey - oy, iw, ih, rad, fg);
  }
  if (style == 2) {
    for (int y = ey + 3; y < ey + ih; y += 6) s.fillRect(ex - ox, y - oy, iw, 2, BG);
  } else if (style == 4) {
    const float R = fminf(iw, ih) * 0.5f - 3.f, cx = ex + iw * 0.5f, cy = ey + ih * 0.5f,
                spin = MOUTH_PHASE * 1.6f;
    float px = cx, py = cy;
    for (int i = 1; i <= 36; i++) {
      const float t = i / 36.f, a = spin + t * 6.2832f * 2.4f, rr = R * t;
      const float x = cx + rr * cosf(a), y = cy + rr * sinf(a);
      s.fillCapsule(px - ox, py - oy, x - ox, y - oy, 1.5f, BG);
      px = x;
      py = y;
    }
  } else if (style == 5) {
    const float m = fminf(iw, ih), k1 = 0.8f + 0.2f * sinf(MOUTH_PHASE * 3.f),
                k2 = 0.8f + 0.2f * sinf(MOUTH_PHASE * 3.f + 2.f);
    const uint16_t white = Canvas::rgb(255, 255, 255);
    fillSpark(s, ex + iw * 0.32f - ox, ey + ih * 0.3f - oy, m * 0.3f * k1, white);
    fillSpark(s, ex + iw * 0.7f - ox, ey + ih * 0.72f - oy, m * 0.17f * k2, white);
  }
}

void FaceRenderer::drawEye(Canvas& s, int ox, int oy, const FaceParams& p, bool left) {
  const int cx = left ? EYE_CX_L : EYE_CX_R;
  const float dir = left ? -1.f : 1.f;
  const EyeBox e = eyeBox(p, left);
  const uint16_t fg = eyeColor(p, left);
  const Fx fx{_eyes, _deco, _emT, _text};

  if (!drawEyeFx(s, ox, oy, fx, p, e, left)) {
    int ex = e.x, ey = e.y, iw = e.w, ih = e.h;
    float h = ih;
    int rad = (int)(S.eye_round * fminf(ih, iw) / 2);
    const uint8_t es = eyeStyleOf(left), ps = pupilStyleOf(left);
    drawEyeBody(s, ox, oy, ex, ey, iw, ih, rad, fg, es);
    const int th = (int)fmaxf(3, S.line_r + 1);
    if ((es == 1 || ps == 6) && iw > 2 * th + 2 && ih > 2 * th + 2)
      s.fillRoundRectAA(ex + th - ox, ey + th - oy, iw - 2 * th, ih - 2 * th, rad > th ? rad - th : 0,
                        holeColour());
    const int pupilShape = ps == 6 ? 0 : ps;
    if (pupilShape >= 1 && ih > 14 && p.lid_bottom < 0.3f) {
      float pr = fminf(iw, ih) * S.pupil_size * PUPIL_MUL, room = fminf(iw, ih) / 2 - pr - 3;
      float px = ex + iw / 2.f + clampf(p.pupil_x, -1, 1) * room,
            py = ey + ih / 2.f + clampf(p.pupil_y, -1, 1) * room;
      const uint16_t pc = S.pupil_ink == 1 ? Canvas::rgb(255, 255, 255) : S.pupil_ink == 2 ? fg : BG;
      switch (pupilShape) {
        case 2:
          s.fillEllipse((int)lroundf(px) - ox, (int)lroundf(py) - oy, (int)fmaxf(2.f, pr * 0.36f),
                        (int)lroundf(fminf(pr * 1.45f, ih / 2.f - 3)), pc);
          break;
        case 3: s.fillHeart(px - ox, py - oy + pr * 0.1f, pr * 2.2f, pc); break;
        case 4: s.fillStar(px - ox, py - oy, pr * 1.3f, 0, pc); break;
        case 5:
          s.fillRect((int)lroundf(px - pr * 0.85f) - ox, (int)lroundf(py - pr * 0.85f) - oy,
                     (int)lroundf(pr * 1.7f), (int)lroundf(pr * 1.7f), pc);
          break;
        case 7: {
          const int cx = (int)lroundf(px) - ox, cy = (int)lroundf(py) - oy, L = (int)lroundf(pr * 1.5f),
                    t = (int)fmaxf(4.f, lroundf(pr * 0.8f));
          s.fillRect(cx - L, cy - (t >> 1), 2 * L, t, pc);
          s.fillRect(cx - (t >> 1), cy - L, t, 2 * L, pc);
          break;
        }
        case 8: {
          const int cx = (int)lroundf(px) - ox, cy = (int)lroundf(py) - oy, R = (int)lroundf(pr * 1.4f);
          s.fillTriangle(cx, cy - R, cx - R, cy, cx + R, cy, pc);
          s.fillTriangle(cx, cy + R, cx - R, cy, cx + R, cy, pc);
          break;
        }
        case 9:
          s.fillCircleAA(px - ox, py - oy, pr, pc);
          s.fillCircleAA(px + pr * 0.55f - ox, py - pr * 0.15f - oy, pr * 0.85f, fg);
          break;
        default: s.fillCircleAA(px - ox, py - oy, pr, pc);
      }
      if (S.glint)
        s.fillCircleAA(px - pr * 0.35f - ox, py - pr * 0.35f - oy, pr * 0.3f, Canvas::rgb(255, 255, 255));
    }

    float a = clampf(p.brow_angle, -1, 1);
    float squint = fmaxf(0.f, -clampf(p.asym, -1, 1) * (left ? 1 : -1)) * h * 0.4f;
    squint += lidOf(left) * h * 0.8f;
    float dIn = fmaxf(a, 0) * h * 0.45f + squint, dOut = fmaxf(-a, 0) * h * 0.35f + squint;
    const float cutIn = dIn, cutOut = dOut;
    if (dIn > 0.5f || dOut > 0.5f) {
      const int gi = 2;
      int xOut = (left ? ex - gi : ex + iw + gi) - ox, xIn = (left ? ex + iw + gi : ex - gi) - ox;
      int top = ey - gi - oy;
      const uint16_t lidBg = holeColour();
      s.fillTriangle(xOut, top, xIn, top, xIn, top + 2 + dIn, lidBg);
      s.fillTriangle(xOut, top, xIn, top + 2 + dIn, xOut, top + 2 + dOut, lidBg);
    }

    float lb = clampf(p.lid_bottom, 0, 1);
    if (lb > 0.02f && h > 10) {
      float ry = h * lb, rx = iw * 0.75f;
      s.fillEllipse(ex + (iw >> 1) - ox, lroundf(ey + h + ry * 0.35f) - oy, lroundf(rx), lroundf(ry),
                    holeColour());
    }

    if (S.lashes && h > 12) {
      const float outer = left ? ex : ex + iw, topOuter = ey + cutOut * 0.9f;
      for (int k = 0; k < 3; k++) {
        float bx = outer - dir * (2 + k * 5),
              by = topOuter + 2 + (k == 0 ? 2 : 0) + k * (cutIn - cutOut) * 0.08f;
        s.fillCapsule(bx - ox, by - oy, bx + dir * (5 - k) - ox, by - 6 - (2 - k) - oy, 1.3f, fg);
      }
    }
  }

  drawGlitchFx(s, ox, oy, fx, e, left);

  if (S.blush && p.blush > 0.02f)
    s.fillEllipse(cx + dir * 14 - ox, EYE_CY + BLUSH_DY - oy, 22, BLUSH_RY, blushColor(p.blush));

  if (browsOf(left) && _eyes != EF_HEART && _eyes != EF_STAR) {
    Brow b = brow(p, e, left);
    s.fillCapsule(b.xo - ox, b.yo - oy, b.xi - ox, b.yi - oy, b.r, fg);
  }

  drawDecoFx(s, ox, oy, fx, p, e, left);
}

void FaceRenderer::drawMouth(Canvas& s, int ox, int oy, const FaceParams& p) {
  if (S.mouth_style == 2 && _deco != DF_TONGUE) return;
  const MouthShape m = mouthShape(p);
  if (_deco == DF_TONGUE) drawTongueFx(s, ox, oy, MOUTH_X + m.hw * 0.35f, m.up(0.35f));
  const float lr = S.line_r;
  const uint16_t line = faceColor(p);
  const bool merged = m.open > 0.03f && m.kind != 1 && m.open * MOUTH_DROP < 2 * lr + 2;
  if (m.kind == 7) {
    const float rx = lr * 1.6f + m.open * m.hw * 0.5f, ry = lr * 1.6f + m.open * MOUTH_DROP * 0.5f;
    const int cx = MOUTH_X - ox, cy = (int)lroundf(m.up(0) + m.open * MOUTH_DROP * 0.5f) - oy;
    if (m.open < 0.1f)
      s.fillCircleAA(cx, cy, rx, line);
    else
      s.fillEllipse(cx, cy, (int)lroundf(rx), (int)lroundf(ry), line);
    return;
  }
  if (m.kind == 5) {
    const uint16_t dim = S.mouth_solid ? line : faceColor(p, 0.5f);
    const int d = matrixDot(), g = d + 2;
    const float gf = (float)g;
    for (float x = floorf((MOUTH_X - m.hw) / gf) * gf; x < MOUTH_X + m.hw; x += gf) {
      const float u = clampf((x + gf * 0.5f - MOUTH_X) / m.hw, -1, 1);
      const int yt = (int)lroundf(m.up(u) / gf) * g,
                yb = (int)lroundf((m.up(u) + m.open * MOUTH_DROP * sqrtf(fmaxf(0.f, 1 - u * u))) / gf) * g;
      for (int y = yt; y <= yb; y += g)
        s.fillRect((int)x + 1 - ox, y - (d >> 1) - oy, d, d, y == yt || y == yb ? line : dim);
    }
    return;
  }
  if (m.kind == 6) {
    const int t = (int)lroundf(lr * 2 + 1), x0 = (int)lroundf(MOUTH_X - m.hw), w = (int)lroundf(m.hw * 2);
    const int open = m.open > 0.03f ? (int)lroundf(m.open * MOUTH_DROP) : 0,
              y0 = (int)lroundf(m.up(0)) - (t >> 1);
    s.fillRect(x0 - ox, y0 - oy, w, t + (open ? t + open : 0), line);
    if (open > 1 && w > 2 * t)
      s.fillRect(x0 + t - ox, y0 + t - oy, w - 2 * t, open, (S.mouth_solid ? line : faceColor(p, 0.37f)));
    return;
  }
  if (m.kind == 1 && m.open > 0.08f) {
    const int rx = (int)lroundf(m.hw * 0.2f * (0.7f + m.open * 0.8f) + lr),
              ry = (int)lroundf(m.open * MOUTH_DROP * 0.5f + lr);
    const int cx = MOUTH_X - ox, cy = (int)lroundf(m.up(0)) + ry - oy;
    s.fillEllipse(cx, cy, rx, ry, line);
    if (rx > lr + 1 && ry > lr + 1)
      s.fillEllipse(cx, cy, rx - (int)lr, ry - (int)lr, (S.mouth_solid ? line : faceColor(p, 0.37f)));
  }
  if (m.open > 0.03f && m.kind != 1 && !merged) {
    const uint16_t inner = (S.mouth_solid ? line : faceColor(p, 0.37f));
    const uint16_t white = Canvas::rgb(245, 245, 250);
    for (int x = lroundf(MOUTH_X - m.hw); x <= lroundf(MOUTH_X + m.hw); x++) {
      float u = (x - MOUTH_X) / m.hw;
      float yt = m.up(u), yb = m.lo(u);
      if (yb - yt >= 1) {
        s.vline(x - ox, lroundf(yt) - oy, lroundf(yb - yt) + 1, inner);
        if (m.kind == 9) s.vline(x - ox, lroundf(yt) - oy, (int)fmaxf(3.f, (yb - yt) * 0.45f), white);
      }
    }
  }

  if (S.mouth_style == 9 && m.open > 0.25f) {
    const float ry = 3 + 7 * m.open;
    s.fillEllipse(MOUTH_X - ox, (int)lroundf(m.lo(0) - ry * 0.6f) - oy, (int)lroundf(m.hw * 0.5f),
                  (int)lroundf(ry), mix565(Canvas::rgb(255, 110, 140), faceColor(p), 0.15f));
  }

  auto curve = [&](bool lower) {
    auto y = [&](float x) {
      float u = clampf((x - MOUTH_X) / m.hw, -1, 1);
      return merged ? (m.up(u) + m.lo(u)) * 0.5f : lower ? m.lo(u) : m.up(u);
    };
    for (int x = (int)ceilf(MOUTH_X - m.hw); x <= (int)floorf(MOUTH_X + m.hw); x++) {
      float yc = y(x);
      float slope = fminf(fabsf(y(x + 0.5f) - y(x - 0.5f)), 3.f);
      const float e = fminf(x - (MOUTH_X - m.hw), (MOUTH_X + m.hw) - x);
      const float grow = merged ? 0.5f * m.open * MOUTH_DROP *
                                      sqrtf(fmaxf(0.f, 1 - ((x - MOUTH_X) / m.hw) * ((x - MOUTH_X) / m.hw)))
                                : 0.f;
      float t = fminf(lr * sqrtf(1 + slope * slope), lr + slope * fmaxf(e, 0.f)) + grow;
      float top = yc - t, bot = yc + t;
      if (bot < oy - 1 || top >= oy + STRIP_H + 1) continue;
      int it = (int)ceilf(top), ib = (int)floorf(bot);
      s.vline(x - ox, it - oy, ib - it + 1, line);
      s.blendPixel(x - ox, it - 1 - oy, (uint8_t)((it - top) * 255), line);
      s.blendPixel(x - ox, ib + 1 - oy, (uint8_t)((bot - ib) * 255), line);
    }
  };
  curve(false);
  if (m.open > 0.03f && m.kind != 1 && !merged) curve(true);
  if (m.kind == 8 && (m.A > 0.15f * MOUTH_A || m.open > 0.2f)) {
    const uint16_t white = Canvas::rgb(245, 245, 250);
    for (float u : {-0.55f, 0.55f}) {
      const int x = lroundf(MOUTH_X + u * m.hw) - ox, y = lroundf(m.up(u) + lr) - oy;
      s.fillTriangle(x - 3, y, x + 3, y, x, y + 7, white);
    }
  }
  for (float u : {-1.f, 1.f})
    s.fillCircleAA(lroundf(MOUTH_X + u * m.hw) - ox, lroundf(m.up(u)) - oy, lroundf(lr), line);
}

//==============================
// the head: a box that turns on the X and Y axes (the classic textured-cube effect)
//==============================
// The box is 320 x 240 x 240 px, seen from 520 px away. Its front face shows the live face; the other sides show the same face texture
// stretched around the box. The texture is the whole face drawn at half size (160 x 120) once per frame; every screen pixel is then ray-cast
// onto the box and takes its colour from the texture (nearest texel). Sides are darker than the front.
static constexpr float CUBE_D = 520.f, CUBE_HX = 160.f, CUBE_HY = 120.f, CUBE_HZ = 120.f;

void FaceRenderer::renderFaceTexture(const FaceParams& p) {
  if (!_tex) _tex = (uint16_t*)malloc(160 * 120 * sizeof(uint16_t));
  if (!_texBg) _texBg = (uint8_t*)malloc((160 * 120) >> 3);
  if (!_tex || !_texBg) return;
  if (glowOn()) buildGlow(faceColor(p));
  // The glow at every texel (glowAt(2 tx, 2 ty)), with each column's term worked out once: the same arithmetic as glowStep,
  // so the same texels, and no divisions per texel.
  uint32_t colT[160];
  for (int tx = 0; tx < 160; tx++) colT[tx] = glowCol(tx * 2);
  // (the texture holds pixels as the strips do: byte-swapped for the panel)
  if (glowOn()) {
    uint16_t glowSw[128];
    for (int i = 0; i < 128; i++) glowSw[i] = (uint16_t)(_glowTab[i] >> 8 | _glowTab[i] << 8);
    for (int ty = 0; ty < 120; ty++) {
      const uint32_t rowT = glowRow(ty * 2);
      uint16_t* t = _tex + ty * 160;
      for (int tx = 0; tx < 160; tx++) t[tx] = glowSw[glowRing(colT[tx] + rowT)];
    }
  } else {
    const uint16_t bg = (uint16_t)(BG >> 8 | BG << 8);
    for (int i = 0; i < 160 * 120; i++) _tex[i] = bg;
  }
  memset(_texBg, 0xFF,
         (160 * 120) >> 3);  // all background, until a region's drawing changes a texel
  for (int r = 0; r < R_COUNT; r++) {
    const Rect& rc = REGIONS[r];
    for (int y = rc.y; y < rc.y + rc.h; y += STRIP_H) {
      const int h = min(STRIP_H, rc.y + rc.h - y);
      Canvas& s = _strip[0];
      uint16_t* bg =
          _strip[1]
              .buffer();  // the strip's background as drawn, to tell what the face covered (the warped path pushes no DMA)
      if (glowOn())
        glowBlock(bg, rc.x, y, REG_W, h, REG_W);
      else
        _strip[1].fill(BG);
      memcpy(s.buffer(), bg, (size_t)REG_W * h * sizeof(uint16_t));
      if (r == R_MOUTH)
        drawMouth(s, rc.x - _parMX, y - _parMY, p);
      else
        drawEye(s, rc.x - _parEX, y - _parEY, p, r == R_EYE_L);
      resolveHoles(s.buffer(), rc.x, y, REG_W, h, REG_W);
      for (int ty = 0; ty < (h >> 1); ty++)
        for (int tx = 0; tx < (REG_W >> 1); tx++) {
          const int i = ((y >> 1) + ty) * 160 + (rc.x >> 1) + tx, k = (ty * 2) * REG_W + tx * 2;
          _tex[i] = s.buffer()[k];
          // still the background (also a lid's cut-out, which shows the glow): the turned head shows the screen's glow there, so the glow stays
          // behind the head instead of turning with it like a lit panel. Edge pixels blended with the glow are face: outlines stay smooth.
          if (s.buffer()[k] != bg[k]) _texBg[i >> 3] &= (uint8_t)~(1 << (i & 7));
        }
    }
  }
  if (_deco == DF_GLASSES) {
    const int ty = (int)glassesY(eyeBox(p, true)) / 2;
    const uint16_t c = (uint16_t)(GLASSES >> 8 | GLASSES << 8);
    for (int tx = ((REGIONS[R_EYE_L].x + REG_W) >> 1) - 1; tx <= (REGIONS[R_EYE_R].x >> 1); tx++)
      if (ty >= 0 && ty < 120) {
        const int i = ty * 160 + tx;
        _tex[i] = c;
        _texBg[i >> 3] &= (uint8_t)~(1 << (i & 7));
      }
  }
}

#ifndef TEEVEE_NO_WARP
#ifndef WARP_SPAN
#define WARP_SPAN 8
#endif
//==============================
// the turned and lensed face: the box head turning on the X and Y axes, then the fisheye lens, then pixelate and glitch
//==============================
// The face is the front of the 320 x 240 x 240 box (CUBE_*), seen from CUBE_D px in front of the screen; the box turns about its own centre,
// half its depth behind the face, so the face swings towards where it looks and comes nearer on one side, like the cube did. Only the front is
// drawn: the sides are background. Every screen pixel is taken through the lens, then ray-cast onto the front's plane, and the face's texture
// (renderFaceTexture, half size) gives its colour. Glitch and pixelate run last, per strip.

// Where a screen point lands on the face, through the lens and the turn: the same maths draws the picture (renderWarped) and maps a touch to
// the face it hits (screenToFace), so a poke on an eye of the turned head is a poke on that eye.
namespace {
struct HeadView {
    float R00, R01, R10, R11, R20, R21, nx, ny, nz, num, k, rollC, rollS;
    bool turned;
    HeadView(float turnXDeg, float turnYDeg, float fisheye, float rollDeg = 0) {
      rollC = cosf(rollDeg * 0.01745329f), rollS = sinf(rollDeg * 0.01745329f);
      const float ax = turnXDeg * 0.01745329f, ay = turnYDeg * 0.01745329f;
      const float cx = cosf(ax), sx = sinf(ax), cy = cosf(ay), sy = sinf(ay);
      R00 = cy, R01 = sy * sx, R10 = 0, R11 = cx, R20 = -sy, R21 = cy * sx;
      nx = sy * cx, ny = -sx, nz = cy * cx;
      // The eye is at (0, 0, -CUBE_D), the screen is z = 0, the box centre C = (0, 0, CUBE_HZ). The front lies in the plane through C - CUBE_HZ * n with
      // normal n; a ray eye + t * (qx, qy, CUBE_D) meets it at t = num / (n . (qx, qy, CUBE_D)).
      num = -CUBE_HZ * nx * nx - CUBE_HZ * ny * ny + nz * (CUBE_HZ + CUBE_D - CUBE_HZ * nz);
      turned = turnXDeg != 0.f || turnYDeg != 0.f || rollDeg != 0.f;
      k = fisheye * 2.2f /
          40000.f;  // the lens: pulls the picture in towards the edges (barrel); distances in px
    }
    // Screen point (px, py) -> face point (fu, fv) in the flat face's 320 x 240 pixels, on or off the face. False: the ray misses the plane.
    bool project(float px, float py, float& fu, float& fv) const {
      float qx = px - 160.f, qy = py - 120.f;
      if (k > 0.f) {
        const float f = 1.f + k * (qx * qx + qy * qy);
        qx *= f;
        qy *= f;
      }
      if (!turned) {
        fu = qx + 160.f, fv = qy + 120.f;
        return true;
      }
      if (rollS != 0.f) {
        const float rx = rollC * qx + rollS * qy;
        qy = rollC * qy - rollS * qx;
        qx = rx;
      }
      const float den =
          nx * qx + ny * qy +
          nz * CUBE_D;  // the ray from the eye through this point meets the front of the turned box
      if (den <= 1e-3f) return false;
      const float t = num / den;
      const float X = t * qx, Y = t * qy, Z = CUBE_D * (t - 1.f) - CUBE_HZ;
      fu = R00 * X + R10 * Y + R20 * Z + 160.f, fv = R01 * X + R11 * Y + R21 * Z + 120.f;
      return true;
    }
    bool toFace(float px, float py, float& fu, float& fv) const {
      if (!project(px, py, fu, fv)) return false;
      return turned ? fu >= 160.f - CUBE_HX && fu <= 160.f + CUBE_HX && fv >= 120.f - CUBE_HY &&
                          fv <= 120.f + CUBE_HY
                    : fu >= 0 && fu < 320 && fv >= 0 && fv < 240;
    }
};
}  // namespace

bool FaceRenderer::screenToFace(float sx, float sy, float& fx, float& fy) const {
  fx = sx, fy = sy;
  if (_turnXShown == 0.f && _turnYShown == 0.f && _rollShown == 0.f && _fisheye <= 0.f) return true;
  float u, v;
  if (!HeadView(_turnXShown, _turnYShown, _fisheye, _rollShown).toFace(sx + 0.5f, sy + 0.5f, u, v))
    return false;
  fx = fminf(fmaxf(u, 0.f), 319.f), fy = fminf(fmaxf(v, 0.f), 239.f);
  return true;
}

int FaceRenderer::renderWarped(const FaceParams& p) {
  uint32_t t0 = tvMicros();
  renderFaceTexture(p);
  warpUs[0] += tvMicros() - t0;
  warpFrames++;
  if (!_tex) return 0;
  _wasCube = true;
  _cubeNow = true;
  _valid = false;
  const HeadView view(_turnXShown, _turnYShown, _fisheye, _rollShown);
  const bool glow = glowOn();
  const uint16_t bgSwapped = (uint16_t)(BG >> 8 | BG << 8);
  const uint16_t* const tex = _tex;
  const uint8_t* const texBg = _texBg;
  // The background at screen pixel (sx, sy): the glow behind the head (glowAt, byte-swapped). Its column terms are worked out once a
  // frame, its row term once a row, and the table is swapped once: the same arithmetic as glowStep on the same values, so the same pixels.
  static uint32_t glowColT[APP_W];
  uint16_t glowSw[128];
  uint32_t glowRowT = 0;
  int glowY = -1;
  if (glow) {
    for (int x = 0; x < APP_W; x++) glowColT[x] = glowCol(x);
    for (int i = 0; i < 128; i++) glowSw[i] = (uint16_t)(_glowTab[i] >> 8 | _glowTab[i] << 8);
  }
  auto backdrop = [&](int sx, int sy) -> uint16_t {
    if (!glow) return bgSwapped;
    if (sy != glowY) glowY = sy, glowRowT = glowRow(sy);
    return glowSw[glowRing(glowColT[min(max(sx, 0), APP_W - 1)] + glowRowT)];
  };
  // The colour (byte-swapped, as the strips go to the panel) at face point (fu, fv), fixed point 16.16, seen at screen pixel (sx, sy)
  auto texel = [&](int32_t fu, int32_t fv, int sx, int sy) -> uint16_t {
    if (fu >= 0 && fu <= (320 << 16) && fv >= 0 && fv <= (240 << 16)) {
      const int tu = fu >> 16 > 319 ? 319 : fu >> 16, tv = fv >> 16 > 239 ? 239 : fv >> 16,
                i = (tv >> 1) * 160 + (tu >> 1);
      if (!(texBg[i >> 3] >> (i & 7) & 1)) return tex[i];
    }
    return backdrop(sx, sy);
  };
  // The span ends along one row of row(sx, sy, n, ...): a span's end is the next one's start, so a row costs n / WARP_SPAN + 1
  // projections instead of two per span (a projection is a float divide: about 1 us on the board). Built again only when
  // the row changes.
  constexpr int ENDS = (APP_W + 2 * 8) / WARP_SPAN + 2;
  struct {
      int sx = 0, sy = -1, n = -1;
      float u[ENDS], v[ENDS];
      bool ok[ENDS];         // the end lands on the face's plane, not far off
      int32_t fu[ENDS], fv[ENDS], du[ENDS], dv[ENDS];  // per span, 16.16: its start and the step per pixel
  } ends;
  auto rowEnds = [&](int sx, int sy, int n) {
    if (ends.sx == sx && ends.sy == sy && ends.n == n) return;
    ends.sx = sx, ends.sy = sy, ends.n = n;
    const int K = (n + WARP_SPAN - 1) / WARP_SPAN;
    for (int k = 0; k <= K; k++) {
      float& u = ends.u[k];
      float& v = ends.v[k];
      ends.ok[k] = view.project(sx + min(k * WARP_SPAN, n) + 0.5f, sy + 0.5f, u, v) && fabsf(u) < 8000 && fabsf(v) < 8000;
    }
    for (int k = 0; k < K; k++) {
      ends.ok[k] = ends.ok[k] && ends.ok[k + 1];  // from here on: the span is interpolated
      if (!ends.ok[k]) continue;
      const int m = min(WARP_SPAN, n - k * WARP_SPAN);
      ends.fu[k] = (int32_t)(ends.u[k] * 65536.f), ends.fv[k] = (int32_t)(ends.v[k] * 65536.f);
      // A full span divides by the constant (the ESP32-S3 has no integer divide, so a variable one is a slow library call).
      const int32_t du = (int32_t)((ends.u[k + 1] - ends.u[k]) * 65536.f), dv = (int32_t)((ends.v[k + 1] - ends.v[k]) * 65536.f);
      ends.du[k] = m == WARP_SPAN ? du / WARP_SPAN : du / m;
      ends.dv[k] = m == WARP_SPAN ? dv / WARP_SPAN : dv / m;
    }
  };
  // Pixel p of the row: within a span the face point is interpolated from its ends; where an end misses the face's
  // plane (or lands far off), each pixel is projected on its own.
  auto at = [&](int sx, int sy, int n, int p) -> uint16_t {
    rowEnds(sx, sy, n);
    const int s = p / WARP_SPAN, j = p - s * WARP_SPAN;
    if (ends.ok[s]) return texel(ends.fu[s] + j * ends.du[s], ends.fv[s] + j * ends.dv[s], sx + p, sy);
    float u, v;
    const bool hit = view.project(sx + p + 0.5f, sy + 0.5f, u, v) && fabsf(u) < 8000 && fabsf(v) < 8000;
    return texel(hit ? (int32_t)(u * 65536.f) : -1, hit ? (int32_t)(v * 65536.f) : -1, sx + p, sy);
  };
  auto row = [&](int sx, int sy, int n, uint16_t* out) {
    rowEnds(sx, sy, n);
    for (int s = 0, i = 0; i < n; s++, i += WARP_SPAN) {
      const int m = min(WARP_SPAN, n - i);
      if (!ends.ok[s]) {
        for (int j = 0; j < m; j++) out[i + j] = at(sx, sy, n, i + j);
        continue;
      }
      int32_t fu = ends.fu[s], fv = ends.fv[s];
      const int32_t du = ends.du[s], dv = ends.dv[s], fuN = fu + (m - 1) * du, fvN = fv + (m - 1) * dv;
      // Both ends of the span on the face (the steps are linear, so every pixel between is too): no range checks or clamps per
      // pixel, and the texel is (fv >> 17, fu >> 17), which is what texel() works out for a point on the face.
      if (fu >= 0 && fu < (320 << 16) && fuN >= 0 && fuN < (320 << 16) && fv >= 0 && fv < (240 << 16) && fvN >= 0 &&
          fvN < (240 << 16)) {
        for (int j = 0; j < m; j++, fu += du, fv += dv) {
          const int t = (fv >> 17) * 160 + (fu >> 17);
          out[i + j] = texBg[t >> 3] >> (t & 7) & 1 ? backdrop(sx + i + j, sy) : tex[t];
        }
        continue;
      }
      for (int j = 0; j < m; j++, fu += du, fv += dv) out[i + j] = texel(fu, fv, sx + i + j, sy);
    }
  };
  // RGB split here, from the neighbouring face pixels: postProcess would see only the strip, not what lies past the screen edge
  const int d = min((int)lroundf(S.rgb_split), 8);
  auto sw = [](uint16_t c) { return (uint16_t)(c >> 8 | c << 8); };
  // Pixelated, postProcess keeps only the pixel at each block's centre: only that one is worked out (1 in N x N), and the
  // block filled with it, so the picture is the same as from full rows.
  const int N = pixelSize();
  static uint16_t wide[APP_W + 2 * 8];
  const int H = fullRowH(), w = APP_W;
  int pushes = 0;
  g_lcdFullFrame = true;
  t0 = tvMicros();
  for (int y0 = 0; y0 < APP_H; y0 += H) {
    const int h = min(H, APP_H - y0);
    uint16_t* out = _strip[_cur].buffer();
    for (int j = 0; N && j < h;) {  // the same blocks as postProcess (strips start on a multiple of N)
      const int rows = min(N - (y0 + j) % N, h - j), sy = y0 + j + (rows >> 1);
      // With the RGB split every block reads three pixels of its row. In small blocks that is most of the row, and the whole row
      // in one run (row) costs less than the pixels one at a time (at); both give the same pixel values.
      const bool whole = d && N <= 5;
      if (whole) row(-d, sy, w + 2 * d, wide);
      for (int i = 0; i < w;) {
        const int cols = min(N - i % N, w - i), x = i + (cols >> 1);
        const uint16_t c =
            !d      ? at(0, sy, w, x)
            : whole ? sw((uint16_t)((sw(wide[x + 2 * d]) & 0xF800) | (sw(wide[x + d]) & 0x07E0) | (sw(wide[x]) & 0x001F)))
                    : sw((uint16_t)((sw(at(-d, sy, w + 2 * d, x + 2 * d)) & 0xF800) |
                                    (sw(at(-d, sy, w + 2 * d, x + d)) & 0x07E0) | (sw(at(-d, sy, w + 2 * d, x)) & 0x001F)));
        for (int k = i; k < i + cols; k++) out[j * w + k] = c;
        i += cols;
      }
      for (int y = j + 1; y < j + rows; y++) memcpy(out + y * w, out + j * w, (size_t)w * sizeof(uint16_t));  // the block's other rows
      j += rows;
    }
    for (int j = 0; !N && j < h; j++) {
      if (!d) {
        row(0, y0 + j, w, out + j * w);
        continue;
      }
      row(-d, y0 + j, w + 2 * d, wide);
      for (int i = 0; i < w; i++)
        out[j * w + i] = sw(
            (uint16_t)((sw(wide[i + 2 * d]) & 0xF800) | (sw(wide[i + d]) & 0x07E0) | (sw(wide[i]) & 0x001F)));
    }
    const uint32_t tp = tvMicros();
    postProcess(out, 0, y0, w, h, false, N > 0);
    const uint32_t t1 = tvMicros();
    warpUs[3] += t1 - tp;
    warpUs[1] += t1 - t0;
    _lcd->push(0, y0, w, h, out);
    t0 = tvMicros();
    warpUs[2] += t0 - t1;
    _cur ^= 1;
    pushes++;
  }
  g_lcdFullFrame = false;
  _lcd->wait();
  warpUs[2] += tvMicros() - t0;
  return pushes;
}

#endif
