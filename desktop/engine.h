#pragma once
#include <stdint.h>
#include "appraisal.h"
#include "daytint.h"
#include "diary.h"
#include "emotes.h"
#include "emotion.h"
#include "idle.h"
#include "mapper.h"
#include "mind.h"
#include "mind_senses.h"
#include "mind_actions.h"
#include "look_net.h"
#include "needs.h"
#include "temper.h"
#include "wardrobe.h"
#include "spring.h"
#include "stimuli.h"
#include "touchzone.h"
#include "style.h"
#include "taste.h"

class Engine {
  public:
    void begin(uint32_t seed, uint32_t nowMs, bool wake = true);
    void update(uint32_t nowMs);

    void event(uint8_t type, float intensity = 1.f);
    void pointer(bool down, int x, int y);
    void gaze(float gx, float gy);
    void notePresence() { _presentAt = _now ? _now : 1; }
    void setBatteryLow(float v) { _batteryLow = v < 0 ? 0 : v > 1 ? 1 : v; }
    void setWatched(float v) { _watched = v < 0 ? 0 : v > 1 ? 1 : v; }
    void setScreenSense(bool on) {
      _screenOn = on;
      _dirty = true;
      if (!on) setScreen(-1, 0);
    }
    bool screenSense() const { return _screenOn; }
    void setScreen(float brightness, float activity) {
      _screenLight = brightness, _screenAct = activity < 0 ? 0 : activity > 1 ? 1 : activity;
    }
    // How the robot's body moves (a phone's motion sensor, the desktop pet carried or thrown). motion 0..1 fades by itself; tilt -1..1.
    void setBody(float motion, float tiltX, float tiltY, bool carried) {
      if (motion > _motion) _motion = motion > 1 ? 1 : motion;
      _tiltX = tiltX, _tiltY = tiltY, _carried = carried;
    }
    void showFace(int anchor);  // hold an expression until autoFace()
    void autoFace();
    void playEmote(int id);
    void toggleSleep();
    void setStyle(int preset);
    const char* seasonLook();
    void randomLook(bool all = false);
    void newRobot(uint32_t seed);
    void setHeadTurn(float maxDeg) {
      _headTurn = maxDeg;
      _dirty = true;
    }
    float headTurnDeg() const { return _headTurn; }
    void setFisheye(float k) {
      _fisheyeMax = k;
      _dirty = true;
    }
    float fisheyeMax() const { return _fisheyeMax; }
    float fisheye() const { return _fisheyeNow; }
    float turnX() const { return _turnX; }
    float turnY() const { return _turnY; }
    float turnRoll() const { return _turnRoll; }
    void setLookChange(float chance, float whole) {
      _lookChance = chance < 0 ? 0 : chance > 1 ? 1 : chance == chance ? chance : 0.5f;
      _lookWhole = whole < 0 ? 0 : whole > 1 ? 1 : whole == whole ? whole : 0.3f;
      _dirty = true;
    }
    float lookChance() const { return _lookChance; }
    float lookWhole() const { return _lookWhole; }
    void setWardrobe(bool on) { _wardrobeOn = on; }
    void setLookPinned(bool on) {
      _lookPinned = on;
      _dirty = true;
    }
    bool lookPinned() const { return _lookPinned; }
    bool wardrobe() const { return _wardrobeOn; }
    const Temper& temper() const { return _temper; }
    bool styleText(const char* options);
    void setPersonality(int index);

    const FaceParams& face() const { return _spring.value(); }
    const Overlay& overlay() const { return _ov; }
    const FaceStyle& style() const { return _style; }
    int styleIndex() const { return _styleIdx; }
    int personality() const { return _personality; }
    int heldFace() const { return _manual ? _manualAnchor : -1; }
    int emote() const { return emoteActive() ? _emote : 0; }
    const char* mind();
    PAD emotion() const { return _emo.emotion(); }
    const MindNet& net() const { return _mind; }
    const LookNet& lookNet() const { return _look; }
    const char* lookName() const { return _lookChoice ? STYLE_PRESETS[_lookChoice - 1].name : "home"; }
    float lookScore() const { return _lookScoreNow; }
    const char* lookTopName(int i) const {
      return _lookTop[i] ? STYLE_PRESETS[_lookTop[i] - 1].name : "home";
    }
    float lookTopScore(int i) const { return _lookTopScore[i]; }
    const Needs& needs() const { return _needs; }
    const Tastes& tastes() const { return _tastes; }
    // A behaviour file's text (behaviour.h) comes into force now. False: not such a file; err says why, or names the first skipped line. New robots get its instincts.
    bool setBehaviour(const char* text, size_t len, char* err, size_t errN);
    int idleBehaviour() const { return _idle.behaviour(_now); }
    const Diary& diary() const { return _diary; }
    DayTint dayTint() const;
    float pupilScale() const { return _pupil; }
    float glitchScale() const { return _glitchScale; }
    float glitchDrive() const { return _glitch; }

    bool save(const char* dir) const;
    void load(const char* dir);
    bool dirty() const { return _dirty; }
    void clearDirty() { _dirty = false; }

  private:
    void tick(uint32_t now);
    void apply(const EmotionEvent& ev, uint32_t now);
    void setBase(const FaceParams& p, uint32_t now);
    bool emoteActive() const { return _emote != 0 && (int32_t)(_now - _emoteEnd) < 0; }
    void mindStep(uint32_t now);
    void choosePokeSide(uint32_t now);
    void pokeOneEye(FaceParams& f, uint32_t now) const;
    int _pokeSide = -1;
    uint32_t _pokeSideAt = 0, _pointerAt = 0;

    IdleGen _idle;
    EmotionEngine _emo;
    TouchGestures _touch;
    MindNet _mind;
    Needs _needs;
    Temper _temper;
    Wardrobe _wardrobe, _mouthWardrobe;
    FaceStyle _home = styleInitial();
    bool _wardrobeOn = false, _wardrobeHeld = false;
    uint32_t _lastLook = 0, _wakeAt = 0;
    void changeLook(uint32_t now);
    FaceStyle candidateLook(int c) const;
    void lookInput(uint32_t now, int c, float* x);
    LookNet _look;
    float _lookIn[LookNet::IN] = {};
    bool _lookWorn = false;
    int _lookChoice = 0;
    bool _lookPinned = false;
    int _lookTop[3] = {};
    float _lookTopScore[3] = {};
    float _lookScoreNow = 0;
    Diary _diary;
    float _lastHour = -1;
    uint32_t _lastSample = 0;
    FaceSpring _spring;
    FaceStyle _style = styleInitial();
    Overlay _ov{};
    FaceParams _base{}, _target{};
    bool _started = false, _manual = false, _down = false, _dirty = false;
    int _manualAnchor = 0, _styleIdx = 0, _personality = 0, _px = 0, _py = 0;
    uint32_t _now = 0, _sim = 0, _lastSpring = 0;
    uint8_t _emote = 0, _shownEmote = 0;
    uint32_t _emoteStart = 0, _emoteEnd = 0, _emoteMs = 1, _lastAutoEmote = 0;
    float _petRate = 0, _pokeRate = 0;
    uint32_t _lastTouch = 0, _lastEvent = 0, _lastMind = 0;
    float _tx = 0, _ty = 0, _glitch = 0;
    uint32_t _presentAt = 0;
    float _batteryLow = 0, _motion = 0, _tiltX = 0, _tiltY = 0, _watched = 0, _screenLight = -1,
          _screenAct = 0;
    bool _screenOn = false;
    bool _carried = false;
    bool _emoteHeld = false, _sleepHeld = false;
    uint32_t _rng = 0x2545F491u;
    float _pupil = 1;
    float _turnRoll = 0, _tiltSide = 1;
    float _headTurn = 0, _turnX = 0, _turnY = 0, _fisheyeMax = 0, _fisheyeNow = 0;
    float _gazeX = 0, _gazeY = 0, _headUx = 0, _headUy = 0;
    uint32_t _lastZoneEmote = 0;
    static constexpr int QUEUE = 16;
    void applyBehaviour();
    Tastes _tastes;
    uint8_t _eyeChoice = 0;
    Appraisal _appraisal;
    bool _wasDown = false;
    bool _sleepBubble = false;
    uint32_t _bubbleGap = 2000;
    float _lookChance = 0.5f, _lookWhole = 0.3f, _glitchScale = 1;
    EmotionEvent _queue[QUEUE];
    int _queued = 0;
};
