#include "brain_internal.h"
#include "core/behaviour.h"
#include "app_runtime.h"

namespace brain {
int stateJson(char* buf, size_t n) {
  const PAD &e = emo.emotion(), &m = emo.mood();
  return snprintf(buf, n,
                  "{\"state\":{\"v\":%.2f,\"a\":%.2f,\"d\":%.2f,\"mood\":{\"v\":%.2f,\"a\":%.2f,\"d\":%.2f},"
                  "\"face\":\"%s\",\"idle\":\"%s\",\"manual\":%s,\"pomodoro\":%lu,\"suggest\":\"%s\"}}",
                  e.v, e.a, e.d, m.v, m.a, m.d, ANCHOR_NAMES[dominantAnchor()], STATE_NAMES[idle.state(now)],
                  manual ? "true" : "false", (unsigned long)(pomoEnd ? (pomoEnd - now) / 1000 : 0),
                  suggestedApp);
}

// Replies with the JSON that fill(buf, n) writes into a heap buffer of `size` bytes (big replies do not fit the brain task's stack).
template <typename Fill>
static void replyBuilt(uint8_t client, size_t size, Fill fill) {
  char* buf = (char*)malloc(size);
  if (!buf) {
    apps::bgYield();
    buf = (char*)malloc(size);
  }
  if (!buf) return netReply(client, "{\"error\":\"out of memory\"}");
  fill(buf, size);
  netReply(client, buf);
  free(buf);
}

static void handleStudioMessage(JsonView m, bool& ok) {
  char s[24];
  JsonView fe = m.get("face");
  if (fe.isObject()) {
    char from[16];
    if (fe.get("from").str(from, sizeof from)) ok = showFace(from, 0) || ok;
    const char* it = nullptr;
    char key[16];
    JsonView v;
    while (fe.next(it, key, sizeof key, v))
      if (v.isNumber()) ok = setFaceField(key, v.num(0)) || ok;
  }
  JsonView fs = m.get("face_save");
  if (fs.isObject()) {
    CustomFace f{};
    if (fs.get("name").str(f.name, sizeof f.name)) {
      f.p = base;
      JsonView pad = fs.get("pad");
      f.pad = {pad.at(0).num(0, -1, 1), pad.at(1).num(0, -1, 1), pad.at(2).num(0, -1, 1)};
      f.mood = fs.get("mood").num(0) != 0;
      if (saveFace(custom, f)) {
        saveCustom();
        ok = true;
      }
    }
  }
  if (m.get("face_del").str(s, sizeof s) && deleteFace(custom, s)) {
    saveCustom();
    ok = true;
  }
  JsonView es = m.get("emote_save");
  if (es.isObject()) {
    char spec[160], v[16], name[12] = "";
    es.get("name").str(name, sizeof name);
    int k = snprintf(spec, sizeof spec, "%s", name);
    for (const char* key : {"face", "eyes", "deco", "text"})
      if (es.get(key).str(v, sizeof v) && v[0]) k += snprintf(spec + k, sizeof spec - k, " %s=%s", key, v);
    if (es.get("ms").isNumber())
      k += snprintf(spec + k, sizeof spec - k, " ms=%d", (int)es.get("ms").num(2500, 0, 65535));
    if (es.get("wobble").isNumber())
      snprintf(spec + k, sizeof spec - k, " wobble=%.2f", es.get("wobble").num(0.3f, 0, 1));
    CustomEmote e;
    if (parseEmote(spec, e) && saveEmote(custom, e)) {
      saveCustom();
      ok = true;
    }
  }
  if (m.get("emote_del").str(s, sizeof s) && deleteEmote(custom, s)) {
    saveCustom();
    ok = true;
  }
  JsonView mp = m.get("map");
  if (mp.isObject()) {
    const char* it = nullptr;
    char key[16], v[16];
    JsonView val;
    while (mp.next(it, key, sizeof key, val)) {
      int ev = eventIndex(key), e = val.str(v, sizeof v) ? emoteId(custom, v) : -1;
      if (ev >= 0 && e >= 0) {
        custom.evMap[ev] = (uint8_t)e;
        ok = true;
      }
    }
    saveCustom();
  }
}

// WebSocket protocol: {"event":"pet","intensity":0.5} | {"emotion":{"v","a","d"},"ttl":s} |
// {"show":"happy","ttl":s} | {"show":{"pomodoro":min,"ci":"fail"|{"status","name"},"weather":{"temp","text"}|"off"}} |
// {"style":"sweet"|{"hue":200,"bg":[r,g,b],...}} | {"get":"state"}. Device broadcasts {"state":..} every second.
void onNetMessage(const char* json, size_t len, uint8_t client) {
  JsonView m;
  if (!jsonParse(json, len, m) || !m.isObject()) {
    netReply(client, "{\"error\":\"bad json\"}");
    return;
  }
  idle.poke(now);
  bool ok = false;
  char s[24], get[16] = "";
  m.get("get").str(get, sizeof get);
  if (!get[0]) screenIdle.activity(now);
  if (m.get("event").str(s, sizeof s)) {
    int ev = eventIndex(s);
    if (ev >= 0) {
      pushEvent(ev, m.get("intensity").num(1.f));
      ok = true;
    }
  }
  JsonView e = m.get("emotion");
  if (e.isObject()) {
    PAD p{e.get("v").num(0), e.get("a").num(0), e.get("d").num(0)};
    float ttl = m.get("ttl").num(0, 0, 86400);
    manual = false;
    emo.set(p);
    holdPad = emo.emotion();
    holdUntil = ttl > 0 ? now + (uint32_t)(ttl * 1000) : 0;
    setBase(mapEmotion(emo.emotion()));
    ok = true;
  }
  JsonView st = m.get("style");
  if (st.str(s, sizeof s)) {
    ok = applyStyleText(s);
  } else if (st.isObject()) {
    const char* it = nullptr;
    char key[16], val[24];
    JsonView v;
    while (st.next(it, key, sizeof key, v)) {
      if (v.isArray())
        snprintf(val, sizeof val, "%d %d %d", (int)v.at(0).num(0, 0, 255), (int)v.at(1).num(0, 0, 255),
                 (int)v.at(2).num(0, 0, 255));
      else if (!v.str(val, sizeof val))
        snprintf(val, sizeof val, "%g", v.num(0));
      ok = styleSet(style, key, val) || ok;
    }
    saveStyle();
  }
  JsonView show = m.get("show");
  if (show.str(s, sizeof s)) {
    ok = showFace(s, (uint32_t)(m.get("ttl").num(5.f, 0, 86400) * 1000));
  } else if (show.isObject()) {
    JsonView pomo = show.get("pomodoro");
    if (pomo.isNumber()) {
      startPomodoro(pomo.num(0, 0, 1440));
      ok = true;
    }
    JsonView c = show.get("ci");
    char name[14] = "";
    bool named = c.isObject() && c.get("name").str(name, sizeof name);
    if ((c.isObject() ? c.get("status") : c).str(s, sizeof s)) {
      int cs = ciFromString(s);
      if (cs >= 0) {
        setCi(cs, named ? name : nullptr);
        ok = true;
      }
    }
    JsonView w = show.get("weather");
    if (w.isObject()) {
      char text[14] = "";
      w.get("text").str(text, sizeof text);
      setWeather(w.get("temp").num(0), text);
      ok = true;
    } else if (w.eq("off")) {
      hasTemp = false;
      weather[0] = 0;
      ok = true;
    }
  }
  if (m.get("app").str(s, sizeof s)) {
    if (!strcmp(s, "stop")) {
      apps::stop();
      ok = true;
    } else {
      ok = apps::start(s);
    }
  }
  if (m.get("app_del").str(s, sizeof s)) ok = apps::remove(s) || ok;
  if (m.get("background").str(s, sizeof s)) ok = apps::bgSet(s) || ok;
  if (!strcmp(get, "apps"))
    return replyBuilt(client, 1100, [](char* p, size_t n) {
      size_t k = snprintf(p, n, "{\"apps\":[");
      for (int i = 0; i < apps::count() && k < n - 60; i++)
        k += snprintf(p + k, n - k, "%s\"%s\"", i ? "," : "", apps::name(i));
      k += snprintf(p + k, n - k, "],\"sd\":[");
      bool first = true;
      for (int i = 0; i < apps::count() && k < n - 60; i++)
        if (apps::onSd(i))
          k += snprintf(p + k, n - k, "%s\"%s\"", first ? "" : ",", apps::name(i)), first = false;
      snprintf(
          p + k, n - k,
          "],\"running\":\"%s\",\"free\":%u,\"total\":%u,\"sdok\":%s,\"sdfree\":%u,\"bg\":\"%s\",\"bgerr\":\"%s\"}",
          apps::active() ? apps::current() : "", (unsigned)apps::freeBytes(), (unsigned)apps::totalBytes(),
          sdcard::mounted() ? "true" : "false",
          (unsigned)((sdcard::totalBytes() - sdcard::usedBytes()) / 1048576), apps::bgName(),
          apps::bgError());
    });
  if (!strcmp(get, "brain"))
    return replyBuilt(client, 3600, [](char* p, size_t n) { mindJson(mindNet, p, n); });
  if (!strcmp(get, "diary")) return replyBuilt(client, 2600, [](char* p, size_t n) { diary.json(p, n); });
  if (!strcmp(get, "mood"))
    return replyBuilt(client, 1400, [](char* p, size_t n) {
      size_t k = snprintf(
          p, n,
          "{\"mood\":{\"temper\":{\"v\":%.3f,\"a\":%.3f,\"drift\":%.1f},\"needs\":{\"attention\":%.2f,\"rest\":%.2f,\"play\":%.2f},\"wardrobe\":{\"on\":%s,\"look\":\"%s\",\"mouth\":\"%s\",\"scores\":[",
          temper.valence, temper.arousal, settings.drift, needs.attention, needs.rest, needs.play,
          settings.wardrobe ? "true" : "false",
          wardrobe.current == 0 ? "home" : STYLE_PRESETS[wardrobe.current - 1].name,
          MOUTH_STYLES[style.mouth_style]);
      bool first = true;
      for (int i = 1; i <= STYLE_PRESET_COUNT && k + 40 < n; i++)
        if (fabsf(wardrobe.score[i]) >= 0.05f)
          k += snprintf(p + k, n - k, "%s[\"%s\",%.2f]", first ? "" : ",", STYLE_PRESETS[i - 1].name,
                        wardrobe.score[i]),
              first = false;
      k += snprintf(p + k, n - k, "],\"mouths\":[");
      first = true;
      for (int i = 1; i <= MOUTH_CHOICES && k + 40 < n; i++)
        if (fabsf(mouthWardrobe.score[i]) >= 0.05f)
          k += snprintf(p + k, n - k, "%s[\"%s\",%.2f]", first ? "" : ",", MOUTH_STYLES[mouthChoiceStyle(i)],
                        mouthWardrobe.score[i]),
              first = false;
      snprintf(p + k, n - k, "]}}}");
    });
  if (!strcmp(get, "tastes"))
    return replyBuilt(client, 260, [](char* p, size_t n) {
      size_t k = snprintf(p, n, "{\"tastes\":\"");
      k += tastes.summary(p + k, n - k - 3);
      snprintf(p + k, n - k, "\"}");
    });
  if (!strcmp(get, "behaviour"))
    return replyBuilt(client, 4600, [](char* p, size_t n) {
      Scratch text(4096);
      size_t k = snprintf(p, n, "{\"behaviour\":\"");
      if (text) {
        behaviourExport(BEHAVIOUR, (char*)text.p, text.n);
        for (const char* c = (const char*)text.p; *c && k + 4 < n; c++) {
          if (*c == '\n')
            p[k++] = '\\', p[k++] = 'n';
          else if (*c == '"' || *c == '\\')
            p[k++] = '\\', p[k++] = *c;
          else
            p[k++] = *c;
        }
      }
      snprintf(p + k, n - k, "\"}");
    });
  if (m.get("behaviour_reload").isTrue()) {
    char why[64], reply[100];
    const bool loaded = loadBehaviour(why);
    snprintf(reply, sizeof reply, loaded && !why[0] ? "{\"ok\":true}" : "{\"error\":\"%s\"}", why);
    netReply(client, reply);
    return;
  }
  if (!strcmp(get, "diarylog")) return replyBuilt(client, 1600, [](char* p, size_t n) { sdDiaryLog(p, n); });
  if (!strcmp(get, "store")) return replyBuilt(client, 700, [](char* p, size_t n) { sdStoreJson(p, n); });
  if (m.get("store_install").str(s, sizeof s)) ok = sdStoreInstall(s) || ok;
  if (m.get("shot").isTrue()) g_shotWanted = true, ok = sdcard::mounted();
  if (m.get("sd_update").isTrue()) {
    const char* why = sdUpdate();
    char reply[80];
    snprintf(reply, sizeof reply, "{\"error\":\"%s\"}", why ? why : "");
    netReply(client, reply);
    return;
  }
  if (!strcmp(get, "looks")) return replyBuilt(client, 700, [](char* p, size_t n) { sdLooksJson(p, n); });
  if (m.get("look_save").str(s, sizeof s)) ok = sdLookSave(s) || ok;
  if (m.get("look_load").str(s, sizeof s)) ok = sdLookLoad(s) || ok;
  if (m.get("look_del").str(s, sizeof s)) ok = sdLookDelete(s) || ok;
  if (!strcmp(get, "style"))
    return replyBuilt(client, 4800, [](char* p, size_t n) {
      const size_t k = styleJson(style, p, n, true);
      const char* now = stylePresetNow(style);
      if (now && k > 1 && k + strlen(now) + 16 < n) snprintf(p + k - 1, n - k + 1, ",\"preset\":\"%s\"}", now);
    });
  if (m.get("emote").str(s, sizeof s)) {
    int e = emoteId(custom, s);
    if (validEmote(e)) {
      playEmote(e);
      ok = true;
    }
  }
  handleStudioMessage(m, ok);
  if (!strcmp(get, "custom")) return replyBuilt(client, 6144, [](char* p, size_t n) { customJson(p, n); });
  JsonView set = m.get("settings");
  if (set.isObject()) {
    const char* it = nullptr;
    char key[20], val[24];
    JsonView v;
    while (set.next(it, key, sizeof key, v)) {
      if (!v.str(val, sizeof val)) snprintf(val, sizeof val, "%g", v.num(0));
      ok = settingSet(settings, key, val) || ok;
    }
    saveSettings();
  }
  if (!strcmp(get, "settings"))
    return replyBuilt(client, 12288, [](char* p, size_t n) { settingsJson(settings, p, n, true); });
  if (!strcmp(get, "state")) {
    char buf[256];
    stateJson(buf, sizeof buf);
    netReply(client, buf);
    return;
  }
  netReply(client, ok ? "{\"ok\":true}" : "{\"error\":\"unknown message\"}");
}
}  // namespace brain
