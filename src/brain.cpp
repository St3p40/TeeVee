#include "brain_internal.h"
#include "sdcard.h"
#include "portal.h"
#include "core/appraisal.h"
#include "core/head.h"
#include "core/behaviour.h"
#include "app_runtime.h"
#include "imu.h"
#include "battery.h"
#include "audio.h"
#include "core/voice.h"

const char* const STATE_NAMES[] = {"awake", "drowsy", "asleep"};

namespace brain {
IdleGen idle;
Appraisal appraisal;
EmotionEngine emo;
TouchGestures touch;
LightSense light;
DayClock dayClock;
FaceParams base;
bool manual = false;
uint32_t now = 0, manualUntil = 0;  // manualUntil 0 = manual face holds until `auto` / next event
PAD holdPad;
uint32_t holdUntil = 0;  // network {"emotion":..,"ttl":s} keeps the emotion pinned until then
uint32_t pomoEnd = 0, pomoDoneUntil = 0;
char weather[14] = "", ciName[14] = "";
float tempC = 0;
bool hasTemp = false;
Settings settings = SETTINGS_DEFAULTS;
CustomStore custom;
uint8_t ci = CI_NONE;
FaceStyle style;
MindNet mindNet;
Diary diary;
Temper temper;
Wardrobe wardrobe;
Wardrobe mouthWardrobe;
FaceStyle homeStyle;
static uint32_t lastLookChange = 0;
Needs needs;
Preferences store;
float petRate = 0, pokeRate = 0;  // decayed recent counts (half-life 5 min), mind net sense
uint32_t mindDirty = 0, lastMindSave = 0;
TouchPanel touchPanel;
uint32_t lastHeard = 0;  // the last time someone was heard talking (0 = never)
float simLight = -1;
bool logTouch = false, logPad = false;
static uint32_t lastLog = 0, lastUpdate = 0, lastState1s = 0;
static int lastState = IdleGen::AWAKE;

void saveStyle() {
  store.putBytes("style", &style, sizeof style);
  setStyleShared(style);
}

Personality currentPersonality() {
  Personality p = BEHAVIOUR.personality[settings.personality < 5 ? settings.personality : 0];
  p.sensitivity *= settings.sensitivity;
  p.emotionHalfLife *= settings.recovery;
  p.moodHalfLife *= settings.recovery;
  return p;
}

ScreenIdle screenIdle;
Tastes tastes;
char suggestedApp[12] = "";

// The behaviour file: the card's /teevee/behaviour.txt, else /behaviour.txt in flash. Nothing there: the defaults. err (64 bytes, may be null): what was wrong, or the
// first skipped line. False only if a file is there and is not a behaviour file.
bool loadBehaviour(char* err) {
  char why[64] = "";
  Scratch text(4096);
  bool ok = true;
  if (text) {
    File f = sdcard::mounted() && sdcard::fs().exists("/teevee/behaviour.txt")
                 ? sdcard::fs().open("/teevee/behaviour.txt", "r")
                 : File();
    if (!f && LittleFS.exists("/behaviour.txt")) f = LittleFS.open("/behaviour.txt", "r");
    if (f) {
      const size_t n = f.read((uint8_t*)text.p, text.n);
      f.close();
      ok = behaviourParse((const char*)text.p, n, BEHAVIOUR, why, sizeof why);
      Serial.printf("behaviour: %s\n", why[0] ? why : "file loaded");
    }
  }
  if (err) snprintf(err, 64, "%s", why);
  needs.attentionSec = BEHAVIOUR.attentionHours * 3600, needs.restSec = BEHAVIOUR.restHours * 3600,
  needs.playSec = BEHAVIOUR.playHours * 3600;
  needs.restoreSec = BEHAVIOUR.sleepRestoreMinutes * 60;
  idle.setDayGap(BEHAVIOUR.dayGapSeconds);
  appraisal.setRules(BEHAVIOUR.rule);
  appraisal.setLiked(tastes.emote.score);
  mind::INSTINCTS = BEHAVIOUR.instinct;
  mind::INSTINCT_COUNT = BEHAVIOUR.instincts;
  emo.setPersonality(currentPersonality());
  return ok;
}

static void gamesStep() {
  static bool wasApp = false;
  static uint32_t lastSuggest = 0, suggestedAt = 0;
  const bool onScreen = apps::active();
  if (onScreen != wasApp) {
    wasApp = onScreen;
    if (onScreen) {
      tastes.gameStarted(apps::current(), now);
      if (suggestedApp[0] && !strcmp(suggestedApp, apps::current())) tastes.app.react(0.5f, now, 0);
      suggestedApp[0] = 0;
    } else {
      tastes.gameEnded();
    }
  }
  if (suggestedApp[0] && now - suggestedAt > 120000) {
    tastes.app.chose(tastes.appSlot(suggestedApp), now);
    tastes.app.react(-0.15f, now, 0);
    tastes.gameEnded();
    suggestedApp[0] = 0;
  }
  const int n = apps::count();
  if (onScreen || !n || !settings.auto_emotes || idle.state(now) != IdleGen::AWAKE || needs.play < 0.55f ||
      idle.idleSeconds(now) < 30 || emoteActive())
    return;
  if (now < 60000 || now - lastSuggest < (uint32_t)(BEHAVIOUR.suggestGapMinutes * 60000)) return;
  lastSuggest = now;
  const char* names[16];
  const int count = n < 16 ? n : 16;
  for (int i = 0; i < count; i++) names[i] = apps::name(i);
  const int c = tastes.suggest(names, count, []() { return esp_random() / 4294967296.f; });
  if (c < 0) return;
  snprintf(suggestedApp, sizeof suggestedApp, "%s", names[c]);
  suggestedAt = now;
  playEmoteText(EM_QUESTION, suggestedApp);
  Serial.printf("it would like to play %s\n", suggestedApp);
}

void applySettings() {
  emo.setPersonality(currentPersonality());
  idle.setBehaviourRate(settings.behaviours);
  idle.setAmounts(settings.squash_amt, settings.motion_amt, settings.gaze_amt);
  mindNet.setRates(settings.learn_rate, settings.explore);
  memcpy(expressionWeights(), settings.expr_w, sizeof settings.expr_w);
}

void saveSettings() {
  store.putBytes("settings", &settings, sizeof settings);
  applySettings();
}

// The mind net (3.8 KB) and the diary (0.8 KB) are files in the flash filesystem, not NVS: the NVS partition is only 20 KB, shared with Wi-Fi and the
// other settings, and a 3.8 KB blob did not fit any more once the net grew (it silently was not saved). Written to a temp file and renamed.
bool blobWrite(const char* key, const void* data, size_t len) {
  if (!LittleFS.exists("/state")) LittleFS.mkdir("/state");
  char tmp[32], dst[32];
  snprintf(tmp, sizeof tmp, "/state/%s.tmp", key);
  snprintf(dst, sizeof dst, "/state/%s.bin", key);
  File f = LittleFS.open(tmp, "w");
  if (!f) return false;
  const bool ok = f.write((const uint8_t*)data, len) == len;
  f.close();
  if (!ok) {
    LittleFS.remove(tmp);
    return false;
  }
  LittleFS.remove(dst);
  return LittleFS.rename(tmp, dst);
}

size_t blobRead(const char* key, void* buf, size_t cap) {
  char path[32];
  snprintf(path, sizeof path, "/state/%s.bin", key);
  File f = LittleFS.open(path, "r");
  if (!f) return 0;
  const size_t n = f.size() <= cap ? f.read((uint8_t*)buf, f.size()) : 0;
  f.close();
  return n;
}

void loadPersisted() {
  store.begin("teevee_x", false);
  // Saved settings are a prefix of today's struct: fields added since load as defaults.
  size_t setLen = store.getBytesLength("settings");
  if (setLen) {
    static uint8_t blob[sizeof(Settings)];
    if (setLen > sizeof blob) setLen = sizeof blob;
    store.getBytes("settings", blob, setLen);
    settingsLoad(settings, blob, setLen);
  }
#if BOARD_IMU_BUILTIN
  // A board with its own motion sensor turns motion on once; after that the setting is the owner's.
  if (!store.getBool("imu_auto", false)) {
    settings.motion = true;
    store.putBytes("settings", &settings, sizeof settings);
    store.putBool("imu_auto", true);
  }
#endif
#if BOARD_IMU_BUILTIN && !defined(TEEVEE_NO_WARP)
  // The same for the head turn, on a board that can draw it: on once (20 degrees), then the setting is the owner's.
  if (!store.getBool("turn_auto", false)) {
    if (settings.head_turn <= 0) settings.head_turn = 20;
    store.putBytes("settings", &settings, sizeof settings);
    store.putBool("turn_auto", true);
  }
#endif
  // A style saved by older firmware is a prefix of today's: newer options load as defaults.
  uint8_t blob[sizeof(FaceStyle)];
  size_t styleLen = store.getBytesLength("style");
  if (styleLen > sizeof blob) styleLen = sizeof blob;
  if (styleLen) store.getBytes("style", blob, styleLen);
  if (styleLen)
    styleLoad(style, blob, styleLen);
  else
    style = styleInitial();
  setStyleShared(style);
  customInit(custom);
  if (store.getBytesLength("custom") == sizeof custom) {
    store.getBytes("custom", &custom, sizeof custom);
    if (custom.version != CUSTOM_VERSION || custom.nFaces > MAX_CUSTOM || custom.nEmotes > MAX_CUSTOM)
      customInit(custom);
    else
      customSanitize(custom);
  }
  customFaces() = &custom;
  diary.begin(1);
  {
    Diary::Saved* ds = (Diary::Saved*)malloc(sizeof(Diary::Saved));
    if (ds) {
      size_t n = blobRead("diary", ds, sizeof(Diary::Saved));
      if (!n && store.getBytesLength("diary") ==
                    sizeof(Diary::Saved)) {  // an older version kept it in NVS: move it
        store.getBytes("diary", ds, sizeof(Diary::Saved));
        n = sizeof(Diary::Saved);
        blobWrite("diary", ds, n);
        store.remove("diary");
      }
      if (n == sizeof(Diary::Saved)) diary.load(ds, n);
      free(ds);
    }
  }
  loadBehaviour(nullptr);
  mindNet.init(esp_random());
  {
    Temper::Saved ts;
    if (store.getBytesLength("temper") == sizeof ts) {
      store.getBytes("temper", &ts, sizeof ts);
      temper.load(&ts, sizeof ts);
    }
    Wardrobe::Saved ws;
    if (const size_t n = store.getBytesLength("wardrobe");
        n && n <= sizeof ws) {  // older saves are shorter (fewer looks): load() takes them
      store.getBytes("wardrobe", &ws, n);
      wardrobe.load(&ws, n, styleOldPreset);
    }
    if (const size_t n = store.getBytesLength("mouthwr"); n && n <= sizeof ws) {
      store.getBytes("mouthwr", &ws, n);
      mouthWardrobe.load(&ws, n);
    }
    homeStyle = style;
    if (store.getBytesLength("home") <= sizeof homeStyle &&
        store.getBytesLength("home") >
            0) {  // a prefix of today's style loads with the newer options at their defaults
      uint8_t blob[sizeof(FaceStyle)];
      const size_t len = store.getBytes("home", blob, sizeof blob);
      styleLoad(homeStyle, blob, len);
    }
  }
  {
    Needs::Saved ns;
    if (store.getBytesLength("needs") == sizeof ns) {
      store.getBytes("needs", &ns, sizeof ns);
      needs.load(&ns, sizeof ns);
    }
  }
  store.remove("mind");  // (older versions kept the net in NVS; it never fit any more)
  // (an older save is a different size, and the last one in floats is bigger than today's: load() carries it over)
  static_assert(MindNet::MAX_SAVE >= sizeof(MindNet::Weights), "the buffer must hold today's save too");
  void* saved = malloc(MindNet::MAX_SAVE);
  if (saved) {
    static_assert(IdleGen::URGES == MindNet::OUT, "IdleGen::URGES must match the mind net's urges");
    const size_t got = blobRead("mind", saved, MindNet::MAX_SAVE);
    if (got && mindNet.load(saved, got))
      Serial.printf("mind: restored, %lu reactions learned from\n", (unsigned long)mindNet.weights().updates);
    free(saved);
  }
  if (Tastes::Saved* ts = (Tastes::Saved*)malloc(sizeof(Tastes::Saved))) {
    if (const size_t got = blobRead("tastes", ts, sizeof(Tastes::Saved))) tastes.load(ts, got);
    free(ts);
  }
}

static void changeLook() {
  const bool whole = esp_random() / 4294967296.f < settings.look_whole;
  const uint32_t pick = esp_random() % 100;
  const int part = whole       ? -1
                   : pick < 40 ? (int)SG_MOUTH
                   : pick < 60 ? (int)SG_COLOUR
                   : pick < 80 ? (int)SG_EYES
                               : (int)SG_PUPILS;
  if (part == SG_MOUTH) {
    static const char* names[MOUTH_CHOICES];
    int n = 0;
    for (int c = 1; c <= MOUTH_CHOICES; c++) names[n++] = MOUTH_STYLES[mouthChoiceStyle(c)];
    const int c = mouthWardrobe.pick(names, n, emo.emotion().v, emo.emotion().a, idle.sleepiness(now),
                                     []() { return esp_random() / 4294967296.f; });
    style.mouth_style = c == 0 ? homeStyle.mouth_style : mouthChoiceStyle(c);
    mouthWardrobe.current = c;
    lastLookChange = now;
    saveStyle();
    Serial.printf("wardrobe: mouth %s\n", MOUTH_STYLES[style.mouth_style]);
    return;
  }
  if (part == SG_EYES && (esp_random() & 1)) {
    const int c = tastes.eye.pick(EYE_COUNT, style.eye_style, []() { return esp_random() / 4294967296.f; });
    if (c >= 0) {
      style.eye_style = (uint8_t)c;
      tastes.eye.chose(c, now);
      lastLookChange = now;
      saveStyle();
      return;
    }
  }
  static const char* names[STYLE_PRESET_COUNT];
  for (int i = 0; i < STYLE_PRESET_COUNT; i++) names[i] = STYLE_PRESETS[i].name;
  const int c = wardrobe.pick(names, STYLE_PRESET_COUNT, emo.emotion().v, emo.emotion().a,
                              idle.sleepiness(now), []() { return esp_random() / 4294967296.f; });
  FaceStyle s = c == 0 ? homeStyle : style;
  if (c > 0) {
    styleApplyPreset(s, STYLE_PRESETS[c - 1].name);
    s.pixel = style.pixel;
    s.glitch = style.glitch;
    s.daytint = style.daytint;
    s.rgb_split = style.rgb_split;
    s.speed = style.speed;
    s.bounce = style.bounce;
  }
  if (part >= 0) {
    styleCopyGroup(style, s, part);
    lastLookChange = now;
    saveStyle();
    Serial.printf("wardrobe: a part (%d) from %s\n", part,
                  c == 0 ? "its home look" : STYLE_PRESETS[c - 1].name);
    return;
  }
  style = s;
  wardrobe.current = c;
  mouthWardrobe.current = 0;
  lastLookChange = now;
  saveStyle();
  Serial.printf("wardrobe: wearing %s\n", c == 0 ? "its home look" : STYLE_PRESETS[c - 1].name);
}

static uint32_t lastTouchMs = 0, lastEventMs = 0;
static bool touchingNow = false;
static bool buttonPetting = false;
static float touchGX = 0, touchGY = 0;

void mindSense(MindInputs& in) {
  MindSources src;
  src.emotion = emo.emotion();
  src.mood = emo.mood();
  src.hour = dayClock.known() ? dayClock.hour(now) : -1.f;
  struct tm tm;
  if (getLocalTime(&tm, 0)) src.weekDay = tm.tm_wday, src.weekHour = tm.tm_hour;
  src.petRate = petRate;
  src.pokeRate = pokeRate;
  src.light = simLight >= 0 ? light.level() : -1.f;  // no light sensor on the boards: only the simulated level
  src.lastTouch = lastTouchMs;
  src.lastEvent = lastEventMs;
  src.trust = appraisal.trust();
  src.eyeOpenL = base.eye_open_l;
  src.eyeOpenR = base.eye_open_r;
  src.emote = emoteActive();
  src.hasTouch = BOARD_HAS_TOUCH;
  src.touching = touchingNow;
  src.tx = touchGX;
  src.ty = touchGY;
#if BOARD_IMU_SDA >= 0  // the Lilka has no IMU
  const imu::State s = imu::state();
  if (s.live) {
    src.hasMotion = true;
    src.carried = s.carried;
    src.motion = fminf(1.f, fabsf(s.last.gx) / 300.f + fabsf(s.last.gy) / 300.f + fabsf(s.last.gz) / 300.f);
    src.tiltX = s.tiltX;
    src.tiltY = s.tiltY;
  }
#endif
  if (lastHeard) src.presentAt = lastHeard;  // someone talking nearby (boards with a microphone)
  fillMindInputs(in, src, now, idle, needs, tastes);  // battery, watched and screen stay 0: desktop senses
}

// The head turn and the lens: the motion sensor and the mind net together, through HeadLife (core/head.h), which gives the head a
// weight, a mood, boredom and turns of its own. `u` is the mind net's urges, or null with the mind net off (the sensor still turns it).
static HeadLife headLife;
static float headRoll = 0;  // degrees: curiosity tips the head to one side (added to the levelling roll in reactMotion)
static void headTurn(const float* u, float dt) {
  HeadLife::In in;
#if BOARD_IMU_SDA >= 0  // the Lilka has no IMU
  const imu::State s = imu::state();
  if (s.live) in.sensor = true, in.carried = s.carried, in.tiltX = s.tiltX, in.tiltY = s.tiltY;
#endif
  in.asleep = idle.state(now) == IdleGen::ASLEEP;
  in.arousal = emo.emotion().a;
  in.sleepy = idle.sleepiness(now);
  const float m = settings.mind_amount;
  if (u) {
    in.gazeX = u[mind::U_GAZE_X] * m, in.gazeY = u[mind::U_GAZE_Y] * m;
    in.headX = u[mind::U_HEAD_X] * m, in.headY = u[mind::U_HEAD_Y] * m;
    in.curious = u[mind::U_CURIOUS] * m;
  }
  const HeadLife::Out o = headLife.step(in, dt);
  g_turnY = settings.head_turn * o.turnY;
  g_turnX = settings.head_turn * o.turnX;
  headRoll = settings.head_turn * o.roll;
  g_fisheye = settings.fisheye * fminf(1.f, o.lens + (u ? lensFrom(u[mind::U_LENS] * m) : 0.f));
  // The turned face hides the corner panels: flat while the web page's PIN must be read off the screen.
  if (netPinShowing()) g_turnX = g_turnY = g_fisheye = headRoll = 0;
}

// True with the chance p (0..1): for what it does sometimes and not every time.
static bool chance(float p) { return (esp_random() >> 8) / 16777216.f < p; }

void mindStep() {
  static uint32_t last = 0;
  if (now - last < 100) return;
  const float dt = (now - last) * 1e-3f > 0.5f ? 0.1f : (now - last) * 1e-3f;
  last = now;
  static const float none[MindNet::OUT] = {};
  if (!settings.learner || settings.mind_amount <= 0) {
    idle.setUrges(none, 0);
    g_glitchDrive = 0;
    g_glitchScale = appraisal.glitchFor(0, idle.state(now) == IdleGen::ASLEEP, emo.emotion().a, 0).scale;
    g_pupilScale = 1.f;
    headTurn(nullptr, dt);
    return;
  }
  MindInputs in;
  mindSense(in);
  mindNet.step(in, dt);
  float u[MindNet::OUT];
  for (int k = 0; k < MindNet::OUT; k++) u[k] = mindNet.urge(k);
  idle.setUrges(u, settings.mind_amount);
  apps::setUrges(u);
  g_pupilScale = 1.f + 0.3f * u[mind::U_PUPIL] * settings.mind_amount;
  headTurn(u, dt);
  {
    const Appraisal::GlitchMix mix =
        appraisal.glitchFor(fminf(1.f, fmaxf(0.f, (u[mind::U_GLITCH] + 0.5f) * settings.mind_amount)),
                            idle.state(now) == IdleGen::ASLEEP, emo.emotion().a, touchingNow ? 0.1f : 0.f);
    g_glitchDrive = mix.drive;
    g_glitchScale = mix.scale;
  }
  static bool emoteHeld = false;
  const float eu = u[mind::U_EMOTE];
  const bool asleep = idle.state(now) == IdleGen::ASLEEP;
  if (eu > 0.4f && !emoteHeld) {
    emoteHeld = true;
    if (settings.auto_emotes && !emoteActive() && !asleep && idle.state(now) == IdleGen::AWAKE &&
        now - lastAutoEmote > (uint32_t)(BEHAVIOUR.emoteGapSeconds * 1000)) {
      const uint8_t own = mindPickEmote(u, esp_random(), tastes.emote.score);
      playEmote(own);
      tastes.emote.chose(own, now);
      lastAutoEmote = now;
    }
  } else if (eu < 0.1f) {
    emoteHeld = false;
  }
  // The wardrobe urge: it wants a change of look (only when the setting is on, now and then, never while touched or in an app).
  static bool wardrobeHeld = false;
  if (u[mind::U_WARDROBE] > 0.5f && !wardrobeHeld) {
    wardrobeHeld = true;
    if (settings.wardrobe && idle.state(now) == IdleGen::AWAKE &&
        now - lastLookChange > (uint32_t)(BEHAVIOUR.lookGapMinutes * 60000) && !touchingNow &&
        !apps::active() && esp_random() / 4294967296.f < settings.look_chance)
      changeLook();
  } else if (u[mind::U_WARDROBE] < 0.1f) {
    wardrobeHeld = false;
  }
  static bool sleepHeld = false;
  if (u[mind::U_SLEEP] > 0.5f && !sleepHeld) {
    sleepHeld = true;
    if (idle.state(now) == IdleGen::AWAKE && needs.rest > 0.6f && idle.idleSeconds(now) >= 120 &&
        !touchingNow && !apps::active())
      idle.sleepNow(now);
  } else if (u[mind::U_SLEEP] < 0.1f) {
    sleepHeld = false;
  }
}

// Owner reactions that teach the mind net (events from the PC count too; CI/pomodoro outcomes don't).
float eventReward(const EmotionEvent& e) {
  switch (e.type) {
    case EV_PET: return settings.r_pet * e.intensity;
    case EV_PRAISE: return settings.r_praise * e.intensity;
    case EV_LOVE: return settings.r_love * e.intensity;
    case EV_GREET: return settings.r_greet * e.intensity;
    case EV_POKE: return settings.r_poke * e.intensity;
    case EV_ANNOY: return settings.r_annoy * e.intensity;
    case EV_SCOLD: return settings.r_scold * e.intensity;
    default: return 0;
  }
}

bool applyStyleText(const char* text) {
  FaceStyle s = style;
  if (!strcmp(text, "season")) {
    struct tm tm;
    if (!getLocalTime(&tm, 0)) return false;
    text = styleSeasonName(tm.tm_mon + 1, tm.tm_mday);
  }
  if (!strcmp(text, "random") || !strcmp(text, "random all")) {
    static uint32_t rng = 0;
    if (!rng) rng = esp_random();
    if (text[6])
      styleRandomize(s, rng);
    else
      styleRandomizeSome(s, rng);
  } else if (!styleApplyPreset(s, text) && styleApplyText(s, text) <= 0) {
    return false;
  }
  style = s;
  homeStyle = s;
  wardrobe.current = 0;
  mouthWardrobe.current = 0;
  saveStyle();
  return true;
}

void setBase(const FaceParams& p) {
  idle.exprChanged(base, p, now);
  base = p;
}

void pushEvent(uint8_t type, float k) {
  EmotionEvent e{type, k};
  xQueueSend(g_events, &e, 0);
}

int dominantAnchor() {
  float w[A_COUNT];
  anchorWeights(emo.emotion(), w);
  int b = 0;
  for (int i = 1; i < A_COUNT; i++)
    if (w[i] > w[b]) b = i;
  return b;
}

void startPomodoro(float minutes) {
  if (!(minutes > 0)) minutes = 0;  // NaN too
  if (minutes > 1440) minutes = 1440;
  pomoEnd = minutes > 0 ? now + (uint32_t)(minutes * 60000) : 0;
  pomoDoneUntil = 0;
  if (minutes > 0) pushEvent(EV_GREET, 0.4f);
}

// CI transitions move the emotion: going green = win, going red = fail.
void setCi(uint8_t status, const char* name) {
  if (name) snprintf(ciName, sizeof ciName, "%s", name);
  if (status == ci) return;
  if (status == CI_PASS && ci != CI_NONE) pushEvent(EV_WIN, 0.7f);
  if (status == CI_FAIL) pushEvent(EV_FAIL, 0.8f);
  ci = status;
}

int ciFromString(const char* s) {
  if (!s) return -1;
  if (!strcmp(s, "pass") || !strcmp(s, "success")) return CI_PASS;
  if (!strcmp(s, "fail") || !strcmp(s, "failure")) return CI_FAIL;
  if (!strcmp(s, "running") || !strcmp(s, "in_progress") || !strcmp(s, "queued")) return CI_RUNNING;
  if (!strcmp(s, "none")) return CI_NONE;
  return -1;
}

void setWeather(float celsius, const char* text) {
  tempC = celsius;
  hasTemp = true;
  snprintf(weather, sizeof weather, "%s", text ? text : "");
}

// Screens in BOOT-button order: the face, then the apps by name. dir +1 = next, -1 = previous (wraps around).
void switchScreen(int dir) {
  int n = apps::count() + 1, cur = 0;
  if (apps::active())
    for (int i = 0; i < apps::count(); i++)
      if (!strcmp(apps::name(i), apps::current())) cur = i + 1;
  int next = ((cur + dir) % n + n) % n;
  if (next == 0)
    apps::stop();
  else
    apps::start(apps::name(next - 1));
}

// The BOOT button (IO0, pulled up, pressed = low) goes through the screens and never reaches an app: a short press = the next screen (the face, then the apps
// by name, then the face again); long press (0.8 s) = back to the face from an app, or sleep from the face. On the face, quick extra presses teach the mind net:
// two presses = "good" (praise), three = "no" (scold), so a single press waits a moment to see if more come.
void senseButton() {
  static bool inited = false, down = false, longDone = false;
  static uint32_t since = 0, lastRelease = 0;
  static int presses = 0;
  if (!inited) {
    for (int pin : BOARD_BOOT_PINS) pinMode(pin, INPUT_PULLUP);
    inited = true;
  }
  bool pressed = false;
  for (int pin : BOARD_BOOT_PINS) pressed = pressed || digitalRead(pin) == LOW;
  static bool wakePress = false;
  if (pressed && screenIdle.wake(now)) wakePress = true;
  if (wakePress) {
    if (!pressed) wakePress = false;
    pressed = false;
  }
  if (pressed && !down) {
    down = true;
    longDone = false;
    since = now;
  } else if (pressed && down && !longDone && now - since >= 800) {
    longDone = true;
    presses = 0;
    idle.poke(now);
    if (apps::active())
      apps::stop();
    else if (idle.state(now) == IdleGen::AWAKE)
      idle.sleepNow(now);
  } else if (!pressed && down) {
    down = false;
    if (!longDone && now - since >= 30) {
      idle.poke(now);
      if (apps::active()) {
        switchScreen(1);
      } else {
        presses++;
        lastRelease = now;
      }
    }
  }
  if (presses && now - lastRelease > 450) {
    if (presses == 1) {
      if (apps::count() > 0) switchScreen(1);
    } else if (presses == 2) {
      pushEvent(EV_PRAISE, 1.0f);
      Serial.println("button: good");
    } else {
      pushEvent(EV_SCOLD, 1.0f);
      Serial.println("button: no");
    }
    presses = 0;
  }
}

#if BOARD_HAS_BUTTONS
void senseButtons() {
  struct Btn {
      int pin;
      bool down;
      uint32_t since, lastRepeat;
  };
  static Btn b[] = {{BTN_A}, {BTN_B}, {BTN_C}, {BTN_D}, {BTN_UP}, {BTN_DOWN}, {BTN_LEFT}, {BTN_RIGHT}};
  static const int APP_INDEX[] = {4, 5, 6, 7, 0, 1, 2, 3};
  static bool inited = false;
  if (!inited) {
    for (Btn& x : b) pinMode(x.pin, INPUT_PULLUP);
    inited = true;
  }
  buttonPetting = false;
  bool anyPressed = false;
  for (const Btn& x : b) anyPressed = anyPressed || digitalRead(x.pin) == LOW;
  static bool wakePress = false;
  if (anyPressed && screenIdle.wake(now)) wakePress = true;
  if (wakePress) {
    if (!anyPressed) wakePress = false;
    return;
  }
  for (Btn& x : b) {
    const bool pressed = digitalRead(x.pin) == LOW;
    const bool pressNow = pressed && !x.down && now - x.since >= 30;
    const bool releaseNow = !pressed && x.down && now - x.since >= 30;
    if (pressed != x.down && (pressNow || releaseNow)) x.down = pressed, x.since = now;
    if (apps::active()) {
      if (pressNow || releaseNow) apps::button(APP_INDEX[&x - b], pressNow), idle.poke(now);
      continue;
    }
    if (!pressed) continue;
    idle.poke(now);
    switch (x.pin) {
      case BTN_A:
        buttonPetting = true;
        if (pressNow)
          pushEvent(EV_PET, 0.5f), x.lastRepeat = now;
        else if (now - x.lastRepeat >= 1200)
          pushEvent(EV_PET, 0.35f), x.lastRepeat = now;
        break;
      case BTN_B:
        if (pressNow) pushEvent(EV_POKE, 0.6f);
        break;
      case BTN_C:
        if (pressNow) pushEvent(EV_PRAISE, 1.0f);
        break;
      case BTN_D:
        if (pressNow) pushEvent(EV_SCOLD, 1.0f);
        break;
      case BTN_UP: idle.lookAt(0, -1, now, 600); break;
      case BTN_DOWN: idle.lookAt(0, 1, now, 600); break;
      case BTN_LEFT: idle.lookAt(-1, 0, now, 600); break;
      case BTN_RIGHT: idle.lookAt(1, 0, now, 600); break;
    }
  }
}
#endif

void senseStimuli(float dt) {
  StimulusOut so;
  int tx = 0, ty = 0;
  bool touched = touchPanel.read(tx, ty, g_madctl);
  static HoldDetector hold;
  static bool wasTouched = false, ignoreTouch = false;
  static int lastX = 0, lastY = 0;
  if (touched && screenIdle.wake(now)) ignoreTouch = true;
  if (ignoreTouch) {  // after hold-to-exit: ignore the finger until it lifts, so leaving doesn't also poke the face
    if (!touched) ignoreTouch = false;
    touched = false;
  }
  bool wasPetting = touch.petting;
  if (apps::active()) {
    if (touched) {
      apps::touch(wasTouched ? TOUCH_MOVE : TOUCH_DOWN, tx, ty);
      idle.poke(now);
    } else if (wasTouched) {
      apps::touch(TOUCH_UP, lastX, lastY);
    }
    if (hold.update(touched, tx, ty, now, 2500)) {
      apps::stop();
      ignoreTouch = true;
    }
    StimulusOut ignored;
    touch.update(false, 0, 0, now, ignored);
  } else {
    touch.update(touched, tx, ty, now, so);
    if (so.nEvents) {
      static uint32_t lastZoneEmote = 0;
      const ZoneFx fx = applyZone(zoneAt(style, touch.startX(), touch.startY()), so);
      if (fx.flinch) idle.flinch(fx.flinch, now);
      if (fx.emote && settings.auto_emotes && idle.state(now) != IdleGen::ASLEEP && !emoteActive() &&
          now - lastZoneEmote > 2500) {
        playEmote(fx.emote);
        lastZoneEmote = lastAutoEmote = now;
      }
    }
    static uint32_t touchStart = 0;
    static bool setupFired = false;
    if (!touched) {
      touchStart = 0;
      setupFired = false;
    } else if (!touchStart) {
      touchStart = now;
    } else if (!setupFired && now - touchStart > 8000 && !touch.petting) {
      setupFired = true;
      netStartSetup();
      pushEvent(EV_STARTLE, 0.3f);
    }
    if (touched) {
      idle.poke(now);
      idle.lookAt(touch.gazeX, touch.gazeY, now, 1200);
    }
  }
  if (touched) {
    lastX = tx;
    lastY = ty;
    if (logTouch) Serial.printf("touch %ld,%ld\n", (long)tx, (long)ty);
  }
  wasTouched = touched;
  touchingNow = touched || buttonPetting;
  if (touched) {
    lastTouchMs = now;
    if (!apps::active()) {
      touchGX = touch.gazeX;
      touchGY = touch.gazeY;
    }
  }
  idle.setPetting(touch.petting || buttonPetting);
  if (logTouch && touch.petting != wasPetting) Serial.println(touch.petting ? "petting" : "petting end");
  const float level = simLight >= 0 ? simLight : 1.f;
  if (light.update(level, dt, so)) idle.poke(now);
  apps::setLight(light.level());
  apps::setMood(emo.emotion().v, emo.emotion().a, emo.emotion().d);
  apps::setNeeds(needs.attention, needs.rest, needs.play);
  if (const uint8_t e = apps::takeFaceEmote()) playEmote(e);
  {
    char shown[16];
    uint32_t showMs;
    if (apps::takeFaceShow(shown, sizeof shown, showMs)) showFace(shown, showMs);
    float lx, ly;
    if (apps::takeFaceLook(lx, ly)) idle.lookAt(lx, ly, now, 800);
  }
  for (int i = 0; i < so.nEvents; i++) xQueueSend(g_events, &so.events[i], 0);

  float energy = dayClock.energy(now);
  emo.setBaselineShift({0.05f * energy + needs.valenceShift() + temper.valence,
                        0.25f * energy + needs.arousalShift() + temper.arousal, 0});
  uint32_t after =
      settings.sleep_after ? settings.sleep_after * 60000UL : 0x7FFFFFFFUL;  // 0 = never from idleness
  if (settings.night_sleep && dayClock.night(now)) after = 60000;
  if (light.dark) after = 30000;
  idle.setDrowsyAfter(after);
  {
    const DayTint dt = dayTintAt(dayClock.known() ? dayClock.hour(now) : -1.f, style.daytint);
    g_dayHue = dt.hue;
    g_dayVal = dt.val;
  }
  // Backlight: setting, dimmed to a quarter while asleep or at night, off when nobody has done anything for a while.
  // Never over something that has to be seen: the PIN, the setup details, a firmware update, an app, a timer, or while the frame rate is being measured.
  const bool mustShow = netSetupMode() || netPinShowing() || portalOtaProgress() >= 0 || apps::active() ||
                        pomoEnd || pomoDoneUntil > now || g_bench || g_stats;
  const bool dark = screenIdle.update(now, settings.screen_off, mustShow);
  g_screenOff = dark;
  // With the screen dark nobody is looking: the radio dozes, and on a board that runs from a battery the processor drops to a
  // third of its speed (the robot's own life needs very little of it; drawing is what needs the rest). Both come back with the screen.
  static bool saving = false;
  if (dark != saving) {
    saving = dark;
    netPowerSave(dark);
#if BOARD_HAS_BATTERY
    setCpuFrequencyMhz(dark ? 80 : 240);
#endif
    Serial.printf("power: %s\n", dark ? "saving (screen dark)" : "full");
  }
  bool dim = settings.night_dim && (idle.state(now) == IdleGen::ASLEEP || dayClock.night(now));
  g_brightness = dark  ? 0
                 : dim ? (settings.brightness / 4 > 10 ? settings.brightness / 4 : 10)
                       : settings.brightness;
}

void think(float dt) {
  EmotionEvent ev;
  while (xQueueReceive(g_events, &ev, 0) == pdTRUE)
    if (ev.type < EV_COUNT) appraisal.hear(ev, emo.emotion(), idle.sleepiness(now), now);
  appraisal.update(dt);
  {
    static bool wasTouching = false;
    if (wasTouching && !touchingNow) appraisal.touched(touch.gazeX, touch.gazeY, now);
    wasTouching = touchingNow;
    float gx, gy;
    if (!touchingNow && appraisal.glance(now, gx, gy)) idle.lookAt(gx, gy, now, 700);
  }
  while (appraisal.pop(now, ev)) {
    const bool wasAsleep = idle.state(now) == IdleGen::ASLEEP;
    EmotionEvent weighted = ev;
    weighted.intensity *= settings.ev_w[ev.type];
    emo.apply(weighted);
    // The voice: what it felt, in the mood it is now in (only sometimes, never asleep)
    static VoiceGate voiceGate;
    if (voiceGate.shouldSay(ev.type, weighted.intensity, now, idle.state(now) == IdleGen::ASLEEP, (esp_random() >> 8) / 16777216.f))
      audio::say(ev.type, emo.emotion(), fminf(1.f, weighted.intensity));
    const float reward = eventReward(ev);
    mindNet.reward(reward);
    if (ev.type == EV_WIN) tastes.won(ev.intensity, apps::active(), now);
    if (ev.type == EV_FAIL) tastes.lost(ev.intensity, apps::active(), now);
    if (reward != 0) {
      struct tm tm;
      const bool timed = getLocalTime(&tm, 0);
      tastes.react(reward, now, timed ? tm.tm_wday : -1, timed ? tm.tm_hour : 0);
      wardrobe.reward(reward);
      mouthWardrobe.reward(reward);
    }
    if (reward != 0) mindDirty++;
    lastEventMs = now;
    diary.event(ev.type);
    temper.react(ev.type, ev.intensity, settings.drift);
    switch (ev.type) {
      case EV_PET: needs.fed(0.35f * ev.intensity); break;
      case EV_PRAISE:
      case EV_LOVE: needs.fed(0.45f * ev.intensity); break;
      case EV_GREET:
        needs.fed(0.3f * ev.intensity);
        needs.played(0.1f);
        break;
      case EV_POKE:
        needs.fed(0.08f * ev.intensity);
        needs.played(0.08f * ev.intensity);
        break;
      case EV_WIN: needs.played(0.25f * ev.intensity); break;
      default: break;
    }
    if (ev.type == EV_PET) petRate += 1;
    if (ev.type == EV_POKE || ev.type == EV_ANNOY) pokeRate += 1;
    idle.poke(now);
    manual = false;
    setBase(mapEmotion(emo.emotion()));
    Serial.printf("event: %s %.2f -> ", EVENTS[ev.type].name, ev.intensity);
    printPad(emo);
    uint8_t mapped = custom.evMap[ev.type];
    uint8_t e = !settings.auto_emotes || wasAsleep || emoteActive() || now - lastAutoEmote <= 6000
                    ? (uint8_t)EM_NONE
                : mapped == EMOTE_AUTO ? appraisal.emoteFor(ev.type, emo.emotion())
                                       : mapped;
    if (e != EM_NONE) {
      playEmote(e);
      tastes.emote.chose(e, now);
      lastAutoEmote = now;
    }
  }
  tastes.update(dt);
  gamesStep();
  static bool sleepBubble = false;
  static uint32_t bubbleGap = 2000;
  if (idle.state(now) == IdleGen::ASLEEP) {
    if (settings.auto_emotes && !emoteActive() && now - emoteEnd > bubbleGap) {
      playEmote(EM_BUBBLE);
      sleepBubble = true;
      bubbleGap = 1000 + esp_random() % 7000;
    }
  } else if (sleepBubble) {
    sleepBubble = false;
    if (emoteActive() && emote == EM_BUBBLE) emoteEnd = now;
  }
  emo.update(dt);
  temper.update(dt);
  needs.update(dt, idle.state(now) == IdleGen::AWAKE, idle.state(now) == IdleGen::ASLEEP, touchingNow,
               apps::active());
  if (!manual) base = mapEmotion(emo.emotion());
  idle.setArousal(emo.emotion().a);
  idle.setValence(emo.emotion().v);
  float rateDecay = expf(-dt * 0.6931f / 300.f);
  petRate *= rateDecay;
  pokeRate *= rateDecay;
  if (dayClock.known()) {
    static float lastHour = -1;
    static uint32_t lastSample = 0, lastDiarySave = 0;
    const float h = dayClock.hour(now);
    if (lastHour >= 0 && h < lastHour - 12) {
      diary.newDay(diary.today() + 1);
      sdLogDiaryDay(diary.day(1));
    }
    lastHour = h;
    if (now - lastSample > 30000) {
      lastSample = now;
      diary.mood(emo.emotion().v, h);
      const Diary::Feel day = diary.feel();
      idle.setDay(day.warmth, day.lonely);
    }
    if (diary.dirty() && now - lastDiarySave > 900000) {
      blobWrite("diary", &diary.saved(), sizeof(Diary::Saved));
      diary.clearDirty();
      lastDiarySave = now;
    }
  }
  static uint32_t lastBackup = 0;
  if (sdcard::mounted() && now - lastBackup > 6UL * 3600000UL) {
    if (lastBackup) sdBackup();
    lastBackup = now;
  }
  static uint32_t lastNeedsSave = 0;
  if (now - lastNeedsSave > 600000) {
    const Needs::Saved ns = needs.save();
    store.putBytes("needs", &ns, sizeof ns);
    const Temper::Saved ts = temper.save();
    store.putBytes("temper", &ts, sizeof ts);
    store.putFloat("trust", appraisal.trust());
    if (Tastes::Saved* tast = new Tastes::Saved(tastes.save())) {
      blobWrite("tastes", tast, sizeof *tast);
      delete tast;
    }
    static_assert(STYLE_PRESET_COUNT + 1 <= Wardrobe::MAX,
                  "more presets than the wardrobe holds: raise Wardrobe::MAX");
    const Wardrobe::Saved ws = wardrobe.save(STYLE_PRESET_COUNT + 1);
    store.putBytes("wardrobe", &ws, sizeof ws);
    const Wardrobe::Saved ms = mouthWardrobe.save(MOUTH_STYLE_COUNT);
    store.putBytes("mouthwr", &ms, sizeof ms);
    store.putBytes("home", &homeStyle, sizeof homeStyle);
    lastNeedsSave = now;
  }
  if (mindDirty && (mindDirty >= 5 || now - lastMindSave > 600000)) {  // spare the flash
    if (!blobWrite("mind", &mindNet.weights(), sizeof(MindNet::Weights)))
      Serial.println("mind: could not save to flash");
    mindDirty = 0;
    lastMindSave = now;
  }
  if (logPad && now - lastLog >= 1000) {
    printPad(emo);
    lastLog = now;
  }
}

void reactMotion() {
  static bool rotatedByMotion = false;
  imu::enable(settings.motion);
  imu::sensitivity(settings.motion_sens);
  MotionOut m;
  static int rockRun = 0;
  static uint32_t lastRock = 0;
  while (imu::popEvent(m)) {
    if (m.event != ME_ROCK) idle.poke(now);  // rocking soothes: it must not keep it awake
    if (m.event != ME_ROCK) screenIdle.wake(now);  // picking it up, shaking it, a fall: the screen comes on
    const bool emotes = settings.auto_emotes && !emoteActive() && idle.state(now) != IdleGen::ASLEEP;
    switch (m.event) {
      case ME_KNOCK: pushEvent(EV_STARTLE, 0.3f + 0.5f * m.intensity); break;
      case ME_SHAKE:
        // Shaken: dizzy more often the harder it was, less often once it is used to it; otherwise the event picks its own emote, or none.
        pushEvent(m.intensity > 0.5f ? EV_ANNOY : EV_CONFUSE, 0.35f + 0.3f * m.intensity);
        if (emotes && chance((0.3f + 0.6f * m.intensity) / (1.f + 0.5f * appraisal.habit(EV_CONFUSE) + 0.5f * appraisal.habit(EV_ANNOY))))
          playEmote(EM_DIZZY);
        break;
      case ME_PICKUP: pushEvent(EV_STARTLE, 0.3f); break;
      case ME_PUTDOWN: pushEvent(EV_GREET, 0.2f); break;
      case ME_FALL:
        pushEvent(EV_STARTLE, 1.f);
        if (emotes) playEmote(EM_EXCLAIM);
        break;
      case ME_FACEDOWN:
        pushEvent(EV_TIRED, 0.6f);
        idle.sleepNow(now);
        break;
      case ME_FACEUP: pushEvent(EV_GREET, 0.4f); break;
      case ME_UPSIDEDOWN:
        pushEvent(EV_CONFUSE, 0.5f);  // (the event picks the emote: thinking, a question, a sweat drop, not the same twice running)
        if (settings.auto_rotate && !rotatedByMotion) {
          g_madctlReq = g_madctl == LCD_MADCTL ? LCD_MADCTL_FLIPPED : LCD_MADCTL;
          rotatedByMotion = true;
        }
        break;
      case ME_ROCK:  // soothed, then sleepy, then it dozes off
        rockRun = now - lastRock < 4000 ? rockRun + 1 : 1;
        lastRock = now;
        if (idle.state(now) != IdleGen::AWAKE) break;
        {
          // Rocking soothes. How soon it gets drowsy and drops off is not a count: it depends on how long it has been rocked, how calm
          // it already is, and chance. Wound up, it can be rocked for a long time; calm, two or three swings may do.
          const float a = emo.emotion().a, calm = a > 0.5f ? 0.f : a < -0.5f ? 1.f : 0.5f - a;
          const float drowse = 0.12f * (rockRun - 1) + 0.35f * calm;
          if (rockRun >= 3 && chance(drowse - 0.25f))
            idle.sleepNow(now);
          else if (chance(drowse))
            pushEvent(EV_TIRED, 0.3f + 0.3f * calm);
          else
            pushEvent(EV_PET, 0.25f + 0.15f * calm);
        }
        break;
      case ME_SPIN:
        pushEvent(EV_CONFUSE, 0.5f + 0.4f * m.intensity);
        // dizzy even over another emote (the "!" of being picked up is usually still showing)
        if (settings.auto_emotes && idle.state(now) != IdleGen::ASLEEP &&
            chance((0.45f + 0.5f * m.intensity) / (1.f + 0.4f * appraisal.habit(EV_CONFUSE))))
          playEmote(EM_DIZZY);
        break;
      case ME_UPRIGHT:
        if (rotatedByMotion) {
          g_madctlReq = g_madctl == LCD_MADCTL ? LCD_MADCTL_FLIPPED : LCD_MADCTL;
          rotatedByMotion = false;
        }
        break;
    }
    Serial.printf("motion: %s %.2f\n", MOTION_EVENT_NAMES[m.event], m.intensity);
  }
  imu::State s = imu::state();
  if (s.live && s.orientation == OR_UPRIGHT && !s.carried &&
      (fabsf(s.tiltX) > 0.12f || fabsf(s.tiltY) > 0.12f))
    idle.lookAt(s.tiltX, s.tiltY, now, 400);
  // Lying or held flat: the eyes roll downhill, like marbles.
  if (s.live && s.orientation == OR_FACEUP && (fabsf(s.flatX) > 0.15f || fabsf(s.flatY) > 0.15f))
    idle.lookAt(s.flatX, s.flatY, now, 400);
  // Standing and turned round: the face stays level (a few degrees of slack, so it does not shiver).
  const float r = s.live ? s.roll : 0;
  g_roll = netPinShowing() ? 0.f : settings.level * (fabsf(r) < 4.f ? 0.f : r - copysignf(4.f, r)) + headRoll;
  // The parallax: the tilt (the change from rest) moves the eyes, and half as far the mouth, so they look nearer.
  const float px = settings.parallax * 12.f;
  const bool tilted = s.live && !s.carried;
  g_parEX = tilted ? lroundf(s.tiltX * px) : 0;
  g_parEY = tilted ? lroundf(s.tiltY * px) : 0;
  g_parMX = g_parEX / 2;
  g_parMY = g_parEY / 2;
}

// Sound (boards with an audio codec): a sudden loud sound startles it and, if strong, wakes it and the screen; voice that
// keeps going means someone is near (the mind net's presence) and keeps it awake; talking after a long quiet is a greeting.
void reactSound() {
  audio::enable(settings.hearing, settings.sound);
  audio::sensitivity(settings.hearing_sens);
  audio::volume(settings.volume);
  HearingOut h;
  while (audio::popEvent(h)) {
    const bool awake = idle.state(now) == IdleGen::AWAKE;
    if (h.loud) {
      if (!awake && h.loudK < 0.5f) continue;  // a light sleeper, not a jumpy one
      screenIdle.wake(now);
      if (!awake) idle.wakeUp(now);
      idle.poke(now);
      pushEvent(EV_STARTLE, 0.3f + 0.6f * h.loudK);
      if (h.loudK > 0.8f && settings.auto_emotes && !emoteActive()) playEmote(EM_EXCLAIM);
      Serial.printf("sound: loud %.2f\n", h.loudK);
    }
    if (h.voiceStart && awake) {
      pushEvent(EV_GREET, 0.5f);
      Serial.println("sound: someone is talking");
    }
  }
  static uint32_t lastPoke = 0;
  if (audio::talking()) {
    lastHeard = now;
    if (idle.state(now) == IdleGen::AWAKE && now - lastPoke > 3000) idle.poke(now), lastPoke = now;
  }
}

void tickTimers() {
  if (apps::exitRequested()) apps::stop();
  if (now > 5000) apps::bgStep(now);
  {
    char said[8];
    if (apps::takeFaceSay(said, sizeof said)) playEmoteText(EM_QUESTION, said);
  }
  if (apps::takeFaceWake()) {
    screenIdle.activity(now);
    idle.poke(now);
  }
  if (holdUntil && now < holdUntil)
    emo.set(holdPad);
  else
    holdUntil = 0;
  if (manual && manualUntil && now >= manualUntil) {
    manual = false;
    manualUntil = 0;
  }
  if (pomoEnd && now >= pomoEnd) {
    pomoEnd = 0;
    pomoDoneUntil = now + 10000;
    pushEvent(EV_WIN, 0.8f);
    Serial.println("pomodoro: done");
  }
  if (now - lastState1s >= 1000) {
    lastState1s = now;
    int h, mi;
    if (netLocalTime(h, mi)) dayClock.set(h, mi, now);
    char buf[256];
    stateJson(buf, sizeof buf);
    netBroadcast(buf);
  }
}

// Feature panels for core 1.
void publishWidgets() {
  Widgets w{};
  if (dayClock.known()) {
    float hr = dayClock.hour(now);
    unsigned h = (unsigned)hr % 24u, mi = (unsigned)(hr * 60) % 60u;
    if (settings.clock_24h)
      snprintf(w.clock, sizeof w.clock, "%02u:%02u", h, mi);
    else
      snprintf(w.clock, sizeof w.clock, "%u:%02u%c", h % 12 ? h % 12 : 12, mi, h < 12 ? 'a' : 'p');
  }
  w.pomoActive = pomoEnd != 0;
  if (pomoEnd) {
    uint32_t left = (pomoEnd - now + 999) / 1000;
    snprintf(w.leftSub, sizeof w.leftSub, "POMO %lu:%02lu", (unsigned long)(left / 60),
             (unsigned long)(left % 60));
  } else if (portalOtaProgress() >= 0) {
    snprintf(w.leftSub, sizeof w.leftSub, "update %d%%", portalOtaProgress());
  } else if (now < pomoDoneUntil) {
    snprintf(w.leftSub, sizeof w.leftSub, "POMO done!");
  } else {
    snprintf(w.leftSub, sizeof w.leftSub, "%s", netStatus());
  }
  if (hasTemp) {
    bool f = settings.temp_unit == 1;
    snprintf(w.temp, sizeof w.temp, "%d%c", (int)lroundf(f ? tempC * 1.8f + 32 : tempC), f ? 'F' : 'C');
  }
  w.panels = settings.panels;
#if BOARD_HAS_BATTERY
  {
    // The battery, read every 10 s (a few I2C bytes).
    static uint32_t readAt = 0;
    static battery::State bat;
    if (!readAt || now - readAt >= 10000) {
      bat = battery::read();
      readAt = now ? now : 1;
    }
    if (bat.known) w.battery = bat.percent + 1, w.charging = bat.charging;
  }
#endif
  g_darkMode = settings.dark_mode;
  memcpy(w.weather, weather, sizeof w.weather);
  w.ci = ci;
  memcpy(w.ciName, ciName, sizeof w.ciName);
  w.setup = netSetupMode();
  if (netPinShowing()) snprintf(w.pin, sizeof w.pin, "%s", netPin());
  if (w.setup) {
    snprintf(w.apSsid, sizeof w.apSsid, "%s", netApSsid());
    snprintf(w.apPass, sizeof w.apPass, "%s", netApPass());
  }
  setWidgets(w);
}
}  // namespace brain

// Core 0 (50 Hz): stimuli + Serial + WebSocket -> event queue -> emotion engine -> mapper -> base face;
// idle generator overlays it -> shared target face. Also drives the feature panels.
void brainTask(void*) {
  using namespace brain;
  now = lastLog = lastUpdate = lastState1s = millis();
  idle.begin(esp_random(), now);
  appraisal.begin(esp_random());
  headLife.begin(esp_random());
  apps::
      begin();  // mounts the flash filesystem (and the SD card) first: the mind net and diary are files there
  loadPersisted();
  appraisal.setTrust(store.getFloat("trust", 0));
  emo.begin(currentPersonality());
  applySettings();
  base = mapEmotion(emo.emotion());
  static uint32_t wakeAt = 0;
  if (settings.wake_up) {
    idle.fallAsleepNow(now);
    wakeAt = 1;
  } else {
    idle.poke(now);
  }
  touchPanel.begin();
  netBegin(onNetMessage);
  printHelp();
  if (wakeAt) wakeAt = millis() + 1100;
  for (;;) {
    netBrainLock();  // the web task changes the robot only while this is not held (net.h)
    now = millis();
    const float dt = (now - lastUpdate) * 1e-3f;
    lastUpdate = now;
    readSerial();
    senseStimuli(dt);
    senseButton();
#if BOARD_HAS_BUTTONS
    senseButtons();
#endif
    reactMotion();
    reactSound();
    if (wakeAt && now >= wakeAt) {
      wakeAt = 0;
      if (idle.state(now) != IdleGen::AWAKE) idle.wakeUp(now);
    }
    think(dt);
    mindStep();
    tickTimers();
    publishWidgets();
    if (emoteActive()) {
      static uint8_t shown = EM_NONE;
      FaceParams face = emoteFaceAt((now - emoteStart) / (float)emoteMs);
      if (shown != emote || now - emoteStart < 25) idle.exprChanged(base, face, now);
      shown = emote;
      setTarget(idle.apply(face, now));
    } else {
      setTarget(idle.apply(base, now));
    }
    int st = idle.state(now);
    if (st != lastState) {
      Serial.printf("idle: %s\n", STATE_NAMES[st]);
      lastState = st;
    }
    netBrainUnlock();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
