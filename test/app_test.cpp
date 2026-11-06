#include <chrono>
#include <thread>
#include <cstdio>
#include <map>
#include <string>
#include <vector>
#include "app_vm.h"
extern "C" const char* tv_cstack_floor;
#include "emotes.h"
#include "emotion.h"

static int fails = 0;
#define CHECK(c)                                                                                             \
  do {                                                                                                       \
    if (!(c)) {                                                                                              \
      printf("FAIL line %d: %s\n", __LINE__, #c);                                                            \
      fails++;                                                                                               \
    }                                                                                                        \
  } while (0)

struct FakePlatform : AppPlatform {
    std::vector<std::pair<int, float>> events;
    std::vector<int> emotes;
    float mv = 0.2f, ma = 0.1f, md = 0;
    void faceEvent(int type, float k) override { events.push_back({type, k}); }
    void faceEmote(int e) override { emotes.push_back(e); }
    std::vector<std::string> said;
    void faceSay(const char* t) override { said.push_back(t); }
    bool faceUrge(int, float& v) override { v = 0.5f; return true; }
    std::vector<std::string> shown;
    float showSec = 0, lookX = 9, lookY = 9;
    int wakes = 0;
    void faceWake() override { wakes++; }
    void faceShow(const char* a, float s) override {
      shown.push_back(a);
      showSec = s;
    }
    void faceLook(float x, float y) override { lookX = x, lookY = y; }
    bool faceMood(float& v, float& a, float& d) override {
      v = mv;
      a = ma;
      d = md;
      return true;
    }
    std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    std::vector<std::string> logs, urls;
    std::map<std::string, std::string> kv;
    bool exited = false, timeKnown = true;
    uint32_t millis() override {
      return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::steady_clock::now() - t0)
          .count();
    }
    bool localTime(AppTime& t) override {
      t = {14, 5, 9, 22, 10, 2026, 4};
      return timeKnown;
    }
    float light() override { return 0.25f; }
    bool hasMotion = true;
    bool motion(float v[6]) override {
      if (!hasMotion) return false;
      float m[6] = {0.1f, 0.9f, 0.2f, 5, -6, 7};
      memcpy(v, m, sizeof m);
      return true;
    }
    size_t kvLoad(const char* app, char* buf, size_t cap) override {
      auto it = kv.find(app);
      if (it == kv.end()) return 0;
      size_t n = it->second.size() < cap ? it->second.size() : cap;
      memcpy(buf, it->second.data(), n);
      return n;
    }
    void kvSave(const char* app, const char* buf, size_t len) override { kv[app] = std::string(buf, len); }
    bool httpStart(const char* app, int id, const char* url) override {
      urls.push_back(url);
      return true;
    }
    void log(const char* app, const char* msg) override { logs.push_back(msg); }
    void requestExit() override { exited = true; }
    bool captured = false;
    void setCapture(bool on) override { captured = on; }
    bool needsKnown = true;
    bool faceNeeds(float& at, float& re, float& pl) override {
      at = 0.8f, re = 0.3f, pl = 0.55f;
      return needsKnown;
    }
    bool hasButtons = false;
    int visX0 = 0, visX1 = APP_W;
    bool buttons() override { return hasButtons; }
    void visibleColumns(int& x0, int& x1) override { x0 = visX0, x1 = visX1; }
    bool logged(const char* sub) const {
      for (auto& l : logs)
        if (l.find(sub) != std::string::npos) return true;
      return false;
    }
};

static bool startApp(AppVM& vm, const char* src, char* err = nullptr, size_t n = 0) {
  char e[200];
  return vm.start("app", src, strlen(src), err ? err : e, err ? n : sizeof e);
}

static void savePpm(const AppFrame& f, const char* path) {
  Canvas c;
  c.create(APP_W, APP_H);
  c.fill(0);
  appReplay(f, c, 0, 0);
  FILE* o = fopen(path, "wb");
  fprintf(o, "P6 %d %d 255\n", APP_W, APP_H);
  for (int i = 0; i < APP_W * APP_H; i++) {
    uint16_t v = c.buffer()[i];
    v = (uint16_t)(v >> 8 | v << 8);
    unsigned char px[3] = {(unsigned char)((v >> 11) << 3), (unsigned char)(((v >> 5) & 63) << 2),
                           (unsigned char)((v & 31) << 3)};
    fwrite(px, 1, 3, o);
  }
  fclose(o);
}

int main(int argc, char** argv) {
  const char* outDir = argc > 1 ? argv[1] : nullptr;
  //==============================
  // drawing
  //==============================
  {
    FakePlatform p;
    AppVM vm(&p);
    const char* src = R"(
      local n = 0
      function update(dt) n = n + 1; return n == 1 end
      function draw()
        gfx.clear(0x101820)
        gfx.rect(10, 10, 100, 40, gfx.rgb(255, 80, 80), 8)
        gfx.circle(200, 60, 30, 0x40ff80)
        gfx.line(10, 200, 310, 150, 0xffffff, 4)
        gfx.text("Hello", 160, 120, 0xffffff, 2, "c")
        gfx.text("left", 10, 230, 0x8888ff)
        gfx.text("right", 310, 230, 0xff8888, 1, "r")
      end)";
    CHECK(startApp(vm, src) && vm.running());
    CHECK(vm.tick(0.03f));
    const AppFrame& f = vm.frame();
    CHECK(f.count == 7 && f.version == 1 && !f.overflow);
    CHECK(f.cmds[0].op == OP_CLEAR && f.cmds[1].op == OP_RRECT && f.cmds[1].a == 8 &&
          f.cmds[2].op == OP_CIRCLE);
    CHECK(!vm.tick(0.03f));
    if (outDir) {
      std::string path = std::string(outDir) + "/hello.ppm";
      savePpm(f, path.c_str());
    }
    Canvas full;
    full.create(APP_W, APP_H);
    full.fill(0);
    appReplay(f, full, 0, 0);
    Canvas tile;
    tile.create(APP_TILE_W, APP_TILE_H);
    bool same = true;
    for (int ty = 0; ty < APP_TILES_Y; ty++)
      for (int tx = 0; tx < APP_TILES_X; tx++) {
        tile.fill(0);
        appReplay(f, tile, tx * APP_TILE_W, ty * APP_TILE_H);
        for (int y = 0; y < APP_TILE_H; y++)
          for (int x = 0; x < APP_TILE_W; x++)
            if (tile.buffer()[y * APP_TILE_W + x] !=
                full.buffer()[(ty * APP_TILE_H + y) * APP_W + tx * APP_TILE_W + x])
              same = false;
      }
    CHECK(same);
    vm.stop();
  }
  //==============================
  // only the changed tile's hash changes
  //==============================
  {
    AppFrame a, b;
    auto rect = [](AppFrame& f, int x, int y, uint16_t col) {
      AppCmd* c = f.add();
      c->op = OP_RECT;
      c->x = x;
      c->y = y;
      c->w = 20;
      c->h = 20;
      c->color = col;
    };
    AppCmd* c = a.add();
    c->op = OP_CLEAR;
    c->color = 0;
    rect(a, 5, 5, 0xFFFF);
    c = b.add();
    c->op = OP_CLEAR;
    c->color = 0;
    rect(b, 5, 5, 0xFFFF);
    rect(b, 250, 200, 0xF800);
    int changed = 0;
    for (int ty = 0; ty < APP_TILES_Y; ty++)
      for (int tx = 0; tx < APP_TILES_X; tx++) changed += appTileHash(a, tx, ty) != appTileHash(b, tx, ty);
    CHECK(changed >= 1 && changed <= 4);
  }
  //==============================
  // sandbox
  //==============================
  {
    FakePlatform p;
    AppVM vm(&p);
    CHECK(startApp(vm, R"(
      local names = {"os","io","debug","package","require","dofile","loadfile","load","warn"}
      local leaked = {}
      for _, n in ipairs(names) do if _G[n] ~= nil then leaked[#leaked+1] = n end end
      if string.dump ~= nil then leaked[#leaked+1] = "string.dump" end
      sys.log("leaked:" .. table.concat(leaked, ","))
      sys.log("libs:" .. type(string.format) .. type(table.sort) .. type(math.floor) .. type(coroutine.wrap))
      function draw() end)"));
    CHECK(p.logged("leaked:") && !p.logged("leaked:o") && !p.logged("leaked:i") && !p.logged("leaked:d") &&
          !p.logged("leaked:p") && !p.logged("leaked:r") && !p.logged("leaked:l") && !p.logged("leaked:w") &&
          !p.logged("leaked:s"));
    CHECK(p.logged("libs:functionfunctionfunctionfunction"));
    vm.stop();
  }
  //==============================
  // errors
  //==============================
  {
    FakePlatform p;
    AppVM vm(&p);
    char err[200];
    CHECK(!startApp(vm, "function draw() end\nlocal x = = 1", err, sizeof err) && strstr(err, "app:2:") &&
          vm.failed());
    CHECK(vm.tick(0.03f) && vm.frame().count > 4 && !vm.tick(0.03f));
    vm.stop();
    CHECK(!startApp(vm, "x = 1", err, sizeof err) && strstr(err, "draw()"));
    vm.stop();
    CHECK(!startApp(vm,
                    "\x1b"
                    "Lua rest",
                    err, sizeof err) &&
          strstr(err, "binary"));
    vm.stop();
    CHECK(!startApp(vm, "error('boom')", err, sizeof err) && strstr(err, "boom"));
    vm.stop();
    CHECK(startApp(
        vm,
        "local n=0 function update() n=n+1 end function draw() if n > 2 then error('late') end gfx.clear(0) end function init() end"));
    CHECK(vm.running());
    vm.stop();
  }
  {
    FakePlatform p;
    AppVM vm(&p);
    CHECK(startApp(
        vm,
        "local n=0 function update() n=n+1 return true end function draw() if n > 2 then error('late failure') end gfx.clear(0) end"));
    int frames = 0;
    for (int i = 0; i < 6; i++) frames += vm.tick(0.03f);
    CHECK(vm.failed() && frames >= 3 && strstr(vm.error(), "late failure"));
    vm.stop();
  }
  //==============================
  // limits
  //==============================
  {
    FakePlatform p;
    AppVM vm(&p);
    auto t0 = p.millis();
    CHECK(startApp(vm, "function update() while true do end end function draw() end"));
    CHECK(vm.tick(0.03f) && vm.failed() && strstr(vm.error(), "took too long"));
    CHECK(p.millis() - t0 < 400);
    vm.stop();
    {
      AppVM vb(&p);
      auto tb = p.millis();
      CHECK(startApp(
          vb,
          "local s = string.rep('a', 3000) function update() return s:find('.-.-.-.-.-b') end function draw() end"));
      CHECK(vb.tick(0.03f) && vb.failed() && strstr(vb.error(), "took too long"));
      CHECK(p.millis() - tb < 400);
      vb.stop();
    }
    AppVM vm2(&p);
    CHECK(startApp(
        vm2,
        "local t = {} function update() for i = 1, 100000 do t[i] = string.rep('x', 100) .. i end end function draw() end"));
    CHECK(vm2.tick(0.03f) && vm2.failed() && strstr(vm2.error(), "memory") && vm2.memoryUsed() <= 40 * 1024);
    vm2.stop();
    CHECK(vm2.memoryUsed() == 0);
    AppVM vm3(&p);
    CHECK(startApp(vm3,
                   "function f(n) return 1 + f(n + 1) end function update() f(1) end function draw() end"));
    CHECK(vm3.tick(0.03f) && vm3.failed());  // stack overflow / out of memory, caught
    vm3.stop();
    AppVM vm4(&p);
    CHECK(startApp(vm4, "function f() return pcall(f) end function update() f() end function draw() end"));
    vm4.tick(0.03f);  // C-stack depth is bounded (LUAI_MAXCCALLS), no crash
    vm4.stop();
    AppVM vm5(&p);
    CHECK(startApp(
        vm5,
        "function draw() for i=1,1000 do gfx.rect(i%300, i%200, 5, 5, 0xffffff) gfx.text('t'..i, 0, 0, 0xffffff) end end"));
    CHECK(vm5.tick(0.03f) && !vm5.failed() && vm5.frame().count == APP_MAX_CMDS && vm5.frame().overflow);
    vm5.stop();
  }
  //==============================
  // touch, coroutines, sys
  //==============================
  {
    FakePlatform p;
    AppVM vm(&p);
    CHECK(startApp(vm, R"(
      local taps, co = 0, nil
      function init()
        co = coroutine.wrap(function() for i = 1, 20 do coroutine.yield(i * 10) end end)
        local h, m, s, d, mo, y, wd = sys.time()
        sys.log(string.format("time %02d:%02d:%02d %d.%d.%d wd%d light %.2f", h, m, s, d, mo, y, wd, sys.light()))
      end
      function touch(kind, x, y) taps = taps + 1; sys.log(kind .. " " .. x .. "," .. y .. " co=" .. co()); return kind == "up" end
      function draw() gfx.clear(0) end
      function update(dt) if taps == 4 then sys.exit() end end
      local ax, ay, az, gx, gy, gz = sys.motion()
      sys.log(string.format("motion %.1f %.1f %.1f %d %d %d", ax, ay, az, gx, gy, gz))
      sys.capture(true))"));
    CHECK(p.logged("motion 0.1 0.9 0.2 5 -6 7"));
    CHECK(p.captured);
    CHECK(p.logged("time 14:05:09 22.10.2026 wd4 light 0.25"));
    vm.tick(0.03f);
    vm.touch(TOUCH_DOWN, 10, 20);
    CHECK(!vm.tick(0.03f));
    vm.touch(TOUCH_UP, 11, 21);
    CHECK(vm.tick(0.03f));
    CHECK(p.logged("down 10,20 co=10") && p.logged("up 11,21 co=20"));
    vm.touch(TOUCH_MOVE, 1, 1);
    vm.touch(TOUCH_MOVE, 2, 2);
    vm.tick(0.03f);
    CHECK(p.exited);
    p.timeKnown = false;
    CHECK(vm.running());
    vm.stop();
  }
  //==============================
  // saved data
  //==============================
  {
    FakePlatform p;
    {
      AppVM vm(&p);
      CHECK(startApp(vm, R"(
        function init()
          local n = store.get("count", 0)
          store.set("count", n + 1); store.set("name", "teevee"); store.set("on", true); store.set("pi", 3.25)
          sys.log("count=" .. tostring(n))
        end
        function draw() end)"));
      vm.stop();
    }
    CHECK(p.logged("count=0") && p.kv["app"].find("ncount=1") != std::string::npos);
    {
      AppVM vm(&p);
      CHECK(startApp(vm, R"(
        function init()
          sys.log("again count=" .. tostring(store.get("count")) .. " name=" .. store.get("name") .. " on=" .. tostring(store.get("on")) .. " pi=" .. store.get("pi") .. " none=" .. tostring(store.get("zzz")) .. " dflt=" .. store.get("zzz", "d"))
          local ok1, e1 = pcall(store.set, "k", "line\nbreak")
          local ok2, e2 = pcall(store.set, "bad key", 1)
          local ok3, e3 = pcall(store.set, "k", {})
          local ok4, e4 = pcall(store.set, "k", string.rep("x", 60))
          sys.log("errors " .. tostring(ok1) .. tostring(ok2) .. tostring(ok3) .. tostring(ok4))
          for i = 1, 20 do pcall(store.set, "key" .. i, i) end
          store.set("name", nil)
        end
        function draw() end)"));
      vm.stop();
    }
    CHECK(p.logged("again count=1 name=teevee on=true pi=3.25 none=nil dflt=d") &&
          p.logged("errors falsefalsefalsefalse"));
    CHECK(p.kv["app"].find("sname") == std::string::npos);
    int lines = 0;
    for (char c : p.kv["app"]) lines += c == '\n';
    CHECK(lines <= 16);
  }
  //==============================
  // http
  //==============================
  {
    FakePlatform p;
    AppVM vm(&p);
    CHECK(startApp(vm, R"(
      function init()
        local id = http.get("http://example.local/a", function(status, body) sys.log("got " .. status .. " " .. body) end)
        sys.log("id=" .. tostring(id))
        local id2 = http.get("http://example.local/b", function() end)
        local id3, why = http.get("http://example.local/c", function() end)
        sys.log("third=" .. tostring(id3) .. " " .. tostring(why))
        local s, why2 = http.get("https://example.com", function() end)
        sys.log("https=" .. tostring(s) .. " " .. tostring(why2))
      end
      function draw() end)"));
    CHECK(p.urls.size() == 2 && p.urls[0] == "http://example.local/a");
    CHECK(p.logged("id=1") && p.logged("third=nil too many") && p.logged("https=nil only http"));
    vm.httpResult(1, 200, "hello world", 11);
    CHECK(p.logged("got 200 hello world"));
    vm.httpResult(1, 200, "again", 5);
    CHECK(!p.logged("got 200 again"));
    vm.httpResult(2, -1, "", 0);
    vm.stop();
  }
  //==============================
  // the sample apps in apps/
  //==============================
  {
    struct Tap {
        int x, y;
    };
    struct Sample {
        const char* name;
        std::vector<Tap> taps;
    };
    std::vector<Sample> samples = {{"hello", {{160, 120}}},
                                   {"clock", {{160, 120}}},
                                   {"counter", {{160, 100}, {40, 200}, {160, 100}}},
                                   {"bounce", {{50, 50}, {250, 200}}},
                                   {"timer", {{160, 200}, {160, 100}, {160, 100}}},
                                   {"fetch", {{160, 120}}},
                                   {"level", {}},
                                   {"snake", {{50, 100}, {250, 100}, {250, 100}, {50, 100}}},
                                   {"dice", {{160, 100}}},
                                   {"stopwatch", {{160, 40}, {160, 200}, {160, 40}}},
                                   {"pomodoro", {{160, 100}}},
                                   {"rps", {{160, 180}}},
                                   {"simon", {{160, 120}, {160, 30}, {280, 120}, {40, 120}}},
                                   {"reflex", {{160, 120}, {160, 120}}},
                                   {"tictactoe", {{190, 80}, {130, 140}, {250, 200}}},
                                   {"guess", {{120, 170}, {200, 170}, {160, 215}}},
                                   {"care", {{80, 215}, {240, 215}, {160, 150}, {160, 150}}},
                                   {"clockface", {{160, 120}}},
                                   {"countdown", {{60, 190}, {260, 190}, {160, 220}}},
                                   {"memory", {{50, 50}, {130, 50}, {210, 50}, {160, 120}}},
                                   {"alarm", {{260, 40}, {260, 140}, {160, 210}}},
                                   {"quiz", {{90, 200}, {230, 200}, {160, 120}}},
                                   {"petgame", {{160, 60}, {100, 60}, {220, 60}}},
                                   {"weather", {{160, 120}}}};
    for (auto& s : samples) {
      std::string path = std::string("../apps/") + s.name + ".lua", src;
      FILE* f = fopen(path.c_str(), "rb");
      CHECK(f != nullptr);
      if (!f) continue;
      char buf[4096];
      size_t n;
      while ((n = fread(buf, 1, sizeof buf, f)) > 0) src.append(buf, n);
      fclose(f);
      FakePlatform p;
      AppVM vm(&p);
      char err[200] = "";
      bool started = vm.start(s.name, src.data(), src.size(), err, sizeof err);
      if (!started) printf("  %s did not start: %s\n", s.name, err);
      CHECK(started);
      if (!started) continue;
      for (int i = 0; i < 40; i++) vm.tick(0.033f);
      for (auto& tp : s.taps) {
        vm.touch(TOUCH_DOWN, tp.x, tp.y);
        for (int i = 0; i < 3; i++) vm.tick(0.033f);
        vm.touch(TOUCH_UP, tp.x, tp.y);
        vm.tick(0.033f);
      }
      if (!strcmp(s.name, "weather")) vm.httpResult(1, 200, "+12", 3);
      if (!strcmp(s.name, "fetch")) {
        static const char body[] =
            "Berlin: +12C\nsecond line of text that is long enough to need wrapping across the width\nthird";
        vm.httpResult(p.urls.size(), 200, body, sizeof body - 1);
        vm.tick(0.033f);
      }
      for (int i = 0; i < 10; i++) vm.tick(0.033f);
      if (vm.failed()) printf("  %s failed: %s\n", s.name, vm.error());
      CHECK(!vm.failed());
      CHECK(vm.frame().count > 0 && !vm.frame().overflow && vm.memoryUsed() < 40 * 1024);
      printf("  %-8s ok  cmds=%3u  lua memory=%5zu B\n", s.name, vm.frame().count, vm.memoryUsed());
      if (outDir) {
        std::string o = std::string(outDir) + "/app_" + s.name + ".ppm";
        savePpm(vm.frame(), o.c_str());
      }
      vm.stop();
    }
  }
  {
    const auto file = [](const char* type, int w, int h, std::vector<uint8_t> px) {
      std::vector<uint8_t> f = {'b',
                                'A',
                                (uint8_t)type[0],
                                (uint8_t)type[1],
                                (uint8_t)w,
                                (uint8_t)(w >> 8),
                                (uint8_t)h,
                                (uint8_t)(h >> 8)};
      f.insert(f.end(), px.begin(), px.end());
      return f;
    };
    const auto rowOf = [](const std::vector<uint8_t>& f, const BaInfo& b, uint32_t frame, int x, int y, int n,
                          uint16_t* out) {
      return baRow(b, frame, x, y, n, out, [&](uint32_t off, void* buf, size_t len) {
        if (off + len > f.size()) return false;
        memcpy(buf, f.data() + off, len);
        return true;
      });
    };
    const auto sw = [](uint16_t c) { return (uint16_t)(c >> 8 | c << 8); };
    BaInfo b;
    uint16_t px[8];
    auto m = file("b0", 3, 3, {0b10000000, 0b10000000, 0b01000000, 0b00000000});
    CHECK(baParse(m.data(), m.size(), b) && b.bits == 1 && b.frameBytes() == 2 && b.frames == 2);
    auto m1 = file("b0", 9, 1, {0b10101010, 0b10000000});
    CHECK(baParse(m1.data(), m1.size(), b) && b.frames == 1);
    CHECK(rowOf(m1, b, 0, 7, 0, 2, px) && px[0] == 0 && px[1] == 0xFFFF);
    auto c = file("b2", 2, 1, {0xF8, 0x00, 0x07, 0xE0, 0x00, 0x1F, 0xFF, 0xFF});
    CHECK(baParse(c.data(), c.size(), b) && b.frames == 2);
    CHECK(rowOf(c, b, 0, 0, 0, 2, px) && px[0] == sw(0xF800) && px[1] == sw(0x07E0));
    CHECK(rowOf(c, b, 3, 0, 0, 2, px) && px[0] == sw(0x001F) && px[1] == 0xFFFF);  // frames wrap
    auto t = file("b3", 1, 1, {0xFF, 0x00, 0x00});
    CHECK(baParse(t.data(), t.size(), b) && rowOf(t, b, 0, 0, 0, 1, px) && px[0] == sw(0xF800));
    auto g = file("bw", 1, 1, {0xFF});
    CHECK(baParse(g.data(), g.size(), b) && rowOf(g, b, 0, 0, 0, 1, px) && px[0] == 0xFFFF);
    auto r = file("b1", 3, 1, {0xE0, 0x1C, 0x03});
    CHECK(baParse(r.data(), r.size(), b) && rowOf(r, b, 0, 0, 0, 3, px) && px[0] == sw(0xE000) &&
          (sw(px[1]) & 0x07E0) >= 0x07C0 && (sw(px[2]) & 0x001F) >= 0x1E);
    const uint8_t bad[8] = {'x', 'A', 'b', '2', 1, 0, 1, 0}, kind[8] = {'b', 'A', 'z', 'z', 1, 0, 1, 0},
                  none[8] = {'b', 'A', 'b', '2', 0, 0, 1, 0};
    CHECK(!baParse(bad, 20, b) && !baParse(kind, 20, b) && !baParse(none, 20, b));
    CHECK(!baParse(c.data(), 8, b) && !baParse(c.data(), 4, b));

    struct Mem : AppImageSource {
        std::vector<uint8_t> f;
        bool info(const char* name, BaInfo& i) override {
          return !strcmp(name, "logo") && baParse(f.data(), f.size(), i);
        }
        bool read(const char* name, uint32_t off, void* buf, size_t len) override {
          if (off + len > f.size()) return false;
          memcpy(buf, f.data() + off, len);
          return true;
        }
    } mem;
    std::vector<uint8_t> pix;
    for (int fr = 0; fr < 2; fr++)
      for (int i = 0; i < 100 * 60; i++) {
        pix.push_back(fr ? 0x07 : 0xF8);
        pix.push_back(fr ? 0xE0 : 0x00);
      }
    mem.f = file("b2", 100, 60, pix);
    appImages = &mem;
    FakePlatform ip;
    AppVM vm(&ip);
    char err[200] = "";
    const char* src = R"(
      frame = 0
      function draw() gfx.clear(0); local w, h, n = gfx.image("logo", 70, 40, frame); local missing = gfx.image("nope", 0, 0); sys.log(string.format("img %d %d %d %s", w, h, n, tostring(missing))) end
      function touch() frame = frame + 1; return true end)";
    CHECK(vm.start("img", src, strlen(src), err, sizeof err));
    for (int i = 0; i < 3; i++) vm.tick(0.03f);
    CHECK(ip.logged("img 100 60 2 nil") && !vm.failed());
    Canvas full;
    full.create(APP_W, APP_H);
    full.fill(0);
    appReplay(vm.frame(), full, 0, 0);
    const auto at = [&](Canvas& cv, int x, int y) { return cv.buffer()[y * cv.width() + x]; };
    CHECK(at(full, 70, 40) == sw(0xF800) && at(full, 169, 99) == sw(0xF800) && at(full, 69, 40) == 0 &&
          at(full, 170, 99) == 0);
    Canvas tile;
    tile.create(APP_TILE_W, APP_TILE_H);
    tile.fill(0);
    appReplay(vm.frame(), tile, 80, 48);
    CHECK(at(tile, 0, 0) == sw(0xF800));
    const uint32_t before = appTileHash(vm.frame(), 1, 1);
    vm.touch(TOUCH_DOWN, 1, 1);
    for (int i = 0; i < 3; i++) vm.tick(0.03f);
    CHECK(appTileHash(vm.frame(), 1, 1) != before);
    full.fill(0);
    appReplay(vm.frame(), full, 0, 0);
    CHECK(at(full, 70, 40) == sw(0x07E0));
    vm.stop();
    appImages = nullptr;
  }
  {  // background(dt): runs without draw, draws nothing, keeps its data; no background() or an error is reported, never a screen; the wake is rate limited
    FakePlatform p;
    AppLimits lim;
    lim.background = true;
    lim.memory = 28 * 1024;
    AppVM vm(&p, lim);
    char err[200] = "";
    const char* src = R"(
      n = store.get("n", 0)
      function background(dt) n = n + 1; store.set("n", n); gfx.clear(0); face.wake(); face.wake(); sys.log("bg " .. n) end)";
    CHECK(vm.start("bg", src, strlen(src), err, sizeof err));
    CHECK(!vm.tick(1.f) && p.logged("bg 1") && !vm.failed());
    CHECK(p.wakes == 1);
    vm.stop();
    CHECK(p.kv["bg"].find("n=1") != std::string::npos);
    FakePlatform q;
    AppVM none(&q, lim);
    CHECK(!none.start("x", "function draw() end", 19, err, sizeof err) && strstr(err, "background"));
    none.stop();
    FakePlatform r;
    AppVM boom(&r, lim);
    const char* bad = "function background() error('oops') end";
    CHECK(boom.start("x", bad, strlen(bad), err, sizeof err));
    CHECK(!boom.tick(1.f) && boom.failed() && strstr(boom.error(), "oops"));
    boom.stop();
    FakePlatform w;
    AppVM stuck(&w, lim);
    const char* loop = "function background() while true do end end";
    CHECK(stuck.start("x", loop, strlen(loop), err, sizeof err));
    CHECK(!stuck.tick(1.f) && stuck.failed());
    stuck.stop();
    FakePlatform a;
    AppVM al(&a, lim);
    a.kv["alarm"] = "non=b1\n";
    std::string s;
    {
      FILE* f = fopen("../apps/alarm.lua", "rb");
      CHECK(f != nullptr);
      char b[4096];
      size_t k;
      while (f && (k = fread(b, 1, sizeof b, f)) > 0) s.append(b, k);
      if (f) fclose(f);
    }
    a.kv["alarm"] = "bon=1\nnhour=14\nnmin=0\n";
    CHECK(al.start("alarm", s.data(), s.size(), err, sizeof err));
    al.tick(1.f);
    CHECK(a.wakes == 1 && !al.failed());
    if (al.failed()) printf("  alarm: %s\n", al.error());
    std::this_thread::sleep_for(std::chrono::milliseconds(1300));
    al.tick(1.f);
    CHECK(a.wakes == 1);
    al.stop();
  }
  {
    FakePlatform p;
    AppVM vm(&p);
    char err[200] = "";
    const char* src = R"(
      function init() face.show("happy", 500); face.show("sad"); face.look(3, -0.5) end
      function draw() gfx.clear(0) end)";
    CHECK(vm.start("show", src, strlen(src), err, sizeof err));
    vm.tick(0.03f);
    CHECK(p.shown.size() == 1 && p.shown[0] == "happy" && p.showSec == 30);
    CHECK(p.lookX == 1 && p.lookY == -0.5f && !vm.failed());
    vm.stop();
    FakePlatform q;
    AppVM bad(&q);
    CHECK(startApp(bad, "function draw() end function touch() face.show('nonsense') end"));
    bad.touch(TOUCH_DOWN, 1, 1);
    bad.tick(0.03f);
    CHECK(bad.failed() && q.shown.empty());
    bad.stop();
  }
  {
    FakePlatform p;
    p.hasButtons = true;
    p.visX0 = 20;
    p.visX1 = 300;
    AppVM vm(&p);
    char err[200] = "";
    const char* src = R"(
      function init()
        sys.log(string.format("buttons %s cols %d %d", tostring(sys.buttons()), gfx.X0, gfx.X1))
        local a, r, p = face.needs()
        sys.log(string.format("needs %.2f %.2f %.2f", a, r, p))
      end
      function draw() gfx.clear(0) end
      function button(name, down) sys.log("button " .. name .. " " .. tostring(down)); return down end)";
    CHECK(vm.start("btn", src, strlen(src), err, sizeof err));
    vm.tick(0.03f);
    vm.button(4, true);
    vm.button(4, false);
    vm.button(2, true);
    vm.button(99, true);
    vm.button(-1, true);
    CHECK(p.logged("needs 0.80 0.30 0.55"));
    CHECK(p.logged("buttons true cols 20 300") && p.logged("button a true") && p.logged("button a false") &&
          p.logged("button left true"));
    CHECK(!p.logged("button nil") && !vm.failed());
    vm.stop();
    const char* tapper = R"(
      function draw() gfx.clear(0) end
      function touch(kind, x, y) sys.log(string.format("touch %s %d %d", kind, x, y)) end)";
    CHECK(vm.start("tap", tapper, strlen(tapper), err, sizeof err));
    vm.button(5, true);
    vm.button(4, true);
    vm.button(4, false);
    int touches = 0;
    for (auto& l : p.logs) touches += l.rfind("touch ", 0) == 0;
    CHECK(p.logged("touch down 160 120") && p.logged("touch up 160 120") && touches == 2);
    vm.stop();
    FakePlatform q;
    AppVM v2(&q);
    CHECK(v2.start("btn", src, strlen(src), err, sizeof err) && q.logged("buttons false cols 0 320"));
    v2.stop();
  }
  {  // every sample app on the Lilka: a few thousand random button presses, never fails or overflows
    for (const char* name :
         {"hello", "clock", "counter", "bounce", "timer", "fetch", "level", "snake", "dice", "stopwatch",
          "pomodoro", "rps", "simon", "reflex", "tictactoe", "guess", "care"}) {
      std::string path = std::string("../apps/") + name + ".lua", src;
      FILE* f = fopen(path.c_str(), "rb");
      if (!f) {
        CHECK(f != nullptr);
        continue;
      }
      char buf[4096];
      size_t n;
      while ((n = fread(buf, 1, sizeof buf, f)) > 0) src.append(buf, n);
      fclose(f);
      FakePlatform p;
      p.hasButtons = true;
      p.visX0 = 20;
      p.visX1 = 300;
      AppVM vm(&p);
      char err[200] = "";
      CHECK(vm.start(name, src.data(), src.size(), err, sizeof err));
      uint32_t r = 99;
      for (int i = 0; i < 3000 && !vm.failed(); i++) {
        r = r * 1664525u + 1013904223u;
        const int b = (r >> 16) % APP_BUTTON_COUNT;
        vm.button(b, true);
        vm.tick(0.033f);
        if ((r >> 8) & 1) vm.tick(0.033f);
        vm.button(b, false);
        vm.tick(0.033f);
      }
      if (vm.failed()) printf("  %s failed with buttons: %s\n", name, vm.error());
      CHECK(!vm.failed() && !vm.frame().overflow);
      if (outDir) {
        std::string o = std::string(outDir) + "/lilka_" + name + ".ppm";
        savePpm(vm.frame(), o.c_str());
      }
      vm.stop();
    }
    printf("  buttons: all sample apps ok on a 280 px panel\n");
  }
  {
    FakePlatform p;
    AppVM vm(&p);
    char err[200] = "";
    const char* src = R"(
      function draw()
        gfx.clear(0)
        gfx.face(160, 70, 150)
        local v, a, d = face.mood()
        sys.log(string.format("mood %.1f %.1f %.1f", v, a, d))
      end
      function touch(kind)
        if kind ~= "down" then return end
        for i = 1, 10 do face.event("praise", 0.9) end   -- only the first three (a burst) get through
        face.emote("heart"); face.emote("star")           -- one a second at most
        face.event("pet", 5)                              -- clamped to 1
      end)";
    CHECK(vm.start("facetest", src, strlen(src), err, sizeof err) && vm.running());
    vm.tick(0.03f);
    CHECK(p.logged("mood 0.2 0.1 0.0"));
    bool hasFace = false;
    for (int i = 0; i < vm.frame().count; i++)
      hasFace = hasFace || (vm.frame().cmds[i].op == OP_FACE && vm.frame().cmds[i].w == 150 &&
                            vm.frame().cmds[i].y == 70);
    CHECK(hasFace);
    vm.touch(TOUCH_DOWN, 10, 10);
    vm.tick(0.03f);
    CHECK(p.events.size() == 3 && p.events[0].first == EV_PRAISE && p.events[0].second > 0.89f &&
          p.events[0].second < 0.91f);
    CHECK(p.emotes.size() == 1 && p.emotes[0] == EM_HEART);
    CHECK(startApp(vm, "function draw() end function touch() face.event('nonsense') end"));
    vm.touch(TOUCH_DOWN, 1, 1);
    vm.tick(0.03f);
    CHECK(vm.failed() && strstr(vm.error(), "unknown event"));
    vm.stop();
    AppFrame fa, fb;
    fa.reset();
    fb.reset();
    if (AppCmd* c = fa.add()) {
      c->op = OP_FACE;
      c->x = 100;
      c->y = 60;
      c->w = 120;
    }
    if (AppCmd* c = fb.add()) {
      c->op = OP_FACE;
      c->x = 100;
      c->y = 60;
      c->w = 120;
    }
    CHECK(appTileHash(fa, 1, 1, 111) == appTileHash(fb, 1, 1, 111) &&
          appTileHash(fa, 1, 1, 111) != appTileHash(fb, 1, 1, 222));
    CHECK(appTileHash(fa, 3, 4, 111) == appTileHash(fb, 3, 4, 222));
  }
  {  // snake played at random for a while: never fails, never overflows the draw list, game over / restart work
    std::string src;
    FILE* f = fopen("../apps/snake.lua", "rb");
    CHECK(f != nullptr);
    if (f) {
      char buf[4096];
      size_t n;
      while ((n = fread(buf, 1, sizeof buf, f)) > 0) src.append(buf, n);
      fclose(f);
    }
    FakePlatform p;
    AppVM vm(&p);
    char err[200] = "";
    CHECK(vm.start("snake", src.data(), src.size(), err, sizeof err));
    uint32_t r = 12345;
    unsigned maxCmds = 0;
    for (int i = 0; i < 20000; i++) {
      r = r * 1664525u + 1013904223u;
      if ((r >> 24) % 9 == 0) {
        vm.touch(TOUCH_DOWN, (r >> 16) & 1 ? 40 : 280, 100);
        vm.touch(TOUCH_UP, 0, 0);
      }
      vm.tick(0.05f);
      if (vm.frame().count > maxCmds) maxCmds = vm.frame().count;
      if (vm.failed()) break;
    }
    printf("  snake stress: max %u draw commands\n", maxCmds);
    CHECK(!vm.failed() && !vm.frame().overflow && maxCmds < 250);
    vm.stop();
  }
  {  // face.say and face.urge: names checked, rate limited
    FakePlatform p; AppVM vm(&p); char err[200] = "";
    const char* src = R"(
      function init() face.say("hello"); face.say("again"); sys.log("urge " .. tostring(face.urge("mirth"))) end
      function draw() gfx.clear(0) end)";
    CHECK(vm.start("say", src, strlen(src), err, sizeof err));
    vm.tick(0.03f);
    CHECK(p.said.size() == 1 && p.said[0] == "hello");
    CHECK(p.logged("urge 0.5") && !vm.failed());
    vm.stop();
    FakePlatform q; AppVM bad(&q);
    CHECK(startApp(bad, "function draw() end function touch() face.urge('nonsense') end"));
    bad.touch(TOUCH_DOWN, 1, 1); bad.tick(0.03f);
    CHECK(bad.failed());
    bad.stop();
  }
  {  // recursing through C: with a floor set it stops as an ordinary Lua error, every way there is to get there, and the app can go on
    char here;
    tv_cstack_floor = &here - 24 * 1024;
    FakePlatform p; AppLimits lim; lim.memory = 400 * 1024; lim.callMs = 2000; lim.startMs = 2000; AppVM vm(&p, lim); char err[200] = "";
    const char* src = R"LUA(
      local function viaPcall(n) return select(2, pcall(function() return viaPcall(n + 1) end)) end
      local function viaGsub(n) local r; local ok, e = pcall(function() ("x"):gsub("x", function() r = viaGsub(n + 1) end) end); return r or e end
      local function viaSort(n) local r; local ok, e = pcall(table.sort, {3, 1, 2}, function(a, b) r = viaSort(n + 1); return a < b end); return r or e end
      local function viaTostring(n) local ok, e = pcall(tostring, setmetatable({}, {__tostring = function() return viaTostring(n + 1) end})); return e end
      function init()
        sys.log("pcall " .. tostring(viaPcall(0))); sys.log("gsub " .. tostring(viaGsub(0))); sys.log("sort " .. tostring(viaSort(0))); sys.log("tostring " .. tostring(viaTostring(0)))
        sys.log("still here")
      end
      function draw() gfx.clear(0) end)LUA";
#ifdef __SANITIZE_ADDRESS__
    // Under AddressSanitizer the "C stack overflow" raised in this recursion escapes the pcall that should catch it (the same
    // recursion on a plain Lua state is caught, and the plain build runs this check). Not run here; the other builds cover it.
    printf("  deep recursion: skipped under AddressSanitizer\n");
#else
    CHECK(vm.start("deep", src, strlen(src), err, sizeof err));
    if (vm.failed()) printf("  deep: %s\n", vm.error());
    CHECK(p.logged("still here") && p.logged("overflow"));
#endif
    vm.stop();
    // through the parser: an app's own source nested too deep is refused, not a crash
    std::string nested = "x = " + std::string(400, '(') + "1" + std::string(400, ')') + " function draw() end";
    FakePlatform q; AppVM parsed(&q, lim);
    CHECK(!parsed.start("nest", nested.data(), nested.size(), err, sizeof err) && parsed.failed());
    parsed.stop();
    tv_cstack_floor = nullptr;
  }
  printf("app tests: %s\n", fails ? "FAILED" : "ok");
  return fails;
}
