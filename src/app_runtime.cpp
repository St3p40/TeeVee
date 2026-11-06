#include "app_runtime.h"
#include <FS.h>
#include <LittleFS.h>
#include "sdcard.h"
#include "shared.h"
#include <Preferences.h>
#include <WiFi.h>
#include <time.h>
#include <algorithm>
#include <esp_system.h>
#include "core/mind.h"

extern "C" const char* tv_cstack_floor;
#include "core/app_vm.h"
#include "board.h"
#include "imu.h"

namespace apps {
namespace {
constexpr size_t HTTP_BODY_MAX = 2048;
constexpr size_t HTTP_RAW_MAX = 4096;
constexpr uint32_t TICK_MS = 33;
constexpr uint32_t STACK_BYTES =
    10240;  // apps use under 5 KB; LUAI_MAXCCALLS (platformio.ini) stops recursion through C before the rest is gone

struct TouchEv {
    uint8_t kind;
    int16_t x, y;
};
constexpr uint8_t BUTTON_EV = 0x80;
struct HttpReq {
    int id;
    char url[208];
};
struct HttpRes {
    int id, status;
    char* body;
    size_t len;
};

char s_names[MAX_APPS][12];
bool s_sd[MAX_APPS];
int s_count = 0;

TaskHandle_t s_task = nullptr, s_httpTask = nullptr;
// The app on the screen and the background app take turns and share one stack, kept from the start: after a few apps the heap has plenty free but its large block is cut up.
// A task ends by saying it is done and waiting; whoever stops it deletes it, so its stack is never reused while it is still standing on it.
StackType_t s_appStack[STACK_BYTES];
StaticTask_t s_appTcb;
volatile bool s_taskDone = false, s_bgDone = false;
char s_bgName[12] = "";

// Lua makes thousands of tiny allocations. Let loose on the heap they leave no block for Wi-Fi, which then cannot even send a reply (measured at 2.4 KB; 5 KB works). Holding a
// block while Lua loads keeps them in the scraps around it; an app too big to load with it held loads without it. A private pool for Lua does not work: it must then hold them
// all in the one large block, and that does not fit.
// Data memory is only about 37 KB with no app running: ESP.getFreeHeap() also counts some 37 KB of instruction memory that malloc cannot use, so always ask with MALLOC_CAP_8BIT.
constexpr size_t WIFI_ROOM = 4 * 1024;
constexpr size_t CSTACK_MARGIN = 3584;  // what a last Lua level through C can still need: gsub, format and the error itself (tv_cstack_floor)
// What Lua may use, counted without the heap's overhead. It must leave the rest of the robot something: at 40 KB an app could take the heap down to 200 bytes.
constexpr size_t APP_MEMORY = 22 * 1024;
QueueHandle_t s_touchQ = nullptr, s_reqQ = nullptr, s_resQ = nullptr;
SemaphoreHandle_t s_lock = nullptr;
volatile bool s_active = false, s_stopReq = false, s_exitReq = false, s_capture = false;
char s_current[12] = "";
volatile float s_light = 1.f;
volatile uint32_t s_luaMem = 0;

AppFrame s_frame;
uint32_t s_version = 0;

void copyFrame(AppFrame& dst, const AppFrame& src) {
  memcpy(&dst, &src, offsetof(AppFrame, cmds));
  memcpy(dst.cmds, src.cmds, src.count * sizeof(AppCmd));
  memcpy(dst.text, src.text, src.textLen);
}

void commit(const AppFrame& f) {
  xSemaphoreTake(s_lock, portMAX_DELAY);
  copyFrame(s_frame, f);
  s_version++;  // a global counter: restarting an app can never repeat an old frame number
  xSemaphoreGive(s_lock);
}

//==============================
// minimal HTTP/1.0 GET (no TLS: pulling in HTTPClient + TLS would cost over 100 KB of flash)
//==============================
void fetchUrl(const char* url, HttpRes& res) {
  res.status = -1;
  if (strncmp(url, "http://", 7)) return;
  const char* p = url + 7;
  char host[64];
  int port = 80, n = 0;
  while (*p && *p != ':' && *p != '/' && n < (int)sizeof host - 1) host[n++] = *p++;
  host[n] = 0;
  if (*p == ':') port = atoi(++p);
  while (*p && *p != '/') p++;
  const char* path = *p ? p : "/";
  if (!host[0] || port <= 0 || port > 65535) return;
  if (WiFi.status() != WL_CONNECTED) {
    res.status = -2;
    return;
  }
  WiFiClient cl;
  if (!cl.connect(host, (uint16_t)port, 5000)) {
    res.status = -3;
    return;
  }
  cl.printf("GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: teevee\r\nConnection: close\r\n\r\n", path, host);
  char* raw = (char*)malloc(HTTP_RAW_MAX + 1);
  if (!raw) {
    res.status = -4;
    return;
  }
  size_t got = 0;
  const uint32_t deadline = millis() + 8000;
  while (got < HTTP_RAW_MAX && (int32_t)(millis() - deadline) < 0 && !s_stopReq) {
    int avail = cl.available();
    if (avail > 0) {
      int r = cl.read((uint8_t*)raw + got, std::min<size_t>(avail, HTTP_RAW_MAX - got));
      if (r > 0) got += r;
    } else if (!cl.connected()) {
      break;
    } else {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
  cl.stop();
  raw[got] = 0;
  if (got >= 12 && !strncmp(raw, "HTTP/", 5)) {
    res.status = atoi(raw + 9);
    char* body = strstr(raw, "\r\n\r\n");
    size_t blen = body ? got - (size_t)(body + 4 - raw) : 0;
    if (blen > HTTP_BODY_MAX) blen = HTTP_BODY_MAX;
    res.body = (char*)malloc(blen + 1);
    if (res.body) {
      if (body) memcpy(res.body, body + 4, blen);
      res.body[blen] = 0;
      res.len = blen;
    }
  }
  free(raw);
}

void httpWorker(void*) {
  while (!s_stopReq) {
    HttpReq req;
    if (xQueueReceive(s_reqQ, &req, pdMS_TO_TICKS(200)) != pdTRUE) continue;
    HttpRes res{req.id, -1, nullptr, 0};
    fetchUrl(req.url, res);
    if (s_stopReq || xQueueSend(s_resQ, &res, pdMS_TO_TICKS(1000)) != pdTRUE) free(res.body);
  }
  s_httpTask = nullptr;
  vTaskDelete(nullptr);
}

volatile float s_moodV = 0, s_moodA = 0, s_moodD = 0;
volatile bool s_moodKnown = false;
volatile float s_needAtt = 0, s_needRest = 0, s_needPlay = 0;
volatile bool s_needsKnown = false;
volatile uint8_t s_faceEmote = 0;
char s_faceShow[16] = "";
volatile uint32_t s_faceShowMs = 0;
volatile float s_lookX = 0, s_lookY = 0;
volatile bool s_lookWanted = false;
volatile bool s_wakeWanted = false;
char s_say[8] = "";
volatile bool s_sayWanted = false;
volatile float s_urges[mind::OUT] = {};
volatile bool s_urgesKnown = false;

// Pictures for gfx.image: the card's /teevee/images/<name>.ba, else flash /images/<name>.ba. One file stays open; the app's task asks about pictures and the
// display's core reads their pixels, so a lock guards it.
struct FileImages : AppImageSource {
    SemaphoreHandle_t lock = xSemaphoreCreateMutex();
    File file;
    char current[12] = "";
    BaInfo cached{};
    bool valid = false;
    bool open(const char* name) {
      if (valid && !strcmp(current, name)) return true;
      if (file) file.close();
      valid = false;
      if (!validName(name)) return false;
      char path[40];
      snprintf(path, sizeof path, "/teevee/images/%s.ba", name);
      file = sdcard::mounted() && sdcard::fs().exists(path) ? sdcard::fs().open(path, "r") : File();
      if (!file) {
        snprintf(path, sizeof path, "/images/%s.ba", name);
        file = LittleFS.exists(path) ? LittleFS.open(path, "r") : File();
      }
      uint8_t head[BA_HEADER];
      if (!file || file.read(head, sizeof head) != sizeof head || !baParse(head, file.size(), cached))
        return false;
      snprintf(current, sizeof current, "%s", name);
      return valid = true;
    }
    bool info(const char* name, BaInfo& b) override {
      xSemaphoreTake(lock, portMAX_DELAY);
      const bool ok = open(name);
      if (ok) b = cached;
      xSemaphoreGive(lock);
      return ok;
    }
    bool read(const char* name, uint32_t offset, void* buf, size_t len) override {
      xSemaphoreTake(lock, portMAX_DELAY);
      const bool ok = open(name) && file.seek(offset) && file.read((uint8_t*)buf, len) == len;
      xSemaphoreGive(lock);
      return ok;
    }
};
static FileImages* s_images = nullptr;

struct FwPlatform : AppPlatform {
    void faceEvent(int type, float intensity) override {
      EmotionEvent e{(uint8_t)type, intensity};
      xQueueSend(g_events, &e, 0);
    }
    void faceEmote(int emote) override { s_faceEmote = (uint8_t)emote; }
    void faceShow(const char* anchor, float seconds) override {
      if (s_faceShowMs) return;
      snprintf(s_faceShow, sizeof s_faceShow, "%s", anchor);
      s_faceShowMs = (uint32_t)(seconds * 1000);
    }
    void faceWake() override { s_wakeWanted = true; }
  void faceSay(const char* text) override {
    if (s_sayWanted) return;
    snprintf(s_say, sizeof s_say, "%s", text);
    s_sayWanted = true;
  }
  bool faceUrge(int index, float& value) override {
    value = s_urges[index];
    return s_urgesKnown;
  }
    void faceLook(float x, float y) override {
      s_lookX = x, s_lookY = y;
      s_lookWanted = true;
    }
    bool faceMood(float& v, float& a, float& d) override {
      v = s_moodV;
      a = s_moodA;
      d = s_moodD;
      return s_moodKnown;
    }
    bool faceNeeds(float& at, float& re, float& pl) override {
      at = s_needAtt;
      re = s_needRest;
      pl = s_needPlay;
      return s_needsKnown;
    }
    bool buttons() override { return BOARD_HAS_BUTTONS; }
    void visibleColumns(int& x0, int& x1) override { x0 = LCD_CROP, x1 = LCD_CROP + LCD_PANEL_W; }
    uint32_t millis() override { return ::millis(); }
    bool localTime(AppTime& t) override {
      struct tm tm;
      if (!getLocalTime(&tm, 0)) return false;
      t = {tm.tm_hour, tm.tm_min, tm.tm_sec, tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900, tm.tm_wday};
      return true;
    }
    float light() override { return s_light; }
    bool motion(float v[6]) override {
      imu::State s = imu::state();
      if (!s.live) return false;
      v[0] = s.last.ax;
      v[1] = s.last.ay;
      v[2] = s.last.az;
      v[3] = s.last.gx;
      v[4] = s.last.gy;
      v[5] = s.last.gz;
      return true;
    }
    size_t kvLoad(const char* app, char* buf, size_t cap) override {
      Preferences p;
      if (!p.begin("tv_apps", true)) return 0;
      size_t n = p.getBytesLength(app);
      if (n > cap) n = cap;
      if (n) p.getBytes(app, buf, n);
      p.end();
      return n;
    }
    void kvSave(const char* app, const char* buf, size_t len) override {
      Preferences p;
      if (!p.begin("tv_apps", false)) return;
      p.putBytes(app, buf, len);
      p.end();
    }
    bool httpStart(const char*, int id, const char* url) override {
      if (!s_httpTask &&
          xTaskCreatePinnedToCore(httpWorker, "apphttp", 6144, nullptr, 1, &s_httpTask, 0) != pdPASS)
        return false;
      HttpReq req;
      req.id = id;
      snprintf(req.url, sizeof req.url, "%s", url);
      return xQueueSend(s_reqQ, &req, 0) == pdTRUE;
    }
    void log(const char* app, const char* msg) override { Serial.printf("app[%s]: %s\n", app, msg); }
    void requestExit() override { s_exitReq = true; }
    void setCapture(bool on) override { s_capture = on; }
};

//==============================
// the background app
//==============================
constexpr size_t BG_MEMORY = 18 * 1024, BG_MIN_HEAP = 28 * 1024;
constexpr uint32_t BG_TICK_MS = 1000, BG_AFTER_APP_MS = 3000;
char s_bgErr[80] = "";
TaskHandle_t s_bgTask = nullptr;
volatile bool s_bgStopReq = false;
bool s_bgBroken = false;  // it failed: not again until it is chosen or changed
uint32_t s_bgHoldUntil = 0;

void bgFailed(const char* why) {
  snprintf(s_bgErr, sizeof s_bgErr, "%s", why);
  for (char* c = s_bgErr; *c; c++)
    if (*c == '"' || *c == '\\' || *c == '\n') *c = '\'';
  s_bgBroken = true;
  Serial.printf("background app %s stopped: %s\n", s_bgName, s_bgErr);
}

struct BgPlatform : FwPlatform {
    bool httpStart(const char*, int, const char*) override { return false; }
    void requestExit() override { s_bgStopReq = true; }
    void setCapture(bool) override {}
};

void bgTask(void*) {
  tv_cstack_floor = (const char*)s_appStack + CSTACK_MARGIN;
  BgPlatform plat;
  AppLimits lim;
  lim.memory = BG_MEMORY;
  lim.background = true;
  AppVM* vm = new AppVM(&plat, lim);
  void* room = malloc(WIFI_ROOM);
  char err[160] = "";
  char* src = nullptr;
  size_t len = 0;
  if (File f = open(s_bgName)) {
    len = f.size();
    if (len <= MAX_SCRIPT && (src = (char*)malloc(len + 1))) {
      len = f.read((uint8_t*)src, len);
      src[len] = 0;
    }
    f.close();
  }
  bool started = src && vm->start(s_bgName, src, len, err, sizeof err);
  free(room);
  if (src && !started) started = vm->start(s_bgName, src, len, err, sizeof err);
  free(src);
  if (!started) bgFailed(err[0] ? err : "cannot read the app file");
  uint32_t last = millis();
  while (started && !s_bgStopReq) {
    for (uint32_t waited = 0; waited < BG_TICK_MS && !s_bgStopReq; waited += 100)
      vTaskDelay(pdMS_TO_TICKS(100));
    if (s_bgStopReq) break;
    const uint32_t now = millis();
    vm->tick((now - last) * 1e-3f);
    last = now;
    if (vm->failed()) {
      bgFailed(vm->error());
      break;
    }
  }
  delete vm;
  s_bgDone = true;
  for (;;) vTaskDelay(portMAX_DELAY);
}

void bgStop() {
  if (!s_bgTask) return;
  s_bgStopReq = true;
  for (int i = 0; i < 300 && !s_bgDone; i++) vTaskDelay(pdMS_TO_TICKS(10));
  if (!s_bgDone) {
    Serial.println("apps: the background app did not stop in time");
    return;
  }
  vTaskDelete(s_bgTask);
  s_bgTask = nullptr;
}

void appTask(void*) {
  tv_cstack_floor = (const char*)s_appStack + CSTACK_MARGIN;
  FwPlatform plat;
  AppLimits lim;
  lim.memory = APP_MEMORY;
  AppVM* vm = new AppVM(&plat, lim);
  void* room = malloc(WIFI_ROOM);
  char path[40], err[160] = "";
  pathOf(s_current, path, sizeof path);
  char* src = nullptr;
  size_t len = 0;
  if (File f = open(s_current)) {
    len = f.size();
    if (len <= MAX_SCRIPT && (src = (char*)malloc(len + 1))) {
      len = f.read((uint8_t*)src, len);
      src[len] = 0;
    }
    f.close();
  }
  if (src) {
    const bool started = vm->start(s_current, src, len, err, sizeof err);
    free(room);
    room = nullptr;
    if (!started) vm->start(s_current, src, len, err, sizeof err);
    free(src);
  } else {
    static const char* msg = "error('cannot read the app file')";
    vm->start(s_current, msg, strlen(msg), err, sizeof err);
  }
  free(room);
  uint32_t last = millis();
  while (!s_stopReq) {
    TouchEv t;
    while (xQueueReceive(s_touchQ, &t, 0) == pdTRUE) {
      if (t.kind & BUTTON_EV)
        vm->button(t.x, t.kind & 1);  // (button presses share the touch queue, in order)
      else
        vm->touch((AppTouch)t.kind, t.x, t.y);
    }
    HttpRes res;
    while (xQueueReceive(s_resQ, &res, 0) == pdTRUE) {
      vm->httpResult(res.id, res.status, res.body, res.len);
      free(res.body);
    }
    uint32_t now = millis();
    float dt = (now - last) * 1e-3f;
    last = now;
    if (dt > 0.2f) dt = 0.2f;
    if (vm->tick(dt)) commit(vm->frame());
    s_luaMem = vm->memoryUsed();
    vTaskDelay(pdMS_TO_TICKS(TICK_MS));
  }
  delete vm;
  HttpRes res;
  while (xQueueReceive(s_resQ, &res, 0) == pdTRUE) free(res.body);
  HttpReq req;
  while (xQueueReceive(s_reqQ, &req, 0) == pdTRUE) {}
  s_taskDone = true;
  for (;;) vTaskDelay(portMAX_DELAY);
}
}  // namespace

bool validName(const char* name) {
  size_t n = name ? strlen(name) : 0;
  if (n < 1 || n > 11) return false;
  for (const char* p = name; *p; p++)
    if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '-')) return false;
  return true;
}

bool pathOf(const char* name, char* buf, size_t n) {
  if (!validName(name)) return false;
  snprintf(buf, n, "/apps/%s.lua", name);
  return true;
}

const char SD_APPS[] = "/teevee/apps";

bool sdAppPath(const char* name, char* buf, size_t n) {
  if (!validName(name)) return false;
  snprintf(buf, n, "%s/%s.lua", SD_APPS, name);
  return true;
}

void rescan() {
  s_count = 0;
  memset(s_sd, 0, sizeof s_sd);
  File dir = LittleFS.open("/apps");
  if (!dir || !dir.isDirectory()) return;
  while (File f = dir.openNextFile()) {
    String n = f.name();
    int slash = n.lastIndexOf('/');
    if (slash >= 0) n = n.substring(slash + 1);
    if (!f.isDirectory() && n.endsWith(".lua")) {
      n = n.substring(0, n.length() - 4);
      if (validName(n.c_str()) && s_count < MAX_APPS)
        snprintf(s_names[s_count++], sizeof s_names[0], "%.11s", n.c_str());
    }
    f.close();
  }
  dir.close();
  if (sdcard::
          mounted()) {  // scripts on the SD card: /teevee/apps/<name>.lua (the card's copy wins over a flash copy of the same name)
    File sd = sdcard::fs().open(SD_APPS);
    if (sd && sd.isDirectory()) {
      while (File f = sd.openNextFile()) {
        String n = f.name();
        int slash = n.lastIndexOf('/');
        if (slash >= 0) n = n.substring(slash + 1);
        if (!f.isDirectory() && n.endsWith(".lua")) {
          n = n.substring(0, n.length() - 4);
          if (validName(n.c_str())) {
            int at = -1;
            for (int i = 0; i < s_count; i++)
              if (!strcmp(s_names[i], n.c_str())) at = i;
            if (at < 0 && s_count < MAX_APPS) {
              snprintf(s_names[s_count], sizeof s_names[0], "%.11s", n.c_str());
              at = s_count++;
            }
            if (at >= 0) s_sd[at] = true;
          }
        }
        f.close();
      }
      sd.close();
    }
  }
  for (int i = 1; i < s_count;
       i++) {  // insertion sort by name (arrays of chars can't be swapped by std::sort)
    char key[12];
    memcpy(key, s_names[i], sizeof key);
    const bool keySd = s_sd[i];
    int j = i - 1;
    while (j >= 0 && strcmp(s_names[j], key) > 0) {
      memcpy(s_names[j + 1], s_names[j], sizeof key);
      s_sd[j + 1] = s_sd[j];
      j--;
    }
    memcpy(s_names[j + 1], key, sizeof key);
    s_sd[j + 1] = keySd;
  }
}

bool begin() {
  if (!s_lock) {
    s_lock = xSemaphoreCreateMutex();
    s_touchQ = xQueueCreate(16, sizeof(TouchEv));
    s_reqQ = xQueueCreate(2, sizeof(HttpReq));
    s_resQ = xQueueCreate(2, sizeof(HttpRes));
  }
  if (!LittleFS.begin(true, "/lfs", 5, "spiffs")) {
    Serial.println("apps: the filesystem could not be mounted");
    return false;
  }
  if (!LittleFS.exists("/apps")) LittleFS.mkdir("/apps");
  if (sdcard::begin()) {
    if (!sdcard::fs().exists("/teevee")) sdcard::fs().mkdir("/teevee");
    if (!sdcard::fs().exists(SD_APPS)) sdcard::fs().mkdir(SD_APPS);
  }
  if (!s_images) appImages = s_images = new FileImages;
  {
    Preferences p;
    if (p.begin("tv_bg", true)) {
      p.getString("app", s_bgName, sizeof s_bgName);
      p.end();
    }
    // It starts by itself at every boot, so one that crashes the board must not do so for ever.
    const esp_reset_reason_t why = esp_reset_reason();
    if (s_bgName[0] && (why == ESP_RST_PANIC || why == ESP_RST_INT_WDT || why == ESP_RST_TASK_WDT || why == ESP_RST_WDT)) {
      s_bgBroken = true;
      snprintf(s_bgErr, sizeof s_bgErr, "paused: the board restarted after a crash. Choose it again to run it");
    }
  }
  rescan();
  Serial.printf("apps: %d found, %u of %u bytes free%s\n", s_count, (unsigned)freeBytes(),
                (unsigned)totalBytes(), sdcard::mounted() ? ", SD card in use" : "");
  return true;
}

int count() { return s_count; }
const char* name(int i) { return i >= 0 && i < s_count ? s_names[i] : ""; }
bool exists(const char* n) {
  for (int i = 0; i < s_count; i++)
    if (!strcmp(s_names[i], n)) return true;
  return false;
}
bool onSd(int i) { return i >= 0 && i < s_count && s_sd[i]; }

File open(const char* name) {
  char path[40];
  if (sdcard::mounted() && sdAppPath(name, path, sizeof path) && sdcard::fs().exists(path))
    return sdcard::fs().open(path, "r");
  if (!pathOf(name, path, sizeof path)) return File();
  return LittleFS.open(path, "r");
}

bool sdTmpPath(const char* name, char* buf, size_t n) {
  if (!sdcard::mounted() || !validName(name)) return false;
  snprintf(buf, n, "%s/%s.tmp", SD_APPS, name);
  return true;
}

size_t freeBytes() { return LittleFS.totalBytes() - LittleFS.usedBytes(); }
size_t totalBytes() { return LittleFS.totalBytes(); }

bool imagePaths(const char* n, bool toSd, char* tmp, char* dst, size_t size) {
  if (!validName(n) || (toSd && !sdcard::mounted())) return false;
  const char* dir = toSd ? "/teevee/images" : "/images";
  fs::FS& fs = toSd ? (fs::FS&)sdcard::fs() : (fs::FS&)LittleFS;
  if (!fs.exists(dir)) fs.mkdir(dir);
  snprintf(tmp, size, "%s/%s.tmp", dir, n);
  snprintf(dst, size, "%s/%s.ba", dir, n);
  return true;
}

bool commitImage(const char* n, bool toSd) {
  char tmp[40], dst[40];
  if (!imagePaths(n, toSd, tmp, dst, sizeof tmp)) return false;
  fs::FS& fs = toSd ? (fs::FS&)sdcard::fs() : (fs::FS&)LittleFS;
  if (s_images) {
    xSemaphoreTake(s_images->lock, portMAX_DELAY);
    if (s_images->file) s_images->file.close();
    s_images->valid = false;
    xSemaphoreGive(s_images->lock);
  }
  fs.remove(dst);
  return fs.rename(tmp, dst);
}

bool commitUpload(const char* n, bool toSd) {
  char tmp[40], dst[40];
  bool ok;
  if (!strcmp(n, s_bgName)) {
    bgStop();
    s_bgBroken = false;
    s_bgErr[0] = 0;
  }
  if (toSd) {
    if (!sdTmpPath(n, tmp, sizeof tmp) || !sdAppPath(n, dst, sizeof dst)) return false;
    sdcard::fs().remove(dst);
    ok = sdcard::fs().rename(tmp, dst);
  } else {
    if (!pathOf(n, dst, sizeof dst)) return false;
    snprintf(tmp, sizeof tmp, "/apps/%s.tmp", n);
    LittleFS.remove(dst);
    ok = LittleFS.rename(tmp, dst);
  }
  rescan();
  return ok;
}

bool remove(const char* n) {
  char path[40];
  if (!pathOf(n, path, sizeof path) || !exists(n)) return false;
  if (s_active && !strcmp(s_current, n)) stop();
  if (!strcmp(n, s_bgName)) bgSet("");
  bool ok = LittleFS.remove(path);
  char sdp[40];
  if (sdcard::mounted() && sdAppPath(n, sdp, sizeof sdp) && sdcard::fs().exists(sdp))
    ok = sdcard::fs().remove(sdp) || ok;
  Preferences p;
  if (p.begin("tv_apps", false)) {
    p.remove(n);
    p.end();
  }
  rescan();
  return ok;
}

bool bgSet(const char* n) {
  const bool off = !n[0] || !strcmp(n, "off");
  if (!off && (!validName(n) || !exists(n))) return false;
  bgStop();
  snprintf(s_bgName, sizeof s_bgName, "%s", off ? "" : n);
  s_bgBroken = false;
  s_bgErr[0] = 0;
  Preferences p;
  if (p.begin("tv_bg", false)) {
    p.putString("app", s_bgName);
    p.end();
  }
  return true;
}
const char* bgName() { return s_bgName; }
const char* bgError() { return s_bgErr; }
void bgStep(uint32_t now) {
  if (!s_bgName[0] || s_bgTask || s_task || s_bgBroken || s_active || (int32_t)(now - s_bgHoldUntil) < 0)
    return;
  if (heap_caps_get_free_size(MALLOC_CAP_8BIT) < BG_MIN_HEAP) {
    s_bgHoldUntil = now + 10000;
    return;
  }
  s_bgStopReq = false;
  s_bgDone = false;
  s_bgTask =
      xTaskCreateStaticPinnedToCore(bgTask, "bgapp", STACK_BYTES, nullptr, 1, s_appStack, &s_appTcb, 0);
  if (!s_bgTask) {
    s_bgHoldUntil = now + 10000;
  }
}
void bgYield() {
  if (!s_bgTask) return;
  bgStop();
  s_bgHoldUntil = millis() + 30000;
}
void setUrges(const float* u) {
  for (int i = 0; i < mind::OUT; i++) s_urges[i] = u[i];
  s_urgesKnown = true;
}
bool takeFaceSay(char* text, size_t n) {
  if (!s_sayWanted) return false;
  snprintf(text, n, "%s", s_say);
  s_sayWanted = false;
  return true;
}
bool takeFaceWake() {
  const bool w = s_wakeWanted;
  s_wakeWanted = false;
  return w;
}

bool start(const char* n) {
  if (!validName(n) || !exists(n)) return false;
  bgStop();
  stop();
  if (s_bgTask) return false;
  if (s_task) return false;
  s_stopReq = false;
  s_exitReq = false;
  s_capture = false;
  snprintf(s_current, sizeof s_current, "%.11s", n);
  xQueueReset(s_touchQ);
  s_taskDone = false;
  s_task = xTaskCreateStaticPinnedToCore(appTask, "app", STACK_BYTES, nullptr, 1, s_appStack, &s_appTcb, 0);
  if (!s_task) {
    s_current[0] = 0;
    return false;
  }
  s_active = true;
  return true;
}

void stop() {
  if (s_task) {
    s_stopReq = true;
    for (int i = 0; i < 300 && !s_taskDone; i++) vTaskDelay(pdMS_TO_TICKS(10));
    if (s_taskDone) {
      vTaskDelete(s_task);
      s_task = nullptr;
    } else {
      Serial.println("apps: the app task did not stop in time");
    }
  }
  s_active = false;
  s_current[0] = 0;
  s_exitReq = false;
  s_capture = false;
  s_bgHoldUntil = millis() + BG_AFTER_APP_MS;
}

bool active() { return s_active; }
const char* current() { return s_current; }
bool exitRequested() { return s_exitReq; }
void setNeeds(float attention, float rest, float play) {
  s_needAtt = attention;
  s_needRest = rest;
  s_needPlay = play;
  s_needsKnown = true;
}
void setMood(float v, float a, float d) {
  s_moodV = v;
  s_moodA = a;
  s_moodD = d;
  s_moodKnown = true;
}
bool takeFaceShow(char* name, size_t n, uint32_t& ms) {
  if (!s_faceShowMs) return false;
  snprintf(name, n, "%s", s_faceShow);
  ms = s_faceShowMs;
  s_faceShowMs = 0;
  return true;
}
bool takeFaceLook(float& x, float& y) {
  if (!s_lookWanted) return false;
  x = s_lookX, y = s_lookY;
  s_lookWanted = false;
  return true;
}
uint8_t takeFaceEmote() {
  const uint8_t e = s_faceEmote;
  s_faceEmote = 0;
  return e;
}
bool capturesSwipes() { return s_capture; }
void setLight(float level) { s_light = level; }

void touch(uint8_t kind, int x, int y) {
  if (!s_active || !s_touchQ) return;
  TouchEv e{kind, (int16_t)x, (int16_t)y};
  xQueueSend(s_touchQ, &e, 0);
}

void button(int index, bool down) {
  if (!s_active || !s_touchQ) return;
  TouchEv e{(uint8_t)(BUTTON_EV | (down ? 1 : 0)), (int16_t)index, 0};
  xQueueSend(s_touchQ, &e, 0);
}

bool takeFrame(AppFrame& out, uint32_t& version) {
  if (!s_lock) return false;
  xSemaphoreTake(s_lock, portMAX_DELAY);
  bool fresh = s_version != version;
  if (fresh) {
    copyFrame(out, s_frame);
    version = s_version;
  }
  xSemaphoreGive(s_lock);
  return fresh;
}

int debugInfo(char* buf, size_t n) {
  return snprintf(
      buf, n,
      "app=%s active=%d frame_v=%u cmds=%u text=%u overflow=%d lua_mem=%u stack_free=%u heap=%u min_heap=%u fs_free=%u",
      s_current, s_active, (unsigned)s_version, s_frame.count, s_frame.textLen, s_frame.overflow,
      (unsigned)s_luaMem, s_task ? (unsigned)uxTaskGetStackHighWaterMark(s_task) : 0u,
      (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(), (unsigned)freeBytes());
}
}  // namespace apps
