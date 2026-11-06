//  ######### ####### ####### ##   ## ####### #######
//     ##     ##      ##      ##   ## ##      ##
//     ##     #####   #####   ##   ## #####   #####
//     ##     ##      ##       ## ##  ##      ##
//     ##     ####### #######   ###   ####### #######
// MADE BY ST3P40
#include <emscripten/emscripten.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <string>
#include "engine.h"
#include "behaviour.h"
#include "pack_io.h"
#include "keys.h"
#include "mapper.h"
#include "mind.h"
#include "platform_lcd.h"
#include "renderer.h"

namespace {
Lcd lcd;
FaceRenderer renderer;
Engine engine;
KeyState keys;
std::string toast;
char report[3200];
uint32_t rgba[desktop::W * desktop::H];
}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE void tv_init(uint32_t seed, uint32_t nowMs) {
  mkdir("/state", 0777);
  renderer.begin(&lcd);
  engine.begin(seed, nowMs);
}

static bool s_changed = true;
EMSCRIPTEN_KEEPALIVE int tv_frame_changed() { return s_changed ? 1 : 0; }
EMSCRIPTEN_KEEPALIVE const uint32_t* tv_frame(uint32_t nowMs) {
  engine.update(nowMs);
  renderer.setStyle(engine.style());
  renderer.setGlitchDrive(engine.glitchDrive());
  renderer.setGlitchScale(engine.glitchScale());
  renderer.setPupilScale(engine.pupilScale());
  renderer.setTurn(engine.turnX(), engine.turnY(), engine.turnRoll());
  renderer.setFisheye(engine.fisheye());
  const DayTint dt = engine.dayTint();
  renderer.setDayTint(dt.hue, dt.val);
  static bool first = true;
  s_changed = renderer.render(engine.face(), engine.overlay(), nowMs, false) > 0 || first;
  first = false;
  if (s_changed) desktop::toRgba(rgba);
  return rgba;
}

EMSCRIPTEN_KEEPALIVE void tv_pointer(int down, int x, int y) {
  float fx, fy;
  renderer.screenToFace((float)x, (float)y, fx, fy);
  engine.pointer(down != 0, (int)fx, (int)fy);
}
EMSCRIPTEN_KEEPALIVE void tv_gaze(float gx, float gy) { engine.gaze(gx, gy); }
EMSCRIPTEN_KEEPALIVE void tv_presence() { engine.notePresence(); }
EMSCRIPTEN_KEEPALIVE void tv_battery(float low) { engine.setBatteryLow(low); }
EMSCRIPTEN_KEEPALIVE void tv_watched(float v) { engine.setWatched(v); }
EMSCRIPTEN_KEEPALIVE void tv_body(float motion, float tiltX, float tiltY) {
  engine.setBody(motion, tiltX, tiltY, false);
}

EMSCRIPTEN_KEEPALIVE int tv_key(int k, int shift) {
  toast.clear();
  return applyKey(engine, keys, k, shift != 0, toast) ? 1 : 0;
}
EMSCRIPTEN_KEEPALIVE const char* tv_toast() { return toast.c_str(); }

EMSCRIPTEN_KEEPALIVE int tv_style_text(const char* options) { return engine.styleText(options) ? 1 : 0; }
EMSCRIPTEN_KEEPALIVE void tv_set_style(int preset) { engine.setStyle(preset); }
EMSCRIPTEN_KEEPALIVE void tv_set_personality(int i) { engine.setPersonality(i); }
EMSCRIPTEN_KEEPALIVE int tv_style_index() { return engine.styleIndex(); }
EMSCRIPTEN_KEEPALIVE int tv_personality() { return engine.personality(); }

EMSCRIPTEN_KEEPALIVE const char* tv_behaviour(const char* text) {
  static char err[80];
  err[0] = 0;
  if (!engine.setBehaviour(text, strlen(text), err, sizeof err) && !err[0])
    snprintf(err, sizeof err, "not a behaviour file");
  return err;
}
EMSCRIPTEN_KEEPALIVE const char* tv_behaviour_export() {
  static char text[8192];
  behaviourExport(BEHAVIOUR, text, sizeof text);
  return text;
}
EMSCRIPTEN_KEEPALIVE const char* tv_tastes() {
  static char text[200];
  engine.tastes().summary(text, sizeof text);
  return text;
}
EMSCRIPTEN_KEEPALIVE int tv_pack_export(int all) {
  return packio::exportPack(engine, "/state", "/state/out.tvpack", all != 0) ? 1 : 0;
}
EMSCRIPTEN_KEEPALIVE const char* tv_pack_import(int all) {
  static std::string report;
  packio::importPack(engine, "/state", "/state/in.tvpack", all != 0, report);
  return report.c_str();
}
EMSCRIPTEN_KEEPALIVE const char* tv_season_look() { return engine.seasonLook(); }
EMSCRIPTEN_KEEPALIVE int tv_style_count() { return STYLE_PRESET_COUNT; }
static int presetRgb(int i, bool bg) {
  if (i < 0 || i >= STYLE_PRESET_COUNT) return 0;
  FaceStyle s = STYLE_DEFAULT;
  styleApplyPreset(s, STYLE_PRESETS[i].name);
  if (bg) return s.bg[0] << 16 | s.bg[1] << 8 | s.bg[2];
  const float h = fmodf(s.hue < 0 ? s.hue + 360.f : s.hue, 360.f), c = s.val * s.sat,
              x = c * (1 - fabsf(fmodf(h / 60.f, 2.f) - 1)), m = s.val - c;
  const float rgb[6][3] = {{c, x, 0}, {x, c, 0}, {0, c, x}, {0, x, c}, {x, 0, c}, {c, 0, x}};
  const float* k = rgb[(int)(h / 60.f) % 6];
  return (int)((k[0] + m) * 255) << 16 | (int)((k[1] + m) * 255) << 8 | (int)((k[2] + m) * 255);
}
EMSCRIPTEN_KEEPALIVE int tv_preset_face(int i) { return presetRgb(i, false); }
EMSCRIPTEN_KEEPALIVE int tv_preset_bg(int i) { return presetRgb(i, true); }
EMSCRIPTEN_KEEPALIVE const char* tv_feeling() {
  float w[A_COUNT];
  anchorWeights(engine.emotion(), w);
  int best = 0;
  for (int i = 1; i < A_COUNT; i++)
    if (w[i] > w[best]) best = i;
  return ANCHOR_NAMES[best];
}
EMSCRIPTEN_KEEPALIVE float tv_need(int i) {
  return i == 0 ? engine.needs().attention : i == 1 ? engine.needs().rest : engine.needs().play;
}
EMSCRIPTEN_KEEPALIVE const char* tv_mind_state_name() { return engine.mind(); }
EMSCRIPTEN_KEEPALIVE const char* tv_style_name(int i) { return STYLE_PRESETS[i].name; }
EMSCRIPTEN_KEEPALIVE int tv_opt_count() { return STYLE_DEF_COUNT; }
EMSCRIPTEN_KEEPALIVE const char* tv_opt_key(int i) { return STYLE_DEFS[i].key; }
EMSCRIPTEN_KEEPALIVE int tv_opt_type(int i) { return STYLE_DEFS[i].type; }
EMSCRIPTEN_KEEPALIVE float tv_opt_min(int i) { return STYLE_DEFS[i].min; }
EMSCRIPTEN_KEEPALIVE float tv_opt_max(int i) { return STYLE_DEFS[i].max; }
EMSCRIPTEN_KEEPALIVE const char* tv_opt_help(int i) { return STYLE_DEFS[i].help; }
EMSCRIPTEN_KEEPALIVE const char* tv_opt_options(int i) {
  return STYLE_DEFS[i].options ? STYLE_DEFS[i].options : "";
}
EMSCRIPTEN_KEEPALIVE const char* tv_opt_value(int i) {
  static char v[48];
  styleValue(engine.style(), STYLE_DEFS[i], v, sizeof v, false);
  return v;
}
EMSCRIPTEN_KEEPALIVE void tv_random_look(int all) { engine.randomLook(all != 0); }
EMSCRIPTEN_KEEPALIVE void tv_set_look_change(float chance, float whole) {
  engine.setLookChange(chance, whole);
}
EMSCRIPTEN_KEEPALIVE float tv_look_chance() { return engine.lookChance(); }
EMSCRIPTEN_KEEPALIVE float tv_look_whole() { return engine.lookWhole(); }
EMSCRIPTEN_KEEPALIVE int tv_personality_count() { return 5; }
EMSCRIPTEN_KEEPALIVE const char* tv_personality_name(int i) { return PERSONALITY_NAMES[i]; }

EMSCRIPTEN_KEEPALIVE int tv_save() { return engine.save("/state") ? 1 : 0; }
EMSCRIPTEN_KEEPALIVE void tv_load() { engine.load("/state"); }
EMSCRIPTEN_KEEPALIVE void tv_new_robot(unsigned seed) { engine.newRobot(seed); }
EMSCRIPTEN_KEEPALIVE int tv_dirty() { return engine.dirty() ? 1 : 0; }

EMSCRIPTEN_KEEPALIVE const char* tv_look() {
  static char look[STYLE_TEXT_MAX];
  styleDescribe(engine.style(), look, sizeof look);
  return look;
}

EMSCRIPTEN_KEEPALIVE const char* tv_diary() {
  static char buf[2600];
  engine.diary().json(buf, sizeof buf);
  return buf;
}

EMSCRIPTEN_KEEPALIVE const float* tv_mind_state() {
  static float st[MindNet::IN - 1 + MindNet::N + MindNet::OUT];
  int k = 0;
  for (int i = 0; i < MindNet::IN - 1; i++) st[k++] = engine.net().input(i);
  for (int j = 0; j < MindNet::N; j++) st[k++] = engine.net().inner(j);
  for (int u = 0; u < MindNet::OUT; u++) st[k++] = engine.net().urge(u);
  return st;
}
EMSCRIPTEN_KEEPALIVE int tv_mind_inner_count() { return MindNet::N; }
EMSCRIPTEN_KEEPALIVE unsigned tv_look_updates() { return engine.lookNet().updates; }
EMSCRIPTEN_KEEPALIVE const char* tv_look_name() { return engine.lookName(); }
EMSCRIPTEN_KEEPALIVE float tv_look_score() { return engine.lookScore(); }
EMSCRIPTEN_KEEPALIVE const char* tv_look_top_name(int i) { return engine.lookTopName(i); }
EMSCRIPTEN_KEEPALIVE float tv_look_top_score(int i) { return engine.lookTopScore(i); }
EMSCRIPTEN_KEEPALIVE void tv_set_look_pinned(int on) { engine.setLookPinned(on != 0); }
EMSCRIPTEN_KEEPALIVE int tv_look_pinned() { return engine.lookPinned() ? 1 : 0; }
EMSCRIPTEN_KEEPALIVE float tv_head_turn() { return engine.headTurnDeg(); }
EMSCRIPTEN_KEEPALIVE float tv_fisheye_max() { return engine.fisheyeMax(); }
EMSCRIPTEN_KEEPALIVE float tv_temper_v() { return engine.temper().valence; }
EMSCRIPTEN_KEEPALIVE float tv_temper_a() { return engine.temper().arousal; }
EMSCRIPTEN_KEEPALIVE void tv_set_head_turn(float deg) { engine.setHeadTurn(deg); }
EMSCRIPTEN_KEEPALIVE void tv_set_fisheye(float k) { engine.setFisheye(k); }
EMSCRIPTEN_KEEPALIVE void tv_set_wardrobe(int on) { engine.setWardrobe(on != 0); }
EMSCRIPTEN_KEEPALIVE int tv_wardrobe() { return engine.wardrobe() ? 1 : 0; }

EMSCRIPTEN_KEEPALIVE const char* tv_mind_json() {
  static char buf[4608];
  mindJson(engine.net(), buf, sizeof buf);
  return buf;
}

EMSCRIPTEN_KEEPALIVE const char* tv_report() {
  mindReport(engine.net(), report, sizeof report);
  return report;
}
}
