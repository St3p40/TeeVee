#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <initializer_list>
#include "mapper.h"
#include "renderer.h"

static uint16_t fb[240][320];
void Lcd::push(int x, int y, int w, int h, const uint16_t* p) {
  for (int r = 0; r < h; r++)
    for (int c = 0; c < w; c++) fb[y + r][x + c] = p[r * w + c];
}
void Lcd::wait() {}
void Lcd::fillScreen(uint16_t c) {
  for (auto& row : fb)
    for (auto& v : row) v = (uint16_t)(c >> 8 | c << 8);
}
void Lcd::begin(uint8_t) {}
void Lcd::setMadctl(uint8_t) {}
void Lcd::setBrightness(uint8_t) {}
void Lcd::cmd(uint8_t, const uint8_t*, int) {}
void Lcd::window(int, int, int, int) {}

static uint64_t hashFrame() {
  uint64_t h = 1469598103934665603ull;
  for (auto& row : fb)
    for (uint16_t v : row) {
      h ^= v & 0xFF;
      h *= 1099511628211ull;
      h ^= v >> 8;
      h *= 1099511628211ull;
    }
  return h;
}

static FaceRenderer r;
static Widgets baseWidgets() {
  Widgets w{};
  w.panels = true;
  strcpy(w.clock, "14:05");
  strcpy(w.leftSub, "POMO 18:32");
  w.pomoActive = true;
  strcpy(w.temp, "12C");
  strcpy(w.weather, "cloudy");
  w.ci = CI_FAIL;
  strcpy(w.ciName, "main");
  return w;
}
static void savePpm(const char* name, int a, int b) {
  const char* dir = getenv("GOLDEN_PPM");
  if (!dir) return;
  char path[256];
  if (b >= 0)
    snprintf(path, sizeof path, "%s/%s_%d_%d.ppm", dir, name, a, b);
  else if (a >= 0)
    snprintf(path, sizeof path, "%s/%s_%d.ppm", dir, name, a);
  else
    snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
  FILE* f = fopen(path, "wb");
  if (!f) return;
  fprintf(f, "P6 320 240 255\n");
  for (auto& row : fb)
    for (uint16_t v : row) {
      const uint16_t c = (uint16_t)(v >> 8 | v << 8);  // the screen holds byte-swapped RGB565
      const uint8_t rgb[3] = {(uint8_t)((c >> 11) << 3), (uint8_t)(((c >> 5) & 63) << 2),
                              (uint8_t)((c & 31) << 3)};
      fwrite(rgb, 1, 3, f);
    }
  fclose(f);
}
static void report(const char* name, int a = -1, int b = -1) {
  savePpm(name, a, b);
  if (b >= 0)
    printf("%s %d %d %016llx\n", name, a, b, (unsigned long long)hashFrame());
  else if (a >= 0)
    printf("%s %d %016llx\n", name, a, (unsigned long long)hashFrame());
  else
    printf("%s %016llx\n", name, (unsigned long long)hashFrame());
}

int main() {
  Lcd lcd;
  r.begin(&lcd);
  const Widgets w = baseWidgets();

  for (int s = 0; s < STYLE_PRESET_COUNT; s++) {
    FaceStyle st;
    styleApplyPreset(st, STYLE_PRESETS[s].name);
    r.setStyle(st);
    for (int a = 0; a < A_COUNT; a++) {
      FaceParams p = ANCHORS[a];
      p.pupil_x = 0.4f;
      p.pupil_y = -0.2f;
      r.render(p, Overlay{}, 0, true);
      r.renderWidgets(w);
      report("face", s, a);
    }
  }

  const char* emoteStyles[] = {"TeeVee", "sweet", "anime", "robot"};
  for (const char* sn : emoteStyles) {
    FaceStyle st;
    styleApplyPreset(st, sn);
    r.setStyle(st);
    for (int e = 1; e < EM_COUNT; e++)
      for (float t : {0.1f, 0.45f, 0.9f}) {
        Overlay ov{EMOTE_PARTS[e].eyes, EMOTE_PARTS[e].deco, "", 0, 1000};
        r.render(emoteFace(e, t), ov, (uint32_t)(t * 1000), true);
        r.renderWidgets(w);
        report(sn, e, (int)(t * 100));
      }
  }

  // 3. custom text emote, and parts the built-ins don't combine
  {
    FaceStyle st;
    styleApplyPreset(st, "TeeVee");
    r.setStyle(st);
    Overlay a{EF_STAR, DF_TEXT, "YAY", 0, 1000};
    r.render(ANCHORS[A_HAPPY], a, 370, true);
    report("custom-star-text");
    Overlay b{EF_COIN, DF_HEARTS, "", 0, 1000};
    r.render(ANCHORS[A_EXCITED], b, 600, true);
    report("custom-coin-hearts");
  }

  {
    FaceStyle st;
    styleApplyPreset(st, "TeeVee");
    r.setStyle(st);
    r.render(ANCHORS[A_NEUTRAL], Overlay{}, 0, true);
    for (int step = 0; step <= 10; step++) {
      FaceParams p = ANCHORS[A_NEUTRAL], q = ANCHORS[A_SURPRISED];
      float* pa = (float*)&p;
      const float* pb = (const float*)&q;
      for (int i = 0; i < (int)(sizeof(FaceParams) / sizeof(float)); i++)
        pa[i] += (pb[i] - pa[i]) * step / 10.f;
      r.render(p, Overlay{}, 0);
      report("incr-morph", step);
    }
    for (int e : {EM_DEAD, EM_CRY, EM_GLITCH, EM_BUBBLE, EM_HEART}) {
      Overlay ov{EMOTE_PARTS[e].eyes, EMOTE_PARTS[e].deco, "", 0, 1000};
      r.render(emoteFace(e, 0.3f), ov, 300);
      report("incr-emote-on", e);
      r.render(emoteFace(e, 0.6f), ov, 600);
      report("incr-emote-mid", e);
      r.render(ANCHORS[A_NEUTRAL], Overlay{}, 2000);
      report("incr-emote-off", e);
    }
  }

  {
    FaceStyle st;
    styleApplyPreset(st, "TeeVee");
    r.setStyle(st);
    r.render(ANCHORS[A_NEUTRAL], Overlay{}, 0, true);
    Widgets s = w;
    s.setup = true;
    strcpy(s.apSsid, "TeeVee-8A28");
    strcpy(s.apPass, "12345678");
    r.renderWidgets(s);
    report("widgets-setup");
    Widgets p = w;
    strcpy(p.pin, "4821");
    r.renderWidgets(p);
    report("widgets-pin");
    Widgets off = w;
    off.panels = false;
    r.renderWidgets(off);
    report("widgets-off");
    Widgets bare{};
    bare.panels = true;
    r.renderWidgets(bare);
    report("widgets-bare");
    Widgets upd = w;
    strcpy(upd.leftSub, "update 45%");
    upd.pomoActive = false;
    r.renderWidgets(upd);
    report("widgets-update");
  }

  {
    FaceStyle st = STYLE_DEFAULT;
    styleApplyText(st,
                   "eye_gap 112 eye_y 80 mouth_y 196 glow 1 lashes 1 brows 1 eye outline glint 1 lid 0.4");
    r.setStyle(st);
    FaceParams p = ANCHORS[A_HAPPY];
    p.mouth_open = 0.8f;
    r.render(p, Overlay{}, 0, true);
    report("style-extreme-1");
    styleApplyText(st, "eye_gap 150 eye_y 103 mouth_y 180 mouth cat blush 0");
    r.setStyle(st);
    r.render(ANCHORS[A_SAD], Overlay{}, 0, true);
    report("style-extreme-2");
    styleApplyText(st, "mouth none");
    r.setStyle(st);
    r.render(ANCHORS[A_SURPRISED], Overlay{}, 0, true);
    report("style-no-mouth");
  }

  {
    FaceStyle st;
    styleApplyPreset(st, "TeeVee");
    r.setStyle(st);
    AppFrame f;
    auto cmd = [&](uint8_t op, int x, int y, int w, int h, uint16_t col, uint8_t a = 0,
                   const char* text = nullptr) {
      AppCmd* c = f.add();
      c->op = op;
      c->x = x;
      c->y = y;
      c->w = w;
      c->h = h;
      c->color = col;
      c->a = a;
      if (text) c->text = (uint16_t)f.addText(text, strlen(text));
    };
    cmd(OP_CLEAR, 0, 0, 0, 0, Canvas::rgb(16, 24, 32));
    cmd(OP_RRECT, 10, 10, 100, 40, Canvas::rgb(255, 80, 80), 8);
    cmd(OP_CIRCLE, 200, 60, 30, 0, Canvas::rgb(64, 255, 128));
    cmd(OP_LINE, 10, 200, 310, 150, 0xFFFF, 4);
    cmd(OP_TEXT, 160, 120, 0, 0, 0xFFFF, 2 | AL_CENTER << 4, "Hello");
    cmd(OP_TEXT, 10, 230, 0, 0, Canvas::rgb(136, 136, 255), 1, "left");
    cmd(OP_TEXT, 310, 230, 0, 0, Canvas::rgb(255, 136, 136), 1 | AL_RIGHT << 4, "right");
    r.enterApp();
    int first = r.renderApp(f);
    report("app-full", first);
    f.cmds[2].x = 215;
    int second = r.renderApp(f);
    report("app-moved-circle", second);
    int third = r.renderApp(f);
    report("app-unchanged", third);
    r.enterFace();
    r.render(ANCHORS[A_HAPPY], Overlay{}, 0, true);
    r.renderWidgets(w);
    report("app-back-to-face");
  }

  // 9. a 280 px panel (Lilka: columns 20..300 shown): the corners' text is centred in what is visible and wraps when too wide
  {
    r.setVisibleColumns(20, 300);
    r.render(ANCHORS[A_NEUTRAL], Overlay{}, 0, true);
    Widgets s = w;
    s.setup = true;
    strcpy(s.apSsid, "TeeVee-8A28");
    strcpy(s.apPass, "12345678");
    strcpy(s.leftSub, "192.168.100.123");
    s.pomoActive = false;
    r.renderWidgets(s);
    report("narrow-setup");
    Widgets p = w;
    strcpy(p.pin, "4821");
    r.renderWidgets(p);
    report("narrow-pin");
    r.renderWidgets(w);
    report("narrow-panels");
    r.setVisibleColumns(0, 320);
  }

  // 10. glitches over the whole width (a look with a coloured background and glow), flat and turned, and the
  // frame after the glitch ends, which must clear it everywhere
  {
    FaceStyle st;
    styleApplyPreset(st, "lypa");
    r.setStyle(st);
    r.setGlitchDrive(1);
    r.render(ANCHORS[A_NEUTRAL], Overlay{}, 0, true);
    for (int turned = 0; turned < 2; turned++) {
      r.setTurn(turned ? 20.f : 0.f, 0);
      int shots = 0, after = 0;
      bool was = false;
      for (uint32_t t = 20; t < 30000 && (shots < 3 || !after); t += 20) {
        r.render(ANCHORS[A_NEUTRAL], Overlay{}, 100000 * turned + t);
        const bool on = r.glitchOn();
        if (on && shots < 3 && t % 60 == 0) report("glitch", turned, shots++);
        if (was && !on && shots) {
          report("glitch-after", turned);
          after = 1;
        }
        was = on;
      }
    }
    r.setTurn(0, 0);
    r.setGlitchDrive(0);
  }

  // 11. the turned and lensed face, pixelated, with and without the RGB split (the warp works out only the pixels the
  // pixelate keeps)
  {
    FaceStyle st;
    styleApplyPreset(st, "lypa");
    for (float px : {2.f, 3.f, 6.f})
      for (float split : {0.f, 1.f, 3.f}) {
        st.pixel = px;
        st.rgb_split = split;
        r.setStyle(st);
        r.setTurn(18.f, -9.f, 4.f);
        r.setFisheye(0.6f);
        for (uint32_t t = 0; t < 2000; t += 20) r.render(ANCHORS[A_HAPPY], Overlay{}, 200000 + t);
        report("warp-pixel", (int)px, (int)split);
      }
    r.setTurn(0, 0);
    r.setFisheye(0);
  }
  return 0;
}
