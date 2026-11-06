#pragma once
#include "app_cmds.h"
#include "canvas.h"
#include "emotes.h"
#include "face_params.h"
#include "lcd.h"
#include "style.h"
#include "widgets.h"

// Procedural face renderer. Redraws only regions whose params changed, in small strip sprites pushed by DMA (ping-pong), so no full framebuffer is needed.
int pixelSize();

class FaceRenderer {
  public:
    void begin(Lcd* lcd);
    int render(const FaceParams& p, const Overlay& ov, uint32_t nowMs, bool force = false);
    int renderWidgets(const Widgets& w);
#ifdef TEEVEE_BOARD_AMOLED
    int renderMargins(const Widgets& w);
#endif
    // The whole panel was cleared (a fill): the bands beside the face are drawn again at the next renderWidgets.
    void forgetMargins() {
#ifdef TEEVEE_BOARD_AMOLED
      _marginValid = false;
#endif
    }
#ifndef TEEVEE_NO_WARP
    bool screenToFace(float sx, float sy, float& fx, float& fy) const;
#else
    bool screenToFace(float sx, float sy, float& fx, float& fy) const {
      fx = sx, fy = sy;
      return true;
    }
#endif
    void setVisibleColumns(int x0, int x1) {
      _visX0 = x0 < 0 ? 0 : x0 > 100 ? 100 : x0;
      _visX1 = x1 > 320 ? 320 : x1 < 220 ? 220 : x1;
      _wValid = false;
    }
    void setStyle(const FaceStyle& s);
    void setDayTint(float hue, float val) {
      _dayHue = hue;
      _dayVal = val;
    }
    void setPupilScale(float m) { _pupilAsk = m < 0.7f ? 0.7f : m > 1.4f ? 1.4f : m; }
    // How far the eyes and the mouth are moved (pixels), from the tilt: the parallax. The eyes move more than the mouth, so they look nearer.
    void setParallax(int eyeX, int eyeY, int mouthX, int mouthY) {
      _parEX = eyeX, _parEY = eyeY, _parMX = mouthX, _parMY = mouthY;
    }
#ifndef TEEVEE_NO_WARP
    void setTurn(float xDeg, float yDeg, float rollDeg = 0) {
      _turnXWant = xDeg < -35 ? -35 : xDeg > 35 ? 35 : xDeg;
      _turnYWant = yDeg < -35 ? -35 : yDeg > 35 ? 35 : yDeg;
      _rollWant = rollDeg < -20 ? -20 : rollDeg > 20 ? 20 : rollDeg;
    }
    void setFisheye(float k) { _fisheye = k < 0 ? 0 : k > 1 ? 1 : k; }
#else
    void setTurn(float, float, float = 0) {}
    void setFisheye(float) {}
#endif
    void setGlitchDrive(float d) { _drive = d < 0 ? 0 : d > 1 ? 1 : d; }
    void setGlitchScale(float s) { _glScale = s < 0 ? 0 : s > 2 ? 2 : s; }
    bool glitchOn() const { return _glitchOn; }
    int renderApp(const AppFrame& f);
    void setAppFace(const FaceParams& p) { _appFace = p; }
    void enterApp();
    void enterFace();
    uint32_t regionUs[3] = {};
    uint32_t warpFrames = 0;  // turned frames since the last report
    uint32_t warpUs[4] = {};  // the turned face: texture, rows, push, effects (summed, like regionUs)

  private:
    enum Region : uint8_t { R_EYE_L, R_EYE_R, R_MOUTH, R_COUNT };
    void drawRegion(Region r, const FaceParams& p, int y0, int y1);
    void drawFullRows(const FaceParams& p, int y0, int y1);
    void drawPanel(Canvas& s, const Widgets& w, bool right, int oy) const;
    int renderWarped(const FaceParams& p);
    void renderFaceTexture(const FaceParams& p);
    float _turnXWant = 0, _turnYWant = 0, _turnXShown = 0, _turnYShown = 0, _rollWant = 0, _rollShown = 0;
    bool _bridgeShown = false;
    bool _wasCube = false, _cubeNow = false;
    float _fisheye = 0;
    uint16_t* _tex =
        nullptr;  // the face at half size, the texture of the turned head (38 KB on the heap, allocated when the head first turns)
    uint8_t* _texBg = nullptr;
    void contentRows(Region r, const FaceParams& p, int& y0, int& y1) const;
    void drawEye(Canvas& s, int ox, int oy, const FaceParams& p, bool left);
    void drawEyeBody(Canvas& s, int ox, int oy, int ex, int ey, int iw, int ih, int rad, uint16_t fg,
                     uint8_t style);
    void drawMouth(Canvas& s, int ox, int oy, const FaceParams& p);
    static void drawMiniFace(Canvas& s, int ox, int oy, const AppCmd& c, void* self);
    bool regionChanged(Region r, const FaceParams& p) const;
    void updateGlitch(uint32_t now);
    uint32_t glRnd();
    void postProcess(uint16_t* buf, int ox, int oy, int w, int h, bool split = true, bool pixelated = false) const;
    void buildGlow(uint16_t fg);
    void glowBlock(uint16_t* buf, int x0, int y0, int w, int h, int stride) const;
    uint16_t glowAt(int x, int y) const;
    uint16_t holeColour() const;
    void resolveHoles(uint16_t* buf, int x0, int y0, int w, int h, int stride) const;
    void paintGlowBlock(int x, int y, int w, int h);
    uint16_t _glowTab[128] = {};
    uint16_t _glowFg = 0;
    int _parEX = 0, _parEY = 0, _parMX = 0, _parMY = 0;  // the shift now
    int _parShownEX = 0, _parShownEY = 0, _parShownMX = 0, _parShownMY = 0;  // the shift the last frame was drawn with

    Lcd* _lcd = nullptr;
    Canvas _strip[2];
    uint8_t _cur = 0;
    FaceParams _last{};
    bool _valid = false;
    Canvas _wsprite;
    Widgets _lastW{};
    bool _wValid = false;
#ifdef TEEVEE_BOARD_AMOLED
    void drawMarginStrip(Canvas& s, const Widgets& w, bool bottom, int oy) const;
    Canvas _msprite;
    Widgets _marginW{};
    bool _marginValid = false;
#endif
    int _visX0 = 0, _visX1 = 320;
    FaceStyle _style{};
    FaceParams _appFace{};
    uint32_t _appHash[APP_TILES] = {};
    bool _appValid = false;
    int16_t _drawn[R_COUNT][2] = {};
    uint8_t _eyes = EF_NONE, _deco = DF_NONE;
    char _text[8] = "";
    bool _lastActive = false, _lastTongue = false;
    bool _glitchOn = false, _lastGlitch = false, _glBig = false;
    float _dayHue = 0,
          _dayVal =
              1;  // asked for; the drawn one is DAY_HUE / DAY_VAL (quantised so a slow drift redraws rarely)
    int _dayKey = 0;
    bool _dayChanged = false, _pupilChanged = false;
    float _pupilAsk = 1;
    float _glScale = 1;
    float _drive = 0, _glSched = 0;
    float _lastPhase = 0;
    uint32_t _glNext = 0, _glEnd = 0, _glRoll = 0, _glRng = 0x9E3779B9u;
    int _glBands = 0;
    uint8_t _glFlash = 0;
    struct GlBand {
        int16_t y0, y1;
        int8_t dx, split;
        uint8_t kind, seed;
    } _glBand[6] = {};
    float _emT = 0;
};
