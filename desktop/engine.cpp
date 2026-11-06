#include "engine.h"
#include "behaviour.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <string>

namespace {
static void weekNow(int& day, int& hour) {
  time_t t = time(nullptr);
  struct tm tmv;
#ifdef _WIN32
  localtime_s(&tmv, &t);
#else
  localtime_r(&t, &tmv);
#endif
  day = t > 1577836800 ? tmv.tm_wday : -1, hour = tmv.tm_hour;
}

float localHour() {
  time_t t = time(nullptr);
  struct tm tmv;
#ifdef _WIN32
  localtime_s(&tmv, &t);
#else
  localtime_r(&t, &tmv);
#endif
  return tmv.tm_hour + tmv.tm_min / 60.f;
}

float reward(const EmotionEvent& e) {
  switch (e.type) {
    case EV_PET: return 0.8f * e.intensity;
    case EV_PRAISE:
    case EV_LOVE: return 1.0f * e.intensity;
    case EV_GREET: return 0.4f * e.intensity;
    case EV_POKE: return -0.3f * e.intensity;
    case EV_ANNOY:
    case EV_SCOLD: return -1.0f * e.intensity;
    default: return 0;
  }
}
}  // namespace

void Engine::begin(uint32_t seed, uint32_t nowMs, bool wake) {
  _now = _sim = _lastSpring = nowMs;
  _rng = seed ? seed : _rng;
  applyBehaviour();
  _emo.begin(BEHAVIOUR.personality[_personality]);
  _base = mapEmotion(_emo.emotion());
  _target = _base;
  _spring.reset(wake ? sleepingPose(_base) : _base);
  _idle.begin(seed, nowMs);
  _appraisal.begin(seed ^ 0xA5A5F00Du);
  _mind.init(seed ^ 0x9E3779B9u);
  _mind.setRates(0.05f, 0.15f);
  _diary.begin(1);
  if (wake) {
    _idle.fallAsleepNow(nowMs);
    _wakeAt = nowMs + 1600;
  }
  _started = true;
}

void Engine::changeLook(uint32_t now) {
  if (_lookPinned) return;
  uint32_t rr = now * 2246822519u;
  if (!rr) rr = 1;
  const bool whole = (styleRnd(rr) >> 8) / 16777216.f < _lookWhole;
  const uint32_t pick = styleRnd(rr) % 100;
  const int part = whole       ? -1
                   : pick < 40 ? (int)SG_MOUTH
                   : pick < 60 ? (int)SG_COLOUR
                   : pick < 80 ? (int)SG_EYES
                               : (int)SG_PUPILS;
  if (part == SG_MOUTH) {
    static const char* mnames[MOUTH_CHOICES];
    int n = 0;
    for (int c = 1; c <= MOUTH_CHOICES; c++) mnames[n++] = MOUTH_STYLES[mouthChoiceStyle(c)];
    const int c = _mouthWardrobe.pick(mnames, n, _emo.emotion().v, _emo.emotion().a, _idle.sleepiness(now),
                                      [&]() { return (styleRnd(rr) >> 8) / 16777216.f; });
    _style.mouth_style = c == 0 ? _home.mouth_style : mouthChoiceStyle(c);
    _mouthWardrobe.current = c;
    _lastLook = now;
    _dirty = true;
    return;
  }
  if (part == SG_EYES && (styleRnd(rr) & 1)) {
    const int c =
        _tastes.eye.pick(EYE_COUNT, _style.eye_style, [&]() { return (styleRnd(rr) >> 8) / 16777216.f; });
    if (c >= 0) {
      _style.eye_style = (uint8_t)c;
      _tastes.eye.chose(c, now);
      _lastLook = now;
      _dirty = true;
      return;
    }
  }
  uint32_t r = now * 2654435761u;
  if (!r) r = 1;
  static_assert(STYLE_PRESET_COUNT + 1 <= Wardrobe::MAX,
                "more presets than the wardrobe holds: raise Wardrobe::MAX");
  float vs[STYLE_PRESET_COUNT + 1], ps[STYLE_PRESET_COUNT + 1];
  float x[LookNet::IN];
  for (int k = 0; k <= STYLE_PRESET_COUNT; k++) {
    lookInput(now, k, x);
    uint32_t rk = r + k * 7919u;
    if (!rk) rk = 1;
    ps[k] = _look.score(x);
    vs[k] = ps[k] + 0.15f * ((styleRnd(rk) >> 8) / 16777216.f);
  }
  int order[STYLE_PRESET_COUNT + 1];
  for (int k = 0; k <= STYLE_PRESET_COUNT; k++) order[k] = k;
  for (int a = 1; a <= STYLE_PRESET_COUNT; a++)
    for (int b = a; b > 0 && vs[order[b]] > vs[order[b - 1]]; b--) {
      const int t = order[b];
      order[b] = order[b - 1];
      order[b - 1] = t;
    }
  const int c = order[0];
  for (int i = 0; i < 3; i++) {
    _lookTop[i] = order[i];
    _lookTopScore[i] = ps[order[i]];
  }
  if (part >= 0) {
    styleCopyGroup(_style, candidateLook(c), part);
    _lookWorn = false;
    _lastLook = now;
    _dirty = true;
    return;
  }
  lookInput(now, c, _lookIn);
  _lookWorn = true;
  _lookChoice = c;
  _lookScoreNow = ps[c];
  _style = candidateLook(c);
  _wardrobe.current = c;
  _mouthWardrobe.current = 0;
  _lastLook = now;
  _dirty = true;
}

FaceStyle Engine::candidateLook(int c) const {
  FaceStyle s = c == 0 ? _home : _style;
  if (c > 0) {
    styleApplyPreset(s, STYLE_PRESETS[c - 1].name);
    s.pixel = _style.pixel;
    s.glitch = _style.glitch;
    s.daytint = _style.daytint;
    s.rgb_split = _style.rgb_split;
    s.speed = _style.speed;
    s.bounce = _style.bounce;
  }
  return s;
}

void Engine::lookInput(uint32_t now, int c, float* x) {
  const FaceStyle s = candidateLook(c);
  x[0] = _emo.emotion().v;
  x[1] = _emo.emotion().a;
  x[2] = _idle.sleepiness(now);
  x[3] = _needs.attention;
  x[4] = _needs.rest;
  x[5] = _needs.play;
  x[6] = _temper.valence;
  x[7] = _temper.arousal;
  for (int i = 8; i < LookNet::CTX; i++) x[i] = 0.f;
  lookFeatures(s, c - 1, x + LookNet::CTX);
}

// The head's response to a gaze urge (-1..1): 0 inside a dead band of 0.15, then a soft curve up to +-1.
static float headCurve(float u) {
  const float a = fabsf(u) - 0.15f;
  if (a <= 0) return 0.f;
  const float s = tanhf(2.f * a / 0.85f);
  return u < 0 ? -s : s;
}

void Engine::mindStep(uint32_t now) {
  if (now - _lastMind < 100) return;
  _lastMind = now;
  MindInputs in;
  MindSources src;
  src.emotion = _emo.emotion();
  src.mood = _emo.mood();
  src.hour = localHour();
  int day, hour;
  weekNow(day, hour);
  src.weekDay = day;
  src.weekHour = hour;
  src.petRate = _petRate;
  src.pokeRate = _pokeRate;
  src.light = _screenLight;
  src.lastTouch = _lastTouch;
  src.lastEvent = _lastEvent;
  src.presentAt = _presentAt;
  src.trust = _appraisal.trust();
  src.eyeOpenL = _base.eye_open_l;
  src.eyeOpenR = _base.eye_open_r;
  src.emote = emoteActive();
  src.hasTouch = true;
  src.touching = _down;
  src.tx = _tx;
  src.ty = _ty;
  src.hasMotion = true;
  src.carried = _carried;
  src.motion = _motion;
  src.tiltX = _tiltX;
  src.tiltY = _tiltY;
  src.batteryLow = _batteryLow;
  src.watched = _watched;
  src.screen = _screenAct;
  fillMindInputs(in, src, now, _idle, _needs, _tastes);
  _motion *= 0.8f;
  _mind.step(in, 0.1f);
  float u[MindNet::OUT];
  for (int k = 0; k < MindNet::OUT; k++) u[k] = _mind.urge(k);
  _idle.setUrges(u, 1.f);
  const bool asleep = _idle.state(now) == IdleGen::ASLEEP;
  {
    const Appraisal::GlitchMix mix =
        _appraisal.glitchFor(fminf(1.f, fmaxf(0.f, u[mind::U_GLITCH] + 0.5f)), asleep, _emo.emotion().a,
                             fminf(1.f, (_down ? 0.1f : 0.f) + 0.5f * _motion + 0.2f * _screenAct));
    _glitch = mix.drive;
    _glitchScale = mix.scale;
  }
  const float eu = u[mind::U_EMOTE];
  if (eu > 0.4f && !_emoteHeld) {
    _emoteHeld = true;
    if (!asleep && _idle.state(now) == IdleGen::AWAKE && !emoteActive() &&
        now - _lastAutoEmote > (uint32_t)(BEHAVIOUR.emoteGapSeconds * 1000)) {
      const uint8_t own = mindPickEmote(u, now * 2654435761u, _tastes.emote.score);
      playEmote(own);
      _tastes.emote.chose(own, now);
      _lastAutoEmote = now;
    }
  } else if (eu < 0.1f) {
    _emoteHeld = false;
  }
  _pupil = 1.f + 0.3f * u[mind::U_PUPIL];
  // The head: a small urge does nothing (dead band), a large one turns it smoothly toward a limit of 70% of the setting,
  // and it drifts there over about a second (100 ms steps, 12% each). Asleep it goes back to the middle.
  // It follows its own head urges (head_x, head_y) and, a little, where the eyes look; both as slow averages (about 2 s).
  _gazeX += (u[mind::U_GAZE_X] - _gazeX) * 0.05f;
  _gazeY += (u[mind::U_GAZE_Y] - _gazeY) * 0.05f;
  _headUx += (u[mind::U_HEAD_X] - _headUx) * 0.05f;
  _headUy += (u[mind::U_HEAD_Y] - _headUy) * 0.05f;
  const float goalY = asleep ? 0.f : _headTurn * 0.6f * headCurve(0.7f * _headUx + 0.3f * _gazeX);
  const float goalX = asleep ? 0.f : _headTurn * 0.6f * headCurve(0.7f * _headUy + 0.3f * _gazeY);
  const float headRate = 0.08f + 0.1f * fmaxf(0.f, _emo.emotion().a);
  _turnY += (goalY - _turnY) * headRate;
  _turnX += (goalX - _turnX) * headRate;
  const float curious = u[mind::U_CURIOUS];
  if (curious < 0.1f)
    _tiltSide = (_rng ^= _rng << 13, _rng ^= _rng >> 17, _rng ^= _rng << 5, _rng & 1) ? 1.f : -1.f;
  const float goalRoll =
      asleep ? 0.f
             : _tiltSide * 0.5f * _headTurn * (curious > 0.1f ? (curious > 1 ? 0.9f : curious - 0.1f) : 0.f);
  _turnRoll += (goalRoll - _turnRoll) * headRate;
  _fisheyeNow = _fisheyeMax * (0.75f + 0.25f * lensFrom(u[mind::U_LENS]));
  if (u[mind::U_WARDROBE] > 0.5f && !_wardrobeHeld) {
    _wardrobeHeld = true;
    uint32_t roll = now * 2654435761u + 1;
    if (_wardrobeOn && _idle.state(now) == IdleGen::AWAKE &&
        now - _lastLook > (uint32_t)(BEHAVIOUR.lookGapMinutes * 60000) && !_down &&
        (styleRnd(roll) >> 8) / 16777216.f < _lookChance)
      changeLook(now);
  } else if (u[mind::U_WARDROBE] < 0.1f) {
    _wardrobeHeld = false;
  }
  if (u[mind::U_SLEEP] > 0.5f && !_sleepHeld) {
    _sleepHeld = true;
    if (_idle.state(now) == IdleGen::AWAKE && _needs.rest > 0.6f && _idle.idleSeconds(now) >= 120 && !_down)
      _idle.sleepNow(now);
  } else if (u[mind::U_SLEEP] < 0.1f) {
    _sleepHeld = false;
  }
}

void Engine::event(uint8_t type, float intensity) {
  if (_queued < QUEUE) _queue[_queued++] = {type, intensity};
}

void Engine::pointer(bool down, int x, int y) {
  _down = down;
  _px = x;
  _py = y;
  _pointerAt = _now;
}

void Engine::choosePokeSide(uint32_t now) {
  if (_pointerAt && now - _pointerAt < 1500)
    _pokeSide = _px < 160 ? 0 : 1;
  else {
    _rng = _rng * 1664525u + 1013904223u;
    _pokeSide = (_rng >> 16) & 1;
  }
  _pokeSideAt = now;
}

void Engine::pokeOneEye(FaceParams& f, uint32_t now) const {
  if (_pokeSide < 0 || now - _pokeSideAt >= 600) return;
  const float env = sinf(3.14159265f * (now - _pokeSideAt) / 600.f);
  const float shut = 1.f - 0.6f * env;
  if (_pokeSide == 0)
    f.eye_open_l *= shut;
  else
    f.eye_open_r *= shut;
}

void Engine::gaze(float gx, float gy) { _idle.lookAt(gx, gy, _now, 500); }

void Engine::setBase(const FaceParams& p, uint32_t now) {
  _idle.exprChanged(_base, p, now);
  _base = p;
}

void Engine::showFace(int anchor) {
  if (anchor < 0 || anchor >= A_COUNT) return;
  _manual = true;
  _manualAnchor = anchor;
  setBase(ANCHORS[anchor], _now);
}

void Engine::autoFace() {
  _manual = false;
  setBase(mapEmotion(_emo.emotion()), _now);
}

void Engine::playEmote(int id) {
  if (id <= 0 || id >= EM_COUNT) return;
  _emote = (uint8_t)id;
  _emoteMs = EMOTE_MS[id];
  _emoteStart = _now;
  _emoteEnd = _now + _emoteMs;
  _ov = Overlay{EMOTE_PARTS[id].eyes, EMOTE_PARTS[id].deco, "", _emoteStart, _emoteMs};
}

void Engine::toggleSleep() {
  if (_idle.state(_now) == IdleGen::AWAKE)
    _idle.sleepNow(_now);
  else
    _idle.poke(_now);
}

void Engine::setStyle(int preset) {
  if (preset < 0) preset = STYLE_PRESET_COUNT - 1;
  _styleIdx = preset % STYLE_PRESET_COUNT;
  styleApplyPreset(_style, STYLE_PRESETS[_styleIdx].name);
  _home = _style;
  _wardrobe.current = 0;
  _mouthWardrobe.current = 0;
  _dirty = true;
}

const char* Engine::seasonLook() {
  const time_t t = time(nullptr);
  const struct tm* tm = localtime(&t);
  const char* name = styleSeasonName(tm ? tm->tm_mon + 1 : 1, tm ? tm->tm_mday : 1);
  styleApplyPreset(_style, name);
  _home = _style;
  _wardrobe.current = 0;
  _mouthWardrobe.current = 0;
  _dirty = true;
  return name;
}

void Engine::newRobot(uint32_t seed) {
  _rng = seed ? seed : _rng;
  if (!_rng) _rng = 1;
  setPersonality((int)(styleRnd(_rng) % 5));
  _style = styleInitial();
  _home = _style;
  _wardrobe.current = 0;
  _mouthWardrobe.current = 0;
  _dirty = true;
}

void Engine::randomLook(bool all) {
  if (all)
    styleRandomize(_style, _rng);
  else
    styleRandomizeSome(_style, _rng);
  _home = _style;
  _wardrobe.current = 0;
  _mouthWardrobe.current = 0;
  _dirty = true;
}

bool Engine::styleText(const char* options) {
  FaceStyle s = _style;
  if (styleApplyText(s, options) < 0) return false;
  _style = s;
  _home = s;
  _wardrobe.current = 0;
  _dirty = true;
  return true;
}

void Engine::setPersonality(int index) {
  const int n = (int)(sizeof PERSONALITIES / sizeof PERSONALITIES[0]);
  _personality = ((index % n) + n) % n;
  _emo.setPersonality(BEHAVIOUR.personality[_personality]);
  _dirty = true;
}

DayTint Engine::dayTint() const { return dayTintAt(localHour(), _style.daytint); }

const char* Engine::mind() {
  static const char* const N[] = {"awake", "drowsy", "asleep"};
  return N[_idle.state(_now)];
}

void Engine::apply(const EmotionEvent& ev, uint32_t now) {
  const bool wasAsleep = _idle.state(now) == IdleGen::ASLEEP;
  _emo.apply(ev);
  _mind.reward(reward(ev));
  _diary.event(ev.type);
  _temper.react(ev.type, ev.intensity, 1.f);
  if (ev.type == EV_WIN) _tastes.won(ev.intensity, false, now);
  if (ev.type == EV_FAIL) _tastes.lost(ev.intensity, false, now);
  if (reward(ev) != 0) {
    int day, hour;
    weekNow(day, hour);
    _tastes.react(reward(ev), now, day, hour);
    _wardrobe.reward(reward(ev));
    _mouthWardrobe.reward(reward(ev));
    if (_lookWorn) _look.train(_lookIn, reward(ev), 0.02f);
  }
  switch (ev.type) {
    case EV_PET: _needs.fed(0.35f * ev.intensity); break;
    case EV_PRAISE:
    case EV_LOVE: _needs.fed(0.45f * ev.intensity); break;
    case EV_GREET:
      _needs.fed(0.3f * ev.intensity);
      _needs.played(0.1f);
      break;
    case EV_POKE:
      _needs.fed(0.08f * ev.intensity);
      _needs.played(0.08f * ev.intensity);
      break;
    case EV_WIN: _needs.played(0.25f * ev.intensity); break;
    default: break;
  }
  _lastEvent = now;
  _dirty = true;
  if (ev.type == EV_PET) _petRate += 1;
  if (ev.type == EV_POKE || ev.type == EV_ANNOY) _pokeRate += 1;
  if (ev.type == EV_POKE) choosePokeSide(now);
  _idle.poke(now);
  _manual = false;
  setBase(mapEmotion(_emo.emotion()), now);
  if (!wasAsleep && !emoteActive() && now - _lastAutoEmote > 6000) {
    uint8_t e = _appraisal.emoteFor(ev.type, _emo.emotion());
    if (e != EM_NONE) _tastes.emote.chose(e, now);
    if (e != EM_NONE) {
      playEmote(e);
      _lastAutoEmote = now;
    }
  }
}

void Engine::tick(uint32_t now) {
  _now = now;
  if (_wakeAt && now >= _wakeAt) {
    _wakeAt = 0;
    if (_idle.state(now) != IdleGen::AWAKE) _idle.wakeUp(now);
  }
  const float dt = 0.02f;
  StimulusOut so;
  _touch.update(_down, _px, _py, now, so);
  if (so.nEvents) {
    const ZoneFx fx = applyZone(zoneAt(_style, _touch.startX(), _touch.startY()), so);
    if (fx.flinch) {
      _idle.flinch(fx.flinch, now);
      _pokeSide = fx.flinch < 0 ? 0 : 1;
      _pokeSideAt = now;
    }
    if (fx.emote && _idle.state(now) != IdleGen::ASLEEP && !emoteActive() && now - _lastZoneEmote > 2500) {
      playEmote(fx.emote);
      _lastZoneEmote = _lastAutoEmote = now;
    }
  }
  if (_down) {
    _idle.poke(now);
    _idle.lookAt(_touch.gazeX, _touch.gazeY, now, 1200);
    _lastTouch = now;
    _tx = _touch.gazeX;
    _ty = _touch.gazeY;
  }
  _idle.setPetting(_touch.petting);
  for (int i = 0; i < so.nEvents; i++)
    _appraisal.hear(so.events[i], _emo.emotion(), _idle.sleepiness(now), now);
  for (int i = 0; i < _queued; i++) _appraisal.hear(_queue[i], _emo.emotion(), _idle.sleepiness(now), now);
  _queued = 0;
  {
    EmotionEvent due;
    while (_appraisal.pop(now, due)) apply(due, now);
    _appraisal.update(dt);
    _tastes.update(dt);
    if (_wasDown && !_down) _appraisal.touched(_touch.gazeX, _touch.gazeY, now);
    _wasDown = _down;
    float gx, gy;
    if (!_down && _appraisal.glance(now, gx, gy)) _idle.lookAt(gx, gy, now, 700);
  }

  {
    const float h = localHour();
    if (_lastHour >= 0 && h < _lastHour - 12) _diary.newDay(_diary.today() + 1);
    _lastHour = h;
    if (now - _lastSample > 30000) {
      _lastSample = now;
      _diary.mood(_emo.emotion().v, h);
      const Diary::Feel day = _diary.feel();
      _idle.setDay(day.warmth, day.lonely);
    }
  }
  if (_idle.state(now) == IdleGen::ASLEEP) {
    if (!emoteActive() && now - _emoteEnd > _bubbleGap) {
      playEmote(EM_BUBBLE);
      _sleepBubble = true;
      _rng = _rng * 1664525u + 1013904223u;
      _bubbleGap = 1000 + (_rng >> 8) % 7000;
    }
  } else if (_sleepBubble) {
    _sleepBubble = false;
    if (_emote == EM_BUBBLE) _emoteEnd = now;
  }
  _emo.update(dt);
  _needs.update(dt, _idle.state(now) == IdleGen::AWAKE, _idle.state(now) == IdleGen::ASLEEP, _down, false);
  _temper.update(dt);
  _emo.setBaselineShift(
      {_needs.valenceShift() + _temper.valence, _needs.arousalShift() + _temper.arousal, 0});
  if (!_manual) _base = mapEmotion(_emo.emotion());
  _idle.setArousal(_emo.emotion().a);
  _idle.setValence(_emo.emotion().v);
  const float decay = expf(-dt * 0.6931f / 300.f);
  _petRate *= decay;
  _pokeRate *= decay;
  mindStep(now);

  FaceParams face = _base;
  if (emoteActive()) {
    face = emoteFace(_emote, (now - _emoteStart) / (float)_emoteMs);
    if (_shownEmote != _emote || now - _emoteStart < 25) _idle.exprChanged(_base, face, now);
    _shownEmote = _emote;
  } else {
    _shownEmote = 0;
  }
  pokeOneEye(face, now);
  _target = _idle.apply(face, now);
}

void Engine::update(uint32_t nowMs) {
  if (!_started) return;
  while (_sim + 20 <= nowMs) {
    _sim += 20;
    tick(_sim);
  }
  _now = nowMs;
  float dt = (nowMs - _lastSpring) * 1e-3f;
  if (dt > 0.05f) dt = 0.05f;
  _lastSpring = nowMs;
  _spring.setFeel(_style.speed, _style.bounce);
  _spring.step(_target, dt);
}

//==============================
// saving
//==============================
static std::string join(const char* dir, const char* name) {
  std::string p = dir ? dir : "";
  if (!p.empty() && p.back() != '/' && p.back() != '\\') p += '/';
  return p + name;
}

// Each file is written next to its old copy and then renamed over it, so a crash or power cut mid-save keeps the old one
// (the mind net's file is what it has learned). Windows' rename does not replace, hence the remove first.
template <typename Write>
static bool saveFile(const std::string& path, const char* mode, Write write) {
  const std::string tmp = path + ".tmp";
  FILE* f = fopen(tmp.c_str(), mode);
  if (!f) return false;
  const bool ok = write(f) && fflush(f) == 0;
  if (fclose(f) != 0 || !ok) {
    remove(tmp.c_str());
    return false;
  }
  remove(path.c_str());
  return rename(tmp.c_str(), path.c_str()) == 0;
}

bool Engine::save(const char* dir) const {
  bool ok = saveFile(join(dir, "teevee.cfg"), "w", [&](FILE* f) {
    char look[STYLE_TEXT_MAX];
    styleDescribe(_style, look, sizeof look);
    fprintf(f,
            "style=%s\nlook=%s\npersonality=%d\nneeds=%g %g %g\ntemper=%g %g\npresetorder=2\nwardrobe=%d %d",
            STYLE_PRESETS[_styleIdx].name, look, _personality, _needs.attention, _needs.rest, _needs.play,
            _temper.valence, _temper.arousal, _wardrobeOn ? 1 : 0, _wardrobe.current);
    for (int i = 0; i <= STYLE_PRESET_COUNT && i < Wardrobe::MAX; i++) fprintf(f, " %g", _wardrobe.score[i]);
    return fprintf(f, "\nview=%g %g %d\nsenses=screen %d\ntrust=%g\nlookchange=%g %g\n", _headTurn,
                   _fisheyeMax, _lookPinned ? 1 : 0, _screenOn ? 1 : 0, _appraisal.trust(), _lookChance,
                   _lookWhole) > 0;
  });
  ok = saveFile(join(dir, "diary.bin"), "wb",
                [&](FILE* f) { return fwrite(&_diary.saved(), sizeof(Diary::Saved), 1, f) == 1; }) &&
       ok;
  ok = saveFile(join(dir, "tastes.bin"), "wb",
                [&](FILE* f) {
                  const Tastes::Saved s = _tastes.save();
                  return fwrite(&s, sizeof s, 1, f) == 1;
                }) &&
       ok;
  ok = saveFile(join(dir, "mind.bin"), "wb",
                [&](FILE* f) { return fwrite(&_mind.weights(), sizeof(MindNet::Weights), 1, f) == 1; }) &&
       ok;
  ok = saveFile(join(dir, "lookmind.bin"), "wb",
                [&](FILE* f) {
                  static float w[LookNet::WEIGHTS];
                  _look.flatten(w);
                  const uint32_t head[2] = {LookNet::MAGIC, _look.updates};
                  return fwrite(head, sizeof head, 1, f) == 1 && fwrite(w, sizeof w, 1, f) == 1;
                }) &&
       ok;
  return ok;
}

void Engine::applyBehaviour() {
  _needs.attentionSec = BEHAVIOUR.attentionHours * 3600, _needs.restSec = BEHAVIOUR.restHours * 3600,
  _needs.playSec = BEHAVIOUR.playHours * 3600;
  _needs.restoreSec = BEHAVIOUR.sleepRestoreMinutes * 60;
  _idle.setDayGap(BEHAVIOUR.dayGapSeconds);
  _appraisal.setRules(BEHAVIOUR.rule);
  _appraisal.setLiked(_tastes.emote.score);
  mind::INSTINCTS = BEHAVIOUR.instinct;
  mind::INSTINCT_COUNT = BEHAVIOUR.instincts;
}

bool Engine::setBehaviour(const char* text, size_t len, char* err, size_t errN) {
  if (!behaviourParse(text, len, BEHAVIOUR, err, errN)) return false;
  applyBehaviour();
  _emo.setPersonality(BEHAVIOUR.personality[_personality]);
  return true;
}

void Engine::load(const char* dir) {
  if (FILE* f = fopen(join(dir, "behaviour.txt").c_str(), "rb")) {
    static char text[8192];
    const size_t n = fread(text, 1, sizeof text, f);
    fclose(f);
    char err[64];
    setBehaviour(text, n, err, sizeof err);
    if (err[0]) fprintf(stderr, "behaviour.txt: %s\n", err);
    if (BEHAVIOUR.instincts) _mind.init(_rng);
  }
  if (FILE* f = fopen(join(dir, "tastes.bin").c_str(), "rb")) {
    static Tastes::Saved saved;
    const size_t n = fread(&saved, 1, sizeof saved, f);
    if (n) _tastes.load(&saved, n);
    fclose(f);
  }
  if (FILE* f = fopen(join(dir, "teevee.cfg").c_str(), "r")) {
    char line[STYLE_TEXT_MAX + 64];
    int presetOrder = 1;
    while (fgets(line, sizeof line, f)) {
      char val[64];
      if (sscanf(line, "style=%63s", val) == 1 && styleFindPreset(val) >= 0) setStyle(styleFindPreset(val));
      if (sscanf(line, "presetorder=%d", &presetOrder) == 1) continue;
      if (!strncmp(line, "look=", 5)) {
        line[strcspn(line, "\r\n")] = 0;
        styleText(line + 5);
      }
      int p;
      if (sscanf(line, "personality=%d", &p) == 1) setPersonality(p);
      int sc;
      if (sscanf(line, "senses=screen %d", &sc) == 1) _screenOn = sc != 0;
      float tr, lw;
      if (sscanf(line, "trust=%f", &tr) == 1) _appraisal.setTrust(tr);
      if (sscanf(line, "lookchange=%f %f", &tr, &lw) == 2) setLookChange(tr, lw);
      float tv, ta;
      if (sscanf(line, "temper=%f %f", &tv, &ta) == 2) {
        _temper.valence = tv;
        _temper.arousal = ta;
        _temper.clamp();
      }
      {
        float vh, vf;
        int vp;
        if (sscanf(line, "view=%f %f %d", &vh, &vf, &vp) == 3) {
          _headTurn = vh == vh ? (vh < 0 ? 0 : vh > 90 ? 90 : vh) : 0;  // NaN (a damaged file) -> off
          _fisheyeMax = vf == vf ? (vf < 0 ? 0 : vf > 1 ? 1 : vf) : 0;
          _lookPinned = vp != 0;
        }
      }
      if (!strncmp(line, "wardrobe=", 9)) {
        int on = 0, cur = 0, used = 0;
        if (sscanf(line + 9, "%d %d%n", &on, &cur, &used) == 2) {
          _wardrobeOn = on != 0;
          if (presetOrder < 2 && cur > 0) cur = styleOldPreset(cur - 1) + 1;
          _wardrobe.current = cur >= 0 && cur <= STYLE_PRESET_COUNT ? cur : 0;
          const char* p = line + 9 + used;
          for (int i = 0; i < Wardrobe::MAX; i++) {
            float sc;
            int k = 0;
            if (sscanf(p, " %f%n", &sc, &k) != 1) break;
            const int to = presetOrder >= 2 || i == 0 ? i : styleOldPreset(i - 1) + 1;
            if ((to > 0 || i == 0) && to <= STYLE_PRESET_COUNT)
              _wardrobe.score[to] = sc >= -1 && sc <= 1 ? sc : 0;
            p += k;
          }
        }
      }
      float na, nr, np;
      if (sscanf(line, "needs=%f %f %f", &na, &nr, &np) == 3) {
        _needs.attention = na;
        _needs.rest = nr;
        _needs.play = np;
        _needs.clamp();
      }
    }
    fclose(f);
  }
  if (FILE* f = fopen(join(dir, "diary.bin").c_str(), "rb")) {
    static Diary::Saved ds;
    if (fread(&ds, sizeof ds, 1, f) == 1) _diary.load(&ds, sizeof ds);
    fclose(f);
  }
  if (FILE* f = fopen(join(dir, "mind.bin").c_str(), "rb")) {
    static MindNet::Weights w;  // an older save (fewer urges) is shorter: load() carries it over
    const size_t n = fread(&w, 1, sizeof w, f);
    if (n && fgetc(f) == EOF) _mind.load(&w, n);
    fclose(f);
  }
  if (FILE* f = fopen(join(dir, "lookmind.bin").c_str(), "rb")) {
    static float w[LookNet::OLD_WEIGHTS > LookNet::WEIGHTS ? LookNet::OLD_WEIGHTS : LookNet::WEIGHTS];
    uint32_t head[2];
    if (fread(head, sizeof head, 1, f) == 1 && head[0] == LookNet::MAGIC_OLD) {
      const size_t n = fread(w, sizeof(float), sizeof w / sizeof w[0], f);
      if (const int in = LookNet::oldInputsOf((long)n); in > 0 && fgetc(f) == EOF) {
        _look.unflattenOld(w, in);
        _look.updates = head[1];
      }
    } else if (head[0] ==
               LookNet::MAGIC) {  // a net saved when there were fewer presets is shorter: carried over
      const size_t n = fread(w, sizeof(float), sizeof w / sizeof w[0], f);
      if (const int in = LookNet::inputsOf((long)n); in > 0 && fgetc(f) == EOF) {
        _look.unflattenFrom(w, in);
        _look.updates = head[1];
      }
    }
    fclose(f);
  }
  _dirty = false;
}
