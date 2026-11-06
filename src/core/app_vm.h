#pragma once
#include <stddef.h>
#include <stdint.h>
#include "app_cmds.h"

struct lua_State;

struct AppTime {
    int hour, min, sec, day, month, year, wday;
};

class AppPlatform {
  public:
    virtual ~AppPlatform() {}
    virtual uint32_t millis() = 0;
    virtual bool localTime(AppTime& t) = 0;
    virtual float light() = 0;
    virtual size_t kvLoad(const char* app, char* buf, size_t cap) = 0;
    virtual void kvSave(const char* app, const char* buf, size_t len) = 0;
    virtual bool httpStart(const char* app, int id, const char* url) = 0;
    virtual void log(const char* app, const char* msg) = 0;
    virtual bool motion(float v[6]) = 0;
    virtual void requestExit() = 0;
    virtual void setCapture(bool on) = 0;
    virtual void faceEvent(int type, float intensity) = 0;
    virtual void faceEmote(int emote) = 0;
    virtual bool faceMood(float& v, float& a, float& d) = 0;
    virtual bool faceNeeds(float& attention, float& rest, float& play) { return false; }
    virtual void faceShow(const char* anchor, float seconds) {}
    virtual void faceSay(const char* text) {}                 // face.say(text): a few letters beside the face
  virtual bool faceUrge(int index, float& value) { return false; }  // face.urge(name): the mind net's wish, -1..1 (mind.h)
  virtual void faceWake() {}
    virtual void faceLook(float x, float y) {}
    virtual bool buttons() { return false; }
    virtual void visibleColumns(int& x0, int& x1) { x0 = 0, x1 = APP_W; }
};

struct AppLimits {
    size_t memory = 40 * 1024;  // Lua heap
    uint32_t callMs = 40;
    uint32_t startMs = 250;
    bool background = false;  // runs background(dt) instead of update and draw: nothing on the screen
};

class AppVM {
  public:
    using Limits = AppLimits;

    explicit AppVM(AppPlatform* platform, AppLimits limits = AppLimits());
    ~AppVM();

    bool start(const char* name, const char* source, size_t len, char* err, size_t errCap);
    void stop();

    bool running() const { return L_ && !failed_; }
    bool failed() const { return failed_; }
    const char* error() const { return err_; }
    size_t memoryUsed() const { return used_; }

    bool tick(float dt);
    void touch(AppTouch kind, int x, int y);
    void button(int index, bool down);
    void httpResult(int id, int status, const char* body, size_t len);  // status <= 0: failed
    const AppFrame& frame() const { return frame_; }

  private:
    friend struct AppApi;
    static void* allocator(void* ud, void* ptr, size_t osize, size_t nsize);
    struct Call;
    bool protectedCall(Call& c, uint32_t budgetMs);
    void fail(const char* msg);
    void buildErrorFrame();
    bool kvFlush(bool force);

    AppPlatform* plat_;
    Limits lim_;
    lua_State* L_ = nullptr;
    size_t used_ = 0;
    char name_[16] = "";
    char err_[160] = "";
    bool failed_ = false, dirty_ = true, errorShown_ = false;
    uint32_t deadline_ = 0, startedAt_ = 0, kvSavedAt_ = 0;
    int nextHttp_ = 1, httpPending_ = 0;
    float evTokens_ = 3, emoteTokens_ = 1;
    uint32_t tokenMs_ = 0;
    AppFrame frame_;

    struct KV {
        char key[16];
        char val[48];
        char type;
    } kv_[16];
    int kvCount_ = 0;
    bool kvDirty_ = false;
};
