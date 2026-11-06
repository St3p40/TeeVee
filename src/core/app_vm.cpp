#include "app_vm.h"
#include <initializer_list>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "emotes.h"
#include "emotion.h"
#include "mind.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

// Lua API for apps. Every call into Lua goes through protectedCall(): a script error, running out of memory or
// running too long is caught and shown as an error screen; it can never take the board down.
//
//   gfx.clear([color])                  fill the screen (default black)
//   gfx.rect(x, y, w, h, color [, r])   filled rectangle, optional corner radius
//   gfx.circle(x, y, r, color)
//   gfx.line(x1, y1, x2, y2, color [, thickness])
//   gfx.text(s, x, y, color [, size [, align]])   size 1 (13 px) or 2 (24 px); align "l", "c" or "r"; y = vertical centre
//   gfx.rgb(r, g, b) -> color          colours are 0xRRGGBB numbers
//   gfx.measure(s [, size]) -> width
//   gfx.redraw()                        ask for another draw()
//   gfx.W, gfx.H                        320, 240
//   gfx.X0, gfx.X1                      the columns the panel shows: 0, 320 (the Lilka's 280 px panel: 20, 300); keep text and buttons inside
//   sys.buttons() -> true on a board with buttons (then button(name, down) gets them)   sys.ms()  sys.time()  sys.light()  sys.motion() -> ax,ay,az (g), gx,gy,gz (deg/s) or nil  sys.log(...)  sys.exit()  sys.capture(true)   (capture: you handle horizontal drags)
//   (functions you may define: update(dt), draw(), touch(kind, x, y), button(name, down), stop(); background(dt): once a second, with the app closed, if the owner chose it)
//   face.event(name [, k])  face.emote(name)  face.mood() -> v, a, d  face.needs() -> attention, rest, play (0 content .. 1 urgent)   face.show(name [, seconds])   face.look(x, y)   face.wake()   gfx.face(x, y, width)   gfx.image(name, x, y [, frame]) -> w, h, frames: a Bitmap Array file from the card's /teevee/images or flash   (the robot's feelings; see the cheat sheet in the web portal)
//   store.get(key [, default])  store.set(key, value)   numbers, strings (47 chars) and booleans; 16 keys
//   http.get(url, function(status, body) end) -> id | nil, err      http:// only; body up to 2 KB; status <= 0 = failed

struct AppVM::Call {
    enum Kind { SETUP, FN, HTTP } kind;
    const char* fn = nullptr;
    int argc = 0;
    float dt = 0;
    const char* touchKind = "";
    int x = 0, y = 0;
    int id = 0, status = 0;
    const char* body = "";
    size_t bodyLen = 0;
    bool ret = false;
    bool missing = false;
};

struct AppApi {
    static AppVM* self(lua_State* L) { return *(AppVM**)lua_getextraspace(L); }

    //==============================
    // helpers
    //==============================
    static int16_t coord(lua_State* L, int i) {
      lua_Number n = luaL_checknumber(L, i);
      if (n > 30000) n = 30000;
      if (n < -30000) n = -30000;
      return (int16_t)lroundf((float)n);
    }
    static uint16_t color(lua_State* L, int i) {
      lua_Number n = luaL_checknumber(L, i);
      uint32_t v = (uint32_t)(int32_t)n;
      return Canvas::rgb((v >> 16) & 255, (v >> 8) & 255, v & 255);
    }
    static AppCmd* cmd(lua_State* L, uint8_t op) {
      AppCmd* c = self(L)->frame_.add();
      if (c) c->op = op;
      return c;
    }
    static int textSize(lua_State* L, int i) {
      int s = (int)luaL_optinteger(L, i, 1);
      return s >= 2 ? 2 : 1;
    }

    //==============================
    // gfx
    //==============================
    static int gfxClear(lua_State* L) {
      uint16_t col = lua_gettop(L) >= 1 ? color(L, 1) : 0;
      if (AppCmd* c = cmd(L, OP_CLEAR)) c->color = col;
      return 0;
    }
    static int gfxRect(lua_State* L) {
      int16_t x = coord(L, 1), y = coord(L, 2), w = coord(L, 3), h = coord(L, 4);
      uint16_t col = color(L, 5);
      int r = (int)luaL_optinteger(L, 6, 0);
      if (AppCmd* c = cmd(L, r > 0 ? OP_RRECT : OP_RECT)) {
        c->x = x;
        c->y = y;
        c->w = w;
        c->h = h;
        c->color = col;
        c->a = (uint8_t)(r > 255 ? 255 : r > 0 ? r : 0);
      }
      return 0;
    }
    static int gfxCircle(lua_State* L) {
      int16_t x = coord(L, 1), y = coord(L, 2), r = coord(L, 3);
      uint16_t col = color(L, 4);
      if (AppCmd* c = cmd(L, OP_CIRCLE)) {
        c->x = x;
        c->y = y;
        c->w = r;
        c->color = col;
      }
      return 0;
    }
    static int gfxLine(lua_State* L) {
      int16_t x1 = coord(L, 1), y1 = coord(L, 2), x2 = coord(L, 3), y2 = coord(L, 4);
      uint16_t col = color(L, 5);
      int t = (int)luaL_optinteger(L, 6, 2);
      if (AppCmd* c = cmd(L, OP_LINE)) {
        c->x = x1;
        c->y = y1;
        c->w = x2;
        c->h = y2;
        c->color = col;
        c->a = (uint8_t)(t < 1 ? 1 : t > 40 ? 40 : t);
      }
      return 0;
    }
    static int gfxText(lua_State* L) {
      size_t n;
      const char* s = luaL_checklstring(L, 1, &n);
      int16_t x = coord(L, 2), y = coord(L, 3);
      uint16_t col = color(L, 4);
      int size = textSize(L, 5);
      const char* al = luaL_optstring(L, 6, "l");
      uint8_t align = al[0] == 'c' ? AL_CENTER : al[0] == 'r' ? AL_RIGHT : AL_LEFT;
      if (n > 120) n = 120;
      AppVM* vm = self(L);
      if (vm->frame_.count >= APP_MAX_CMDS) {
        vm->frame_.overflow = true;
        return 0;
      }
      int off = vm->frame_.addText(s, n);
      if (off < 0) return 0;
      if (AppCmd* c = cmd(L, OP_TEXT)) {
        c->x = x;
        c->y = y;
        c->color = col;
        c->a = (uint8_t)(size | align << 4);
        c->text = (uint16_t)off;
      }
      return 0;
    }
    static int gfxRgb(lua_State* L) {
      auto ch = [&](int i) {
        lua_Number v = luaL_checknumber(L, i);
        return (int)(v < 0 ? 0 : v > 255 ? 255 : v);
      };
      lua_pushinteger(L, (lua_Integer)(ch(1) << 16 | ch(2) << 8 | ch(3)));
      return 1;
    }
    static int gfxMeasure(lua_State* L) {
      const char* s = luaL_checkstring(L, 1);
      lua_pushinteger(L, Canvas::textWidth(s, textSize(L, 2) >= 2 ? FONT_LARGE : FONT_SMALL));
      return 1;
    }
    static int gfxRedraw(lua_State* L) {
      self(L)->dirty_ = true;
      return 0;
    }

    //==============================
    // sys
    //==============================
    static int sysMs(lua_State* L) {
      AppVM* vm = self(L);
      lua_pushinteger(L, (lua_Integer)(int32_t)(vm->plat_->millis() - vm->startedAt_));
      return 1;
    }
    static int sysTime(lua_State* L) {
      AppTime t;
      if (!self(L)->plat_->localTime(t)) {
        lua_pushnil(L);
        return 1;
      }
      lua_pushinteger(L, t.hour);
      lua_pushinteger(L, t.min);
      lua_pushinteger(L, t.sec);
      lua_pushinteger(L, t.day);
      lua_pushinteger(L, t.month);
      lua_pushinteger(L, t.year);
      lua_pushinteger(L, t.wday);
      return 7;
    }
    static int sysLight(lua_State* L) {
      lua_pushnumber(L, self(L)->plat_->light());
      return 1;
    }
    static int sysMotion(lua_State* L) {
      float v[6];
      if (!self(L)->plat_->motion(v)) {
        lua_pushnil(L);
        return 1;
      }
      for (int i = 0; i < 6; i++) lua_pushnumber(L, v[i]);
      return 6;
    }
    static int sysLog(lua_State* L) {
      char line[200];
      size_t k = 0;
      int n = lua_gettop(L);
      for (int i = 1; i <= n; i++) {
        size_t len;
        const char* s = luaL_tolstring(L, i, &len);
        if (k + 1 < sizeof line)
          k += snprintf(line + k, sizeof line - k, "%s%.*s", i > 1 ? "\t" : "", (int)(len < 150 ? len : 150),
                        s);
        lua_pop(L, 1);
      }
      AppVM* vm = self(L);
      vm->plat_->log(vm->name_, line);
      return 0;
    }
    static int sysExit(lua_State* L) {
      self(L)->plat_->requestExit();
      return 0;
    }
    static int sysButtons(lua_State* L) {
      lua_pushboolean(L, self(L)->plat_->buttons());
      return 1;
    }
    static int sysCapture(lua_State* L) {
      self(L)->plat_->setCapture(lua_toboolean(L, 1));
      return 0;
    }

    //==============================
    // face: apps that are part of the robot
    //==============================
    static void refill(AppVM* vm) {
      const uint32_t now = vm->plat_->millis(), dt = now - vm->tokenMs_;
      vm->tokenMs_ = now;
      vm->evTokens_ = fminf(3.f, vm->evTokens_ + dt * 0.003f);
      vm->emoteTokens_ = fminf(1.f, vm->emoteTokens_ + dt * (1.f / 1200.f));
    }
    static int faceEvent(lua_State* L) {
      const char* name = luaL_checkstring(L, 1);
      const float k = (float)luaL_optnumber(L, 2, 0.6);
      int type = -1;
      for (int i = 0; i < EV_COUNT; i++)
        if (!strcmp(name, EVENTS[i].name)) type = i;
      if (type < 0) return luaL_error(L, "face.event: unknown event '%s'", name);
      AppVM* vm = self(L);
      refill(vm);
      if (vm->evTokens_ >= 1.f) {
        vm->evTokens_ -= 1.f;
        vm->plat_->faceEvent(type, k < 0 ? 0 : k > 1 ? 1 : k);
      }
      return 0;
    }
    static int faceEmote(lua_State* L) {
      const char* name = luaL_checkstring(L, 1);
      const int id = emoteIndex(name);
      if (id <= 0) return luaL_error(L, "face.emote: unknown emote '%s'", name);
      AppVM* vm = self(L);
      refill(vm);
      if (vm->emoteTokens_ >= 1.f) {
        vm->emoteTokens_ -= 1.f;
        vm->plat_->faceEmote(id);
      }
      return 0;
    }
    static int faceShow(lua_State* L) {
      const char* name = luaL_checkstring(L, 1);
      const float s = (float)luaL_optnumber(L, 2, 3);
      bool known = false;
      for (int i = 0; i < A_COUNT; i++) known = known || !strcmp(name, ANCHOR_NAMES[i]);
      if (!known) return luaL_error(L, "face.show: unknown face '%s'", name);
      AppVM* vm = self(L);
      refill(vm);
      if (vm->emoteTokens_ >= 1.f) {
        vm->emoteTokens_ -= 1.f;
        vm->plat_->faceShow(name, s < 0.5f ? 0.5f : s > 30 ? 30 : s);
      }
      return 0;
    }
  static int faceSay(lua_State* L) {
    const char* text = luaL_checkstring(L, 1);
    AppVM* vm = self(L);
    refill(vm);
    if (vm->emoteTokens_ >= 1.f) {
      vm->emoteTokens_ -= 1.f;
      vm->plat_->faceSay(text);
    }
    return 0;
  }
  static int faceUrge(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    for (int i = 0; i < mind::OUT; i++)
      if (!strcmp(name, mind::URGE_NAMES[i])) {
        float v = 0;
        if (!self(L)->plat_->faceUrge(i, v)) return 0;
        lua_pushnumber(L, v);
        return 1;
      }
    return luaL_error(L, "face.urge: unknown urge '%s'", name);
  }
    static int
    faceWake(lua_State*
                 L) {  // (an alarm: it shares the emotes' limit, so an app cannot keep the screen on with it)
      AppVM* vm = self(L);
      refill(vm);
      if (vm->emoteTokens_ >= 1.f) {
        vm->emoteTokens_ -= 1.f;
        vm->plat_->faceWake();
      }
      return 0;
    }
    static int faceLook(lua_State* L) {
      const float x = (float)luaL_checknumber(L, 1), y = (float)luaL_checknumber(L, 2);
      self(L)->plat_->faceLook(x < -1 ? -1 : x > 1 ? 1 : x, y < -1 ? -1 : y > 1 ? 1 : y);
      return 0;
    }
    static int faceMood(lua_State* L) {
      float v = 0, a = 0, d = 0;
      if (!self(L)->plat_->faceMood(v, a, d)) return 0;
      lua_pushnumber(L, v);
      lua_pushnumber(L, a);
      lua_pushnumber(L, d);
      return 3;
    }
    static int faceNeeds(lua_State* L) {
      float at = 0, re = 0, pl = 0;
      if (!self(L)->plat_->faceNeeds(at, re, pl)) return 0;
      lua_pushnumber(L, at);
      lua_pushnumber(L, re);
      lua_pushnumber(L, pl);
      return 3;
    }
    // gfx.image(name, x, y [, frame]) -> width, height, frames; nothing if there is no such picture. Frames wrap round.
    static int gfxImage(lua_State* L) {
      const char* name = luaL_checkstring(L, 1);
      int16_t x = coord(L, 2), y = coord(L, 3);
      const lua_Integer frame = luaL_optinteger(L, 4, 0);
      BaInfo b;
      if (!appImages || !appImages->info(name, b)) return 0;
      AppVM* vm = self(L);
      if (AppCmd* c = cmd(L, OP_IMAGE)) {
        const int off = vm->frame_.addText(name, strlen(name));
        if (off < 0) return 0;
        const lua_Integer at = frame % (lua_Integer)b.frames;
        c->x = x;
        c->y = y;
        c->w = (int16_t)(b.w > 320 ? 320 : b.w);
        c->h = (int16_t)(b.h > 240 ? 240 : b.h);
        c->color = (uint16_t)(at < 0 ? at + (lua_Integer)b.frames : at);
        c->text = (uint16_t)off;
      }
      lua_pushinteger(L, b.w);
      lua_pushinteger(L, b.h);
      lua_pushinteger(L, (lua_Integer)b.frames);
      return 3;
    }
    static int gfxFace(lua_State* L) {
      int16_t x = coord(L, 1), y = coord(L, 2), w = coord(L, 3);
      if (AppCmd* c = cmd(L, OP_FACE)) {
        c->x = x;
        c->y = y;
        c->w = w < 20 ? 20 : w > 400 ? 400 : w;
      }
      return 0;
    }

    //==============================
    // store
    //==============================
    static bool validKey(const char* k) {
      size_t n = strlen(k);
      if (n < 1 || n > 15) return false;
      for (; *k; k++)
        if (!((*k >= 'a' && *k <= 'z') || (*k >= 'A' && *k <= 'Z') || (*k >= '0' && *k <= '9') || *k == '_'))
          return false;
      return true;
    }
    static int kvFind(AppVM* vm, const char* key) {
      for (int i = 0; i < vm->kvCount_; i++)
        if (!strcmp(vm->kv_[i].key, key)) return i;
      return -1;
    }
    static int storeGet(lua_State* L) {
      const char* key = luaL_checkstring(L, 1);
      AppVM* vm = self(L);
      int i = validKey(key) ? kvFind(vm, key) : -1;
      if (i < 0) {
        lua_settop(L, 2);
        lua_pushvalue(L, 2);
        return 1;
      }
      const AppVM::KV& e = vm->kv_[i];
      if (e.type == 'b')
        lua_pushboolean(L, e.val[0] == '1');
      else if (e.type == 'n') {
        if (!lua_stringtonumber(L, e.val)) lua_pushnil(L);
      } else
        lua_pushstring(L, e.val);
      return 1;
    }
    static int storeSet(lua_State* L) {
      const char* key = luaL_checkstring(L, 1);
      if (!validKey(key)) return luaL_error(L, "bad key (1-15 letters, digits or _)");
      AppVM* vm = self(L);
      int i = kvFind(vm, key);
      if (lua_isnoneornil(L, 2)) {
        if (i >= 0) {
          vm->kv_[i] = vm->kv_[--vm->kvCount_];
          vm->kvDirty_ = true;
        }
        return 0;
      }
      AppVM::KV e;
      memset(&e, 0, sizeof e);
      snprintf(e.key, sizeof e.key, "%s", key);
      if (lua_type(L, 2) == LUA_TBOOLEAN) {
        e.type = 'b';
        e.val[0] = lua_toboolean(L, 2) ? '1' : '0';
      } else if (lua_type(L, 2) == LUA_TNUMBER) {
        e.type = 'n';
        lua_pushvalue(L, 2);
        snprintf(e.val, sizeof e.val, "%s", lua_tostring(L, -1));
        lua_pop(L, 1);
      } else if (lua_type(L, 2) == LUA_TSTRING) {
        size_t n;
        const char* s = lua_tolstring(L, 2, &n);
        if (n >= sizeof e.val) return luaL_error(L, "string too long (max %d)", (int)sizeof e.val - 1);
        if (strchr(s, '\n')) return luaL_error(L, "string cannot contain a newline");
        e.type = 's';
        memcpy(e.val, s, n);
      } else {
        return luaL_error(L, "store holds numbers, strings and booleans");
      }
      if (i < 0) {
        if (vm->kvCount_ >= 16) return luaL_error(L, "store is full (16 keys)");
        i = vm->kvCount_++;
      }
      vm->kv_[i] = e;
      vm->kvDirty_ = true;
      return 0;
    }

    //==============================
    // http
    //==============================
    static int httpGet(lua_State* L) {
      size_t n;
      const char* url = luaL_checklstring(L, 1, &n);
      luaL_checktype(L, 2, LUA_TFUNCTION);
      AppVM* vm = self(L);
      if (n > 200 || strncmp(url, "http://", 7)) {
        lua_pushnil(L);
        lua_pushstring(L, "only http:// URLs up to 200 characters");
        return 2;
      }
      if (vm->httpPending_ >= 2) {
        lua_pushnil(L);
        lua_pushstring(L, "too many requests in flight");
        return 2;
      }
      int id = vm->nextHttp_++;
      lua_getfield(L, LUA_REGISTRYINDEX, "tv_http");
      lua_pushvalue(L, 2);
      lua_rawseti(L, -2, id);
      lua_pop(L, 1);
      if (!vm->plat_->httpStart(vm->name_, id, url)) {
        lua_getfield(L, LUA_REGISTRYINDEX, "tv_http");
        lua_pushnil(L);
        lua_rawseti(L, -2, id);
        lua_pop(L, 1);
        lua_pushnil(L);
        lua_pushstring(L, "cannot start the request");
        return 2;
      }
      vm->httpPending_++;
      lua_pushinteger(L, id);
      return 1;
    }

    static int print(lua_State* L) { return sysLog(L); }

    //==============================
    // setup (runs protected)
    //==============================
    static void setup(lua_State* L) {
      luaL_checkstack(L, 12, "setup");
      luaL_requiref(L, "_G", luaopen_base, 1);
      lua_pop(L, 1);
      luaL_requiref(L, LUA_COLIBNAME, luaopen_coroutine, 1);
      lua_pop(L, 1);
      luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
      lua_pop(L, 1);
      luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
      lua_pop(L, 1);
      luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);
      lua_pop(L, 1);
      for (const char* g : {"dofile", "loadfile", "load", "warn"}) {
        lua_pushnil(L);
        lua_setglobal(L, g);
      }
      lua_getglobal(L, LUA_STRLIBNAME);
      lua_pushnil(L);
      lua_setfield(L, -2, "dump");
      lua_pop(L, 1);
      lua_pushcfunction(L, print);
      lua_setglobal(L, "print");

      static const luaL_Reg gfx[] = {{"clear", gfxClear},     {"rect", gfxRect},     {"circle", gfxCircle},
                                     {"line", gfxLine},       {"text", gfxText},     {"rgb", gfxRgb},
                                     {"measure", gfxMeasure}, {"redraw", gfxRedraw}, {"face", gfxFace},
                                     {"image", gfxImage},     {nullptr, nullptr}};
      static const luaL_Reg sys[] = {{"ms", sysMs},           {"time", sysTime},       {"light", sysLight},
                                     {"motion", sysMotion},   {"log", sysLog},         {"exit", sysExit},
                                     {"capture", sysCapture}, {"buttons", sysButtons}, {nullptr, nullptr}};
      static const luaL_Reg store[] = {{"get", storeGet}, {"set", storeSet}, {nullptr, nullptr}};
      static const luaL_Reg http[] = {{"get", httpGet}, {nullptr, nullptr}};
      luaL_newlib(L, gfx);
      lua_pushinteger(L, APP_W);
      lua_setfield(L, -2, "W");
      lua_pushinteger(L, APP_H);
      lua_setfield(L, -2, "H");
      int x0 = 0, x1 = APP_W;
      self(L)->plat_->visibleColumns(x0, x1);
      lua_pushinteger(L, x0 < 0 ? 0 : x0 > APP_W / 2 ? APP_W / 2 : x0);
      lua_setfield(L, -2, "X0");
      lua_pushinteger(L, x1 > APP_W ? APP_W : x1 < APP_W / 2 ? APP_W / 2 : x1);
      lua_setfield(L, -2, "X1");
      lua_setglobal(L, "gfx");
      luaL_newlib(L, sys);
      lua_setglobal(L, "sys");
      static const luaL_Reg face[] = {{"event", faceEvent}, {"emote", faceEmote}, {"mood", faceMood},
                                      {"needs", faceNeeds}, {"show", faceShow},   {"look", faceLook},
                                      {"wake", faceWake}, {"say", faceSay}, {"urge", faceUrge},   {nullptr, nullptr}};
      luaL_newlib(L, face);
      lua_setglobal(L, "face");
      luaL_newlib(L, store);
      lua_setglobal(L, "store");
      luaL_newlib(L, http);
      lua_setglobal(L, "http");
      lua_newtable(L);
      lua_setfield(L, LUA_REGISTRYINDEX, "tv_http");
    }

    // The one C function every call into Lua goes through (so that even pushing arguments can fail safely).
    static int trampoline(lua_State* L) {
      AppVM::Call* c = (AppVM::Call*)lua_touserdata(L, 1);
      luaL_checkstack(L, 10, "call");
      switch (c->kind) {
        case AppVM::Call::SETUP: setup(L); break;
        case AppVM::Call::FN:
          lua_getglobal(L, c->fn);
          if (!lua_isfunction(L, -1)) {
            c->missing = true;
            break;
          }
          if (c->argc == 1) lua_pushnumber(L, c->dt);
          if (c->argc == 2) {
            lua_pushstring(L, c->touchKind);
            lua_pushboolean(L, c->x);
          }
          if (c->argc == 3) {
            lua_pushstring(L, c->touchKind);
            lua_pushinteger(L, c->x);
            lua_pushinteger(L, c->y);
          }
          lua_call(L, c->argc, 1);
          c->ret = lua_toboolean(L, -1);
          break;
        case AppVM::Call::HTTP:
          lua_getfield(L, LUA_REGISTRYINDEX, "tv_http");
          lua_rawgeti(L, -1, c->id);
          if (!lua_isfunction(L, -1)) {
            c->missing = true;
            break;
          }
          lua_pushnil(L);
          lua_rawseti(L, -3, c->id);
          lua_pushinteger(L, c->status);
          lua_pushlstring(L, c->body, c->bodyLen);
          lua_call(L, 2, 0);
          break;
      }
      return 0;
    }

    static void hookFn(lua_State* L, lua_Debug*) {
      AppVM* vm = self(L);
      if ((int32_t)(vm->plat_->millis() - vm->deadline_) > 0) luaL_error(L, "app took too long");
    }
    static int panicFn(lua_State* L) {
      AppVM* vm = self(L);
      vm->plat_->log(vm->name_, "lua panic: unprotected error");
      return 0;
    }
};

//==============================
// -
//==============================

AppVM::AppVM(AppPlatform* platform, Limits limits) : plat_(platform), lim_(limits) {}
AppVM::~AppVM() { stop(); }

void* AppVM::allocator(void* ud, void* ptr, size_t osize, size_t nsize) {
  AppVM* vm = (AppVM*)ud;
  if (!ptr) osize = 0;
  if (nsize == 0) {
    free(ptr);
    vm->used_ -= osize;
    return nullptr;
  }
  if (vm->used_ - osize + nsize > vm->lim_.memory) return nullptr;
  void* p = realloc(ptr, nsize);
  if (p) vm->used_ += nsize - osize;
  return p;
}

void AppVM::fail(const char* msg) {
  snprintf(err_, sizeof err_, "%s", msg);
  failed_ = true;
  plat_->log(name_, err_);
}

bool AppVM::protectedCall(Call& c, uint32_t budgetMs) {
  deadline_ = plat_->millis() + budgetMs;
  lua_pushcfunction(L_, AppApi::trampoline);
  lua_pushlightuserdata(L_, &c);
  if (lua_pcall(L_, 1, 0, 0) == LUA_OK) return true;
  const char* m = lua_isstring(L_, -1) ? lua_tostring(L_, -1) : "error object is not a string";
  fail(m);
  lua_pop(L_, 1);
  return false;
}

bool AppVM::start(const char* name, const char* source, size_t len, char* err, size_t errCap) {
  stop();
  auto report = [&](const char* m) {
    if (err && errCap) snprintf(err, errCap, "%s", m);
    return false;
  };
  if (!name || !*name || strlen(name) >= sizeof name_) return report("bad app name");
  snprintf(name_, sizeof name_, "%s", name);
  failed_ = false;
  errorShown_ = false;
  dirty_ = true;
  err_[0] = 0;
  frame_.reset();
  startedAt_ = plat_->millis();
  httpPending_ = 0;
  kvCount_ = 0;
  kvDirty_ = false;
  {
    char blob[1024];
    size_t n = plat_->kvLoad(name_, blob, sizeof blob - 1);
    blob[n] = 0;
    for (char* line = blob; *line && kvCount_ < 16;) {
      char* nl = strchr(line, '\n');
      if (nl) *nl = 0;
      char* eq = strchr(line, '=');
      if (eq && line[0] && strlen(line) > 1 && (line[0] == 'n' || line[0] == 's' || line[0] == 'b')) {
        *eq = 0;
        KV& e = kv_[kvCount_];
        memset(&e, 0, sizeof e);
        e.type = line[0];
        snprintf(e.key, sizeof e.key, "%s", line + 1);
        snprintf(e.val, sizeof e.val, "%s", eq + 1);
        kvCount_++;
      }
      if (!nl) break;
      line = nl + 1;
    }
  }
  if (len && (unsigned char)source[0] == 27) {
    fail("binary chunks are not allowed");
    return report(err_);
  }

  L_ = lua_newstate(allocator, this);
  if (!L_) {
    fail("not enough memory to start");
    return report(err_);
  }
  *(AppVM**)lua_getextraspace(L_) = this;
  lua_atpanic(L_, AppApi::panicFn);
  lua_sethook(L_, AppApi::hookFn, LUA_MASKCOUNT, 1000);

  Call setup;
  setup.kind = Call::SETUP;
  bool ok = protectedCall(setup, lim_.startMs);
  if (ok) {
    char chunk[24];
    snprintf(chunk, sizeof chunk, "@%s", name_);
    int r = luaL_loadbufferx(L_, source, len, chunk, "t");
    if (r != LUA_OK) {
      const char* m = lua_isstring(L_, -1) ? lua_tostring(L_, -1) : "load error";
      fail(m);
      lua_pop(L_, 1);
      ok = false;
    } else {
      deadline_ = plat_->millis() + lim_.startMs;
      if (lua_pcall(L_, 0, 0, 0) != LUA_OK) {
        const char* m = lua_isstring(L_, -1) ? lua_tostring(L_, -1) : "error";
        fail(m);
        lua_pop(L_, 1);
        ok = false;
      }
    }
  }
  if (ok) {
    Call init;
    init.kind = Call::FN;
    init.fn = "init";
    ok = protectedCall(init, lim_.startMs);
  }
  if (ok) {
    lua_getglobal(L_, lim_.background ? "background" : "draw");
    bool hasDraw = lua_isfunction(L_, -1);
    lua_pop(L_, 1);
    if (!hasDraw) {
      fail(lim_.background ? "the app has no background()" : "the app must define draw()");
      ok = false;
    }
  }
  if (!ok) {
    report(err_);
    return false;
  }
  return true;
}

void AppVM::stop() {
  if (!L_) {
    failed_ = false;
    return;
  }
  if (!failed_) {
    Call fn;
    fn.kind = Call::FN;
    fn.fn = "stop";
    protectedCall(fn, lim_.callMs);
  }
  kvFlush(true);
  deadline_ = plat_->millis() + lim_.startMs;
  lua_close(L_);
  L_ = nullptr;
  used_ = 0;
  failed_ = false;
}

bool AppVM::kvFlush(bool force) {
  if (!kvDirty_) return true;
  uint32_t now = plat_->millis();
  if (!force && now - kvSavedAt_ < 3000) return false;  // spare the flash
  char blob[1024];
  size_t n = 0;
  for (int i = 0; i < kvCount_ && n + 1 < sizeof blob; i++)
    n += snprintf(blob + n, sizeof blob - n, "%c%s=%s\n", kv_[i].type, kv_[i].key, kv_[i].val);
  plat_->kvSave(name_, blob, n < sizeof blob ? n : sizeof blob - 1);
  kvDirty_ = false;
  kvSavedAt_ = now;
  return true;
}

void AppVM::buildErrorFrame() {
  frame_.reset();
  auto text = [&](const char* s, int x, int y, uint16_t col, int size, uint8_t align) {
    AppCmd* c = frame_.add();
    int off = frame_.addText(s, strlen(s));
    if (!c || off < 0) return;
    c->op = OP_TEXT;
    c->x = x;
    c->y = y;
    c->color = col;
    c->a = (uint8_t)(size | align << 4);
    c->text = (uint16_t)off;
  };
  if (AppCmd* c = frame_.add()) {
    c->op = OP_CLEAR;
    c->color = Canvas::rgb(40, 0, 0);
  }
  text("App error", APP_W / 2, 26, Canvas::rgb(255, 90, 90), 2, AL_CENTER);
  text(name_, APP_W / 2, 52, Canvas::rgb(200, 150, 150), 1, AL_CENTER);
  // Wrap the message at word boundaries, 36 characters a line.
  const char* p = err_;
  for (int line = 0; line < 7 && *p; line++) {
    char buf[40];
    int n = 0, lastSpace = -1;
    while (p[n] && n < 36) {
      if (p[n] == ' ') lastSpace = n;
      n++;
    }
    if (p[n] && lastSpace > 8) n = lastSpace;
    memcpy(buf, p, n);
    buf[n] = 0;
    text(buf, 12, 84 + line * 18, Canvas::rgb(255, 255, 255), 1, AL_LEFT);
    p += n;
    while (*p == ' ') p++;
  }
  text("hold the screen to go back", APP_W / 2, 226, Canvas::rgb(150, 110, 110), 1, AL_CENTER);
  frame_.version++;
}

bool AppVM::tick(float dt) {
  if (failed_) {  // shows the error screen once (also when the app never got as far as having a VM)
    if (errorShown_) return false;
    errorShown_ = true;
    buildErrorFrame();
    return true;
  }
  if (!L_) return false;
  Call up;
  up.kind = Call::FN;
  up.fn = lim_.background ? "background" : "update";
  up.argc = 1;
  up.dt = dt;
  if (lim_.background) {  // nothing on the screen: an error is only reported, through failed() and error()
    frame_.reset();
    protectedCall(up, lim_.callMs);
    kvFlush(false);
    return false;
  }
  if (!protectedCall(up, lim_.callMs)) {
    errorShown_ = true;
    buildErrorFrame();
    return true;
  }
  if (up.ret) dirty_ = true;
  if (!dirty_) {
    kvFlush(false);
    return false;
  }
  dirty_ = false;
  frame_.reset();
  Call dr;
  dr.kind = Call::FN;
  dr.fn = "draw";
  if (!protectedCall(dr, lim_.callMs)) {
    errorShown_ = true;
    buildErrorFrame();
    return true;
  }
  frame_.version++;
  kvFlush(false);
  return true;
}

void AppVM::touch(AppTouch kind, int x, int y) {
  if (!running()) return;
  Call t;
  t.kind = Call::FN;
  t.fn = "touch";
  t.argc = 3;
  t.touchKind = kind == TOUCH_DOWN ? "down" : kind == TOUCH_MOVE ? "move" : "up";
  t.x = x;
  t.y = y;
  if (protectedCall(t, lim_.callMs) && t.ret) dirty_ = true;
}

void AppVM::button(int index, bool down) {
  if (!running() || index < 0 || index >= APP_BUTTON_COUNT) return;
  Call b;
  b.kind = Call::FN;
  b.fn = "button";
  b.argc = 2;
  b.touchKind = APP_BUTTONS[index];
  b.x = down;
  if (!protectedCall(b, lim_.callMs)) return;
  if (b.ret) dirty_ = true;
  if (b.missing && index == 4) touch(down ? TOUCH_DOWN : TOUCH_UP, APP_W / 2, APP_H / 2);
}

void AppVM::httpResult(int id, int status, const char* body, size_t len) {
  if (!running()) return;
  if (httpPending_ > 0) httpPending_--;
  Call h;
  h.kind = Call::HTTP;
  h.id = id;
  h.status = status;
  h.body = body ? body : "";
  h.bodyLen = body ? len : 0;
  if (protectedCall(h, lim_.callMs)) dirty_ = true;
}
