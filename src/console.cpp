#include "brain_internal.h"
#include "core/behaviour.h"
#include "app_runtime.h"
#include "sdcard.h"
#include "imu.h"
#include "audio.h"
#include "core/motion_sim.h"

void printHelp() {
  Serial.println(
      "TeeVee. Emotion: e <event> [0..1] (pet poke praise scold startle win fail greet tired annoy love "
      "confuse bore), "
      "set <v> <a> <d>, pad = print state, v = toggle 1 Hz state log. "
      "Manual face: 1-9 or name (neutral happy sad angry surprised sleepy excited content scared bored "
      "skeptical confused smug), mix <a> <b> <t>; auto = back to emotion. "
      "Emotes: emote <name> (emote = list), emote new/del, map [<event> <emote|none|auto>]. "
      "Studio: face [<field> <value> | save <name> [v a d] | del <name>]. "
      "Motion: imu = status, imu on|off, imu sim <ax> <ay> <az> [gx gy gz], imu play <still|knock|shake|pickup|fall|facedown|upsidedown|rock|spin>. Sound: audio = status, say <event> = hear its voice. "
      "Apps: app = list, app <name> = run, app stop, app rm <name>, app info, app touch down|move|up <x> <y>, app button <up|down|left|right|a|b|c|d> [up]. "
      "Settings: cfg [<key> <value>] (personality, brightness, sleep_after, clock_24h, temp_unit, behaviours, ...). "
      "Look: style [preset] | style <option> <value> [<option> <value> ...] (style = list) "
      "| style bg <r> <g> <b>. Mind net: brain [reset], diary. SD card: sd [backup|restore], look [save|load|del <name>]. "
      "Stimuli: light <0..1>|off (simulated ambient light: the boards have no sensor), time [HH:MM], touch = toggle touch log. "
      "Net: wifi <ssid> <pass>, tz <POSIX TZ>, net = status, pin [new] = web page PIN, setup = Wi-Fi hotspot + page (or hold the "
      "screen 8 s). "
      "Features: pomo <min> (0 = stop), ci <pass|fail|running|none> [name], weather <temp> <text>|off. "
      "madctl <hex> = fix display orientation. sleep, wake, screen [on|off], s = FPS stats, b = FPS bench, ? = help");
}

int anchorIndex(const String& s) {
  if (s.length() == 1 && s[0] >= '1' && s[0] <= '9' && s[0] - '1' < A_COUNT) return s[0] - '1';
  for (int i = 0; i < A_COUNT; i++)
    if (s == ANCHOR_NAMES[i]) return i;
  return -1;
}

FaceParams blendAnchors(int a, int b, float t) {
  FaceParams out;
  const float* pa = reinterpret_cast<const float*>(&ANCHORS[a]);
  const float* pb = reinterpret_cast<const float*>(&ANCHORS[b]);
  float* po = reinterpret_cast<float*>(&out);
  for (int i = 0; i < FaceSpring::N; i++) po[i] = pa[i] + (pb[i] - pa[i]) * t;
  float dh = fmodf(pb[FaceSpring::HUE] - pa[FaceSpring::HUE] + 540.f, 360.f) - 180.f;
  out.hue = fmodf(pa[FaceSpring::HUE] + dh * t + 360.f, 360.f);
  return out;
}

void printPad(const EmotionEngine& emo) {
  const PAD &e = emo.emotion(), &m = emo.mood();
  float w[A_COUNT];
  anchorWeights(e, w);
  Serial.printf("emotion v=%+.2f a=%+.2f d=%+.2f | mood v=%+.2f a=%+.2f d=%+.2f |", e.v, e.a, e.d, m.v, m.a,
                m.d);
  for (int i = 0; i < A_COUNT; i++)
    if (w[i] >= 0.05f) Serial.printf(" %s %.2f", ANCHOR_NAMES[i], w[i]);
  Serial.println();
}

namespace brain {
void printBrain() {
  const size_t n = 2800;
  char* buf = (char*)malloc(n);
  if (!buf) return;
  mindReport(mindNet, buf, n);
  Serial.print(buf);
  Serial.printf("temperament: valence %+.3f arousal %+.3f (drift x%.1f); wardrobe %s, wearing %s\n",
                temper.valence, temper.arousal, settings.drift, settings.wardrobe ? "on" : "off",
                wardrobe.current == 0 ? "home" : STYLE_PRESETS[wardrobe.current - 1].name);
  free(buf);
}

static void handleCommand(const String& line, const String& rawLine) {
  if (line != "screen" && line != "sleep")
    idle.poke(now);  // any command counts as activity (asking about the screen must not change the answer)
  if (line != "screen" && line != "screen off") screenIdle.activity(now);
  int idx = anchorIndex(line);
  if (idx >= 0 || customFace(custom, line.c_str()) >= 0) {
    showFace(idx >= 0 ? ANCHOR_NAMES[idx] : line.c_str(), 0);
    Serial.printf("face: %s (manual, 'auto' to resume emotion)\n",
                  idx >= 0 ? ANCHOR_NAMES[idx] : line.c_str());
  } else if (line.startsWith("e ")) {
    char name[16];
    float k = 1;
    int n = sscanf(line.c_str() + 2, "%15s %f", name, &k);
    int ev = n >= 1 ? eventIndex(name) : -1;
    if (ev >= 0) {
      EmotionEvent e{(uint8_t)ev, k};
      xQueueSend(g_events, &e, 0);
    } else {
      printHelp();
    }
  } else if (line.startsWith("set ")) {
    PAD p;
    if (sscanf(line.c_str() + 4, "%f %f %f", &p.v, &p.a, &p.d) == 3) {
      manual = false;
      emo.set(p);
      setBase(mapEmotion(emo.emotion()));
      printPad(emo);
    } else {
      printHelp();
    }
  } else if (line.startsWith("light")) {
    float l;
    if (line == "light off")
      simLight = -1;
    else if (sscanf(line.c_str() + 5, "%f", &l) == 1)
      simLight = constrain(l, 0.f, 1.f);
    Serial.printf("light: %s level=%.2f dark=%d\n", simLight < 0 ? "sensor" : "simulated", light.level(),
                  light.dark);
  } else if (line.startsWith("time")) {
    int h, m;
    if (sscanf(line.c_str() + 4, "%d:%d", &h, &m) == 2 && h >= 0 && h < 24 && m >= 0 && m < 60)
      dayClock.set(h, m, now);
    if (dayClock.known()) {
      float hr = dayClock.hour(now);
      Serial.printf("time %02d:%02d energy=%+.2f night=%d\n", (int)hr, (int)(hr * 60) % 60,
                    dayClock.energy(now), dayClock.night(now));
    } else {
      Serial.println("time unknown (time HH:MM)");
    }
  } else if (line == "touch") {
    logTouch = !logTouch;
  } else if (line.startsWith("wifi ")) {
    String rest = rawLine.substring(5), ssid, pass;
    if (rest.startsWith("\"") && rest.indexOf('"', 1) > 0) {
      int q = rest.indexOf('"', 1);
      ssid = rest.substring(1, q);
      pass = rest.substring(q + 1);
    } else {
      int sp = rest.indexOf(' ');
      ssid = sp < 0 ? rest : rest.substring(0, sp);
      pass = sp < 0 ? "" : rest.substring(sp + 1);
    }
    pass.trim();
    netSetWifi(ssid.c_str(), pass.c_str());
  } else if (line.startsWith("tz ")) {
    netSetTz(rawLine.substring(3).c_str());
    Serial.printf("tz: %s\n", rawLine.substring(3).c_str());
  } else if (line == "setup" || line == "setup off") {
    netStartSetup(line == "setup");
  } else if (line.startsWith("madctl")) {
    unsigned v;
    if (sscanf(line.c_str() + 6, "%x", &v) == 1 && v < 256) {
      g_madctlReq = (int)v;
      store.putUChar("madctl", (uint8_t)v);
    }
    Serial.printf("madctl: %02x (try 28 68 a8 e8 08 48 88 c8 if the image is mirrored or rotated)\n",
                  g_madctlReq >= 0 ? g_madctlReq : g_madctl);
  } else if (line == "pin" || line == "pin new") {
    if (line == "pin new") netNewPin();
    netShowPin();
    Serial.printf("web PIN: %s%s\n", netPin(), line == "pin new" ? " (new; browsers must log in again)" : "");
  } else if (line == "net") {
    Serial.printf("net: %s\n", netStatus());
  } else if (line.startsWith("pomo")) {
    float mins = 25;
    sscanf(line.c_str() + 4, "%f", &mins);
    startPomodoro(mins);
    Serial.printf("pomodoro: %s\n", pomoEnd ? "started" : "stopped");
  } else if (line.startsWith("ci ")) {
    char st[16] = "", name[14] = "";
    sscanf(line.c_str() + 3, "%15s %13s", st, name);
    int c = ciFromString(st);
    if (c >= 0)
      setCi(c, name[0] ? name : nullptr);
    else
      printHelp();
  } else if (line.startsWith("weather ")) {
    float tc;
    char text[14] = "";
    if (line == "weather off")
      hasTemp = false, weather[0] = 0;
    else if (sscanf(line.c_str() + 8, "%f %13s", &tc, text) >= 1)
      setWeather(tc, text);
  } else if (line == "style") {
    char buf[STYLE_TEXT_MAX];
    styleDescribe(style, buf, sizeof buf);
    Serial.printf("style: %s\n", buf);
  } else if (line.startsWith("style ")) {
    if (applyStyleText(line.c_str() + 6)) {
      char buf[STYLE_TEXT_MAX];
      styleDescribe(style, buf, sizeof buf);
      Serial.printf("style: %s\n", buf);
    } else {
      Serial.print("style presets:");
      for (const auto& sp : STYLE_PRESETS) Serial.printf(" %s", sp.name);
      Serial.print(" | options:");
      for (const auto& d : STYLE_DEFS) Serial.printf(" %s", d.key);
      Serial.println();
    }
  } else if (line.startsWith("emote new ")) {
    CustomEmote e;
    if (parseEmote(line.c_str() + 10, e) && saveEmote(custom, e)) {
      saveCustom();
      Serial.printf("emote saved: %s\n", e.name);
    } else {
      Serial.printf(
          "emote new <name> face=<expression> eyes=<%s> deco=<...|text> text=<word> ms=<n> wobble=<0..1>\n",
          "none|heart|star|x|dizzy|coin");
    }
  } else if (line.startsWith("emote del ")) {
    bool ok = deleteEmote(custom, line.c_str() + 10);
    if (ok) saveCustom();
    Serial.println(ok ? "emote deleted" : "no such custom emote");
  } else if (line == "emote" || line.startsWith("emote ")) {
    int e = line.length() > 6 ? emoteId(custom, line.c_str() + 6) : -1;
    if (validEmote(e)) {
      playEmote(e);
    } else {
      Serial.print("emotes:");
      for (int i = 1; i < EM_COUNT; i++) Serial.printf(" %s", EMOTE_NAMES[i]);
      for (int i = 0; i < custom.nEmotes; i++) Serial.printf(" %s*", custom.emotes[i].name);
      Serial.println("  (* = yours; emote new / emote del to make or remove)");
    }
  } else if (line == "face" || line.startsWith("face ")) {
    char a[16] = "", b[16] = "";
    float v, pv, pa, pd;
    int n = sscanf(line.c_str(), "face %15s %15s", a, b);
    if (n >= 1 && !strcmp(a, "save") && n == 2) {
      CustomFace f{};
      snprintf(f.name, sizeof f.name, "%s", b);
      f.p = base;
      f.mood = sscanf(line.c_str(), "face save %*s %f %f %f", &pv, &pa, &pd) == 3;
      if (f.mood) f.pad = {pv, pa, pd};
      bool ok = saveFace(custom, f);
      if (ok) saveCustom();
      Serial.println(ok ? "face saved" : "can't save (name a-z0-9_-, max 11, not a built-in; max 8 faces)");
    } else if (n == 2 && !strcmp(a, "del")) {
      bool ok = deleteFace(custom, b);
      if (ok) saveCustom();
      Serial.println(ok ? "face deleted" : "no such custom face");
    } else if (n == 2 && sscanf(b, "%f", &v) == 1 && setFaceField(a, v)) {
      Serial.printf("face %s = %.2f\n", a, v);
    } else {
      Serial.print("fields:");
      for (const FaceField& f : FACE_FIELDS) Serial.printf(" %s=%.2f", f.name, faceField(base, f));
      Serial.print("\nyours:");
      for (int i = 0; i < custom.nFaces; i++)
        Serial.printf(" %s%s", custom.faces[i].name, custom.faces[i].mood ? "(mood)" : "");
      Serial.println();
    }
  } else if (line == "map" || line.startsWith("map ")) {
    char ev[16], em[16];
    if (sscanf(line.c_str(), "map %15s %15s", ev, em) == 2) {
      int i = eventIndex(ev), e = emoteId(custom, em);
      if (i >= 0 && e >= 0) {
        custom.evMap[i] = (uint8_t)e;
        saveCustom();
      } else {
        Serial.println("map <event> <emote|none|auto>");
      }
    }
    for (int i = 0; i < EV_COUNT; i++)
      Serial.printf("  %-8s -> %s\n", EVENTS[i].name, emoteName(custom, custom.evMap[i]));
  } else if (line == "audio" || line == "mic") {
    Serial.printf("audio: %s, level %.2f\n", audio::status(), audio::level());
  } else if (line.startsWith("say ")) {
    const int ev = eventIndex(line.substring(4).c_str());
    if (ev < 0)
      Serial.println("say: which event? (pet poke praise ...)");
    else
      audio::say((uint8_t)ev, emo.emotion(), 0.7f);
  } else if (line == "imu" || line.startsWith("imu ")) {
    char a[16] = "", b[16] = "";
    float v[6] = {0, 0, 1, 0, 0, 0};
    int n = sscanf(line.c_str(), "imu %15s %15s", a, b);
    if (n >= 1 && !strcmp(a, "sim")) {
      if (sscanf(line.c_str(), "imu sim %f %f %f %f %f %f", v, v + 1, v + 2, v + 3, v + 4, v + 5) >= 3) {
        imu::simulate({v[0], v[1], v[2], v[3], v[4], v[5]}, 3000);
        Serial.println("imu: simulated reading for 3 s");
      } else {
        Serial.println("imu sim <ax> <ay> <az> [gx gy gz]   (g and deg/s; standing still is 0 1 0)");
      }
    } else if (n >= 2 && !strcmp(a, "play")) {
      int sc = -1;
      for (int i = 0; i < SIM_COUNT; i++)
        if (!strcmp(b, SIM_NAMES[i])) sc = i;
      Serial.println(imu::play(sc) ? "imu: playing"
                                   : "imu play <still|knock|shake|pickup|fall|facedown|upsidedown|rock|spin>");
    } else if (n >= 1 && (!strcmp(a, "on") || !strcmp(a, "off"))) {
      settings.motion = !strcmp(a, "on");
      saveSettings();
      Serial.printf("imu: %s\n",
                    settings.motion ? "on (looking for a sensor on SDA IO27 / SCL IO22)" : "off");
    } else {
      imu::State s = imu::state();
      static const char* const OR[] = {"unknown", "upright",   "upside down",
                                       "face up", "face down", "on its side"};
      Serial.printf(
          "imu: %s, setting %s, sensor: %s, orientation %s%s, tilt %+.2f %+.2f, last %.2f %.2f %.2f g, %.0f %.0f %.0f deg/s\n",
          s.live ? "live" : "no readings", settings.motion ? "on" : "off", imu::chip(), OR[s.orientation % 6],
          s.carried ? " (carried)" : "", s.tiltX, s.tiltY, s.last.ax, s.last.ay, s.last.az, s.last.gx, s.last.gy,
          s.last.gz);
    }
  } else if (line == "app" || line.startsWith("app ")) {
    char a[16] = "", b[16] = "";
    int x = 0, y = 0;
    int n = sscanf(line.c_str(), "app %15s %15s %d %d", a, b, &x, &y);
    if (n <= 0) {
      Serial.printf("apps (%d, %u of %u bytes free):", apps::count(), (unsigned)apps::freeBytes(),
                    (unsigned)apps::totalBytes());
      for (int i = 0; i < apps::count(); i++)
        Serial.printf(" %s%s", apps::name(i),
                      apps::active() && !strcmp(apps::name(i), apps::current()) ? "*" : "");
      Serial.println(apps::count() ? "   (* = running)" : "   (none: upload one on the web page)");
    } else if (!strcmp(a, "stop")) {
      apps::stop();
      Serial.println("app: stopped");
    } else if (!strcmp(a, "bg")) {
      if (n >= 2) Serial.println(apps::bgSet(b) ? "app: background set" : "app: no such app");
      Serial.printf("background app: %s %s\n", apps::bgName()[0] ? apps::bgName() : "none", apps::bgError());
    } else if (!strcmp(a, "info")) {
      char buf[240];
      apps::debugInfo(buf, sizeof buf);
      Serial.println(buf);
    } else if (!strcmp(a, "rm") && n >= 2) {
      Serial.println(apps::remove(b) ? "app: deleted" : "app: no such app");
    } else if (!strcmp(a, "button") && n >= 2) {
      int i = 0;
      while (i < APP_BUTTON_COUNT && strcmp(b, APP_BUTTONS[i])) i++;
      if (i == APP_BUTTON_COUNT)
        Serial.println("app: buttons are up down left right a b c d");
      else if (strstr(line.c_str() + 4 + strlen(a) + 1 + strlen(b), "up"))
        apps::button(i, false);
      else
        apps::button(i, true), apps::button(i, false);
    } else if (!strcmp(a, "touch") && n >= 4) {
      apps::touch(!strcmp(b, "down") ? TOUCH_DOWN : !strcmp(b, "up") ? TOUCH_UP : TOUCH_MOVE, x, y);
    } else if (n == 1) {
      Serial.println(apps::start(a) ? "app: started" : "app: no such app (or it could not start)");
    } else {
      printHelp();
    }
  } else if (line == "cfg" || line.startsWith("cfg ")) {
    char key[20] = "", val[24] = "";
    if (sscanf(line.c_str(), "cfg %19s %23s", key, val) == 2) {
      if (settingSet(settings, key, val))
        saveSettings();
      else
        Serial.printf("cfg: bad key or value: %s %s\n", key, val);
    }
    for (const SettingDef& d : SETTING_DEFS) {
      char v[24];
      settingGet(settings, d, v, sizeof v);
      Serial.printf("  %-12s %-9s %s%s%s%s\n", d.key, v, d.help, d.options ? " [" : "",
                    d.options ? d.options : "", d.options ? "]" : "");
    }
  } else if (line == "brain") {
    printBrain();
  } else if (line == "sd ls" || line.startsWith("sd ls ")) {
    const char* path = line.length() > 6 ? line.c_str() + 6 : "/teevee";
    File dir = sdcard::mounted() ? sdcard::fs().open(path) : File();
    if (!dir || !dir.isDirectory()) {
      Serial.println("sd: no such folder (or no card)");
    } else {
      for (File f = dir.openNextFile(); f; f = dir.openNextFile())
        Serial.printf("  %s%s  %u\n", f.name(), f.isDirectory() ? "/" : "", (unsigned)f.size());
    }
  } else if (line == "screen" || line == "screen on" || line == "screen off") {
    if (line == "screen on") screenIdle.activity(now);
    if (line == "screen off") screenIdle.sleepNow();
    Serial.printf("screen: %s\n", screenIdle.off() ? "off" : "on");
  } else if (line == "heap") {
    Serial.printf("heap: %u free, biggest block for data %u, lowest %u\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                  (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
  } else if (
      line ==
      "stack") {  // how near the brain task has come to the end of its stack: its worst moment since it started
    Serial.printf("stack: %u bytes never used\n", (unsigned)uxTaskGetStackHighWaterMark(nullptr));
  } else if (line == "behaviour off") {  // the flash copy goes: the defaults, or the card's copy
    LittleFS.remove("/behaviour.txt");
    BEHAVIOUR = Behaviour();
    loadBehaviour(nullptr);
    Serial.println("behaviour: flash copy removed");
  } else if (line.startsWith("sd rm ")) {
    const String rel = line.substring(6);
    Serial.println(sdcard::mounted() && rel.indexOf("..") < 0 && !rel.startsWith("/") &&
                           sdcard::fs().remove("/teevee/" + rel)
                       ? "sd: removed"
                       : "sd: could not remove it");
  } else if (line == "behaviour reload") {
    char why[64];
    Serial.println(loadBehaviour(why) ? (why[0] ? why : "behaviour: in force") : why);
  } else if (line == "behaviour") {
    Scratch text(4096);
    if (text) {
      behaviourExport(BEHAVIOUR, (char*)text.p, text.n);
      Serial.print((const char*)text.p);
    }
  } else if (line == "tastes") {
    char text[200];
    tastes.summary(text, sizeof text);
    Serial.println(text);
  } else if (line.startsWith("sd pack ")) {
    char name[16] = "", all[8] = "", path[48], report[160];
    sscanf(line.c_str() + 8, "%15s %7s", name, all);
    snprintf(path, sizeof path, "/teevee/packs/%s.tvpack", name);
    if (!sdcard::mounted())
      Serial.println("sd: no card");
    else {
      packInstall(sdcard::fs(), path, !strcmp(all, "all"), report, sizeof report);
      Serial.printf("pack: %s\n", report);
    }
  } else if (line == "sd export" || line == "sd export all") {
    char err[60];
    if (!sdcard::mounted())
      Serial.println("sd: no card");
    else {
      if (!sdcard::fs().exists("/teevee/packs")) sdcard::fs().mkdir("/teevee/packs");
      const bool ok =
          packExport(sdcard::fs(), "/teevee/packs/export.tvpack", line.endsWith(" all"), err, sizeof err);
      Serial.println(ok ? "sd: saved /teevee/packs/export.tvpack" : err);
    }
  } else if (line == "sd shot") {
    Serial.println(sdcard::mounted() ? "sd: taking a screenshot" : "sd: no card");
    g_shotWanted = sdcard::mounted();
  } else if (line == "sd update") {
    Serial.printf("sd: %s\n", sdUpdate());
  } else if (line == "sd backup") {
    const int n = sdBackup();
    Serial.println(n < 0 ? "sd: no card" : "sd: backed up to /teevee/backup/");
    if (n >= 0) Serial.printf("  %d file(s)\n", n);
  } else if (line == "sd restore") {
    const int n = sdRestore();
    Serial.println(n < 0 ? "sd: no card" : "sd: restored from /teevee/backup/, restarting");
    if (n > 0) {
      delay(300);
      ESP.restart();
    }
  } else if (line == "look" || line.startsWith("look ")) {
    char arg[16] = "";
    if (line.length() > 5) strlcpy(arg, line.c_str() + 5, sizeof arg);
    char* sp = strchr(arg, ' ');
    if (line == "look") {
      char* buf = (char*)malloc(700);
      if (buf) {
        sdLooksJson(buf, 700);
        Serial.println(buf);
        free(buf);
      }
    } else if (!strncmp(line.c_str() + 5, "save ", 5)) {
      Serial.println(sdLookSave(line.c_str() + 10)
                         ? "look: saved"
                         : "look: could not save (name a-z 0-9 _ -, up to 11, card needed)");
    } else if (!strncmp(line.c_str() + 5, "load ", 5)) {
      Serial.println(sdLookLoad(line.c_str() + 10) ? "look: loaded" : "look: no such look");
    } else if (!strncmp(line.c_str() + 5, "del ", 4)) {
      Serial.println(sdLookDelete(line.c_str() + 9) ? "look: deleted" : "look: no such look");
    } else {
      Serial.println("look: save|load|del <name>, or `look` to list");
    }
    (void)sp;
  } else if (line == "sd") {
    const bool ok = sdcard::begin();
    if (!ok) {
      Serial.println("sd: no card (or it does not answer): check it is pushed in, FAT32 formatted");
    } else {
      Serial.printf("sd: %s card, %llu MB, %llu MB used\n", sdcard::typeName(),
                    sdcard::totalBytes() / 1048576, sdcard::usedBytes() / 1048576);
      File root = sdcard::fs().open("/");
      int n = 0;
      for (File f = root.openNextFile(); f && n < 20; f = root.openNextFile(), n++)
        Serial.printf("  %s%s  %u\n", f.name(), f.isDirectory() ? "/" : "", (unsigned)f.size());
      File t = sdcard::fs().open("/teevee_test.txt", FILE_WRITE);
      if (t) {
        t.print("teevee sd test ");
        t.println(millis());
        t.close();
        File r = sdcard::fs().open("/teevee_test.txt");
        Serial.printf("  write/read test: %s\n", r ? r.readStringUntil('\n').c_str() : "could not read back");
        if (r) r.close();
        sdcard::fs().remove("/teevee_test.txt");
      } else {
        Serial.println("  could not write a test file (read-only card?)");
      }
    }
  } else if (line == "diary") {
    char* buf = (char*)malloc(1200);
    if (buf) {
      diary.text(buf, 1200);
      Serial.print(buf);
      free(buf);
    }
  } else if (line == "brain reset") {
    mindNet.init(esp_random());
    LittleFS.remove("/state/mind.bin");
    tastes = Tastes();
    LittleFS.remove("/state/tastes.bin");
    Serial.println("mind: reset to the innate prior");
  } else if (line == "pad") {
    printPad(emo);
  } else if (line == "v") {
    logPad = !logPad;
  } else if (line == "auto") {
    manual = false;
    setBase(mapEmotion(emo.emotion()));
    Serial.println("face: auto (emotion)");
  } else if (line == "sleep") {
    idle.sleepNow(now);
  } else if (line == "wake") {
  } else if (line == "s") {
    g_stats = !g_stats;
    Serial.printf("stats %s\n", g_stats ? "on" : "off");
  } else if (line.startsWith("mix ")) {
    char a[16], b[16];
    float t;
    int ia = -1, ib = -1;
    if (sscanf(line.c_str() + 4, "%15s %15s %f", a, b, &t) == 3) {
      ia = anchorIndex(a);
      ib = anchorIndex(b);
    }
    if (ia >= 0 && ib >= 0) {
      t = constrain(t, 0.f, 1.f);
      manual = true;
      manualUntil = 0;
      setBase(blendAnchors(ia, ib, t));
      Serial.printf("face: %s->%s %.2f\n", ANCHOR_NAMES[ia], ANCHOR_NAMES[ib], t);
    } else {
      printHelp();
    }
  } else if (line == "b") {
    g_bench = !g_bench;
    Serial.printf("bench %s\n", g_bench ? "on" : "off");
  } else {
    printHelp();
  }
}

void readSerial() {
  static String line;
  while (Serial.available()) {
    char c = Serial.read();
    if (c != '\n' && c != '\r') {
      line += c;
      continue;
    }
    line.trim();
    String rawLine = line;
    line.toLowerCase();
    if (!line.isEmpty()) handleCommand(line, rawLine);
    line = "";
  }
}
}  // namespace brain
