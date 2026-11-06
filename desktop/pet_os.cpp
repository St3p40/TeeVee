#include "pet_os.h"
#include <stdio.h>
#include <string.h>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace pet {
namespace {
struct Enum {
    HWND self;
    PetRect* out;
    int max, n;
};
using DwmGet = HRESULT(WINAPI*)(HWND, DWORD, PVOID, DWORD);
DwmGet dwmGet() {
  static DwmGet f = nullptr;
  static bool tried = false;
  if (!tried) {
    tried = true;
    if (HMODULE m = LoadLibraryA("dwmapi.dll")) f = (DwmGet)(void*)GetProcAddress(m, "DwmGetWindowAttribute");
  }
  return f;
}
BOOL CALLBACK onWindow(HWND w, LPARAM lp) {
  Enum& e = *(Enum*)lp;
  if (e.n >= e.max) return FALSE;
  if (w == e.self || !IsWindowVisible(w) || IsIconic(w)) return TRUE;
  const LONG_PTR ex = GetWindowLongPtrA(w, GWL_EXSTYLE);
  if (ex & WS_EX_TOOLWINDOW) return TRUE;
  if (GetAncestor(w, GA_ROOTOWNER) != w && !(ex & WS_EX_APPWINDOW)) return TRUE;
  RECT r;
  if (DwmGet f = dwmGet()) {
    DWORD cloaked = 0;
    if (f(w, 14, &cloaked, sizeof cloaked) == S_OK && cloaked) return TRUE;
    if (f(w, 9, &r, sizeof r) != S_OK) GetWindowRect(w, &r);
  } else {
    GetWindowRect(w, &r);
  }
  if (r.right - r.left <= 0 || r.bottom - r.top <= 0) return TRUE;
  e.out[e.n++] = {(float)r.left, (float)r.top, (float)(r.right - r.left), (float)(r.bottom - r.top)};
  return TRUE;
}
const wchar_t* RUN_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
}  // namespace

int otherWindows(SDL_Window* self, PetRect* out, int max) {
  Enum e{(HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(self), SDL_PROP_WINDOW_WIN32_HWND_POINTER,
                                      nullptr),
         out, max, 0};
  EnumWindows(onWindow, (LPARAM)&e);
  return e.n;
}

bool screenThumb(uint8_t* grey, int w, int h) {
  HDC scr = GetDC(nullptr);
  if (!scr) return false;
  const int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
  HDC mem = CreateCompatibleDC(scr);
  BITMAPINFO bi = {};
  bi.bmiHeader.biSize = sizeof bi.bmiHeader;
  bi.bmiHeader.biWidth = w, bi.bmiHeader.biHeight = -h, bi.bmiHeader.biPlanes = 1,
  bi.bmiHeader.biBitCount = 32;
  void* bits = nullptr;
  HBITMAP bmp = mem ? CreateDIBSection(scr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
  bool ok = false;
  if (bmp && bits) {
    HGDIOBJ old = SelectObject(mem, bmp);
    SetStretchBltMode(mem, HALFTONE);
    SetBrushOrgEx(mem, 0, 0, nullptr);
    ok = StretchBlt(mem, 0, 0, w, h, scr, 0, 0, sw, sh, SRCCOPY) != 0;
    GdiFlush();
    for (int i = 0; ok && i < w * h; i++) {
      const uint32_t px = ((const uint32_t*)bits)[i];
      grey[i] = (uint8_t)((((px >> 16) & 255) * 77 + ((px >> 8) & 255) * 150 + (px & 255) * 29) >> 8);
    }
    SelectObject(mem, old);
  }
  if (bmp) DeleteObject(bmp);
  if (mem) DeleteDC(mem);
  ReleaseDC(nullptr, scr);
  return ok;
}

bool autostartSupported() { return true; }
bool autostartOn() {
  HKEY k;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_READ, &k) != ERROR_SUCCESS) return false;
  const bool on = RegQueryValueExW(k, L"Teevee", nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
  RegCloseKey(k);
  return on;
}
bool setAutostart(bool on) {
  HKEY k;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) !=
      ERROR_SUCCESS)
    return false;
  bool ok;
  if (on) {
    wchar_t exe[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring cmd = L"\"" + std::wstring(exe, n) + L"\" --pet";
    ok = n > 0 && n < MAX_PATH &&
         RegSetValueExW(k, L"Teevee", 0, REG_SZ, (const BYTE*)cmd.c_str(),
                        (DWORD)((cmd.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
  } else {
    const LONG r = RegDeleteValueW(k, L"Teevee");
    ok = r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND;
  }
  RegCloseKey(k);
  return ok;
}
}  // namespace pet

#elif defined(__linux__)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

namespace pet {
namespace {
struct X {
    bool ok = false;
    decltype(&XOpenDisplay) open;
    decltype(&XInternAtom) atom;
    decltype(&XGetWindowProperty) prop;
    decltype(&XGetWindowAttributes) attrs;
    decltype(&XTranslateCoordinates) translate;
    decltype(&XFree) xfree;
    decltype(&XDefaultRootWindow) root;
    decltype(&XGetImage) image;
    Display* d = nullptr;
};
X& x11() {
  static X x;
  static bool tried = false;
  if (tried) return x;
  tried = true;
  const char* drv = SDL_GetCurrentVideoDriver();
  if (!drv || strcmp(drv, "x11")) return x;  // Wayland: windows cannot be listed
  void* h = dlopen("libX11.so.6", RTLD_NOW | RTLD_LOCAL);
  if (!h) return x;
  x.open = (decltype(x.open))dlsym(h, "XOpenDisplay");
  x.atom = (decltype(x.atom))dlsym(h, "XInternAtom");
  x.prop = (decltype(x.prop))dlsym(h, "XGetWindowProperty");
  x.attrs = (decltype(x.attrs))dlsym(h, "XGetWindowAttributes");
  x.translate = (decltype(x.translate))dlsym(h, "XTranslateCoordinates");
  x.xfree = (decltype(x.xfree))dlsym(h, "XFree");
  x.root = (decltype(x.root))dlsym(h, "XDefaultRootWindow");
  x.image = (decltype(x.image))dlsym(h, "XGetImage");
  if (!x.open || !x.atom || !x.prop || !x.attrs || !x.translate || !x.xfree || !x.root) return x;
  x.d = x.open(nullptr);
  x.ok = x.d != nullptr;
  return x;
}
long* longs(X& x, Window w, const char* name, unsigned long& n) {
  Atom type;
  int format;
  unsigned long after;
  unsigned char* data = nullptr;
  n = 0;
  if (x.prop(x.d, w, x.atom(x.d, name, False), 0, 1024, False, AnyPropertyType, &type, &format, &n, &after,
             &data) != Success ||
      !data)
    return nullptr;
  if (format != 32) {
    x.xfree(data);
    n = 0;
    return nullptr;
  }
  return (long*)data;
}
bool hasAtom(X& x, Window w, const char* prop, const char* value) {
  unsigned long n;
  long* v = longs(x, w, prop, n);
  if (!v) return false;
  const Atom a = x.atom(x.d, value, False);
  bool found = false;
  for (unsigned long i = 0; i < n; i++) found = found || (Atom)v[i] == a;
  x.xfree(v);
  return found;
}
std::string autostartFile() {
  const char* cfg = getenv("XDG_CONFIG_HOME");
  const char* home = getenv("HOME");
  if (cfg && *cfg) return std::string(cfg) + "/autostart/teevee.desktop";
  return home ? std::string(home) + "/.config/autostart/teevee.desktop" : std::string();
}
}  // namespace

int otherWindows(SDL_Window* self, PetRect* out, int max) {
  X& x = x11();
  if (!x.ok) return 0;
  const Window root = x.root(x.d), me = (Window)SDL_GetNumberProperty(SDL_GetWindowProperties(self),
                                                                      SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
  unsigned long n;
  long* list = longs(x, root, "_NET_CLIENT_LIST_STACKING", n);
  if (!list) return 0;
  int k = 0;
  for (long i = (long)n - 1; i >= 0 && k < max; i--) {
    const Window w = (Window)list[i];
    if (w == me) continue;
    XWindowAttributes a;
    if (!x.attrs(x.d, w, &a) || a.map_state != IsViewable) continue;
    if (hasAtom(x, w, "_NET_WM_STATE", "_NET_WM_STATE_HIDDEN")) continue;
    if (hasAtom(x, w, "_NET_WM_WINDOW_TYPE", "_NET_WM_WINDOW_TYPE_DESKTOP") ||
        hasAtom(x, w, "_NET_WM_WINDOW_TYPE", "_NET_WM_WINDOW_TYPE_DOCK"))
      continue;
    int rx = 0, ry = 0;
    Window child;
    x.translate(x.d, w, root, 0, 0, &rx, &ry, &child);
    float l = 0, r = 0, t = 0, b = 0;
    unsigned long ne;
    if (long* fe = longs(x, w, "_NET_FRAME_EXTENTS", ne)) {
      if (ne >= 4) l = (float)fe[0], r = (float)fe[1], t = (float)fe[2], b = (float)fe[3];
      x.xfree(fe);
    }
    out[k++] = {rx - l, ry - t, a.width + l + r, a.height + t + b};
  }
  x.xfree(list);
  return k;
}

bool screenThumb(uint8_t* grey, int w, int h) {
  X& x = x11();
  if (!x.ok || !x.image) return false;
  const Window root = x.root(x.d);
  XWindowAttributes a;
  if (!x.attrs(x.d, root, &a) || a.width <= 0 || a.height <= 0) return false;
  XImage* im = x.image(x.d, root, 0, 0, a.width, a.height, AllPlanes, ZPixmap);
  if (!im) return false;
  const bool ok = im->bits_per_pixel == 32 && im->data;
  for (int y = 0; ok && y < h; y++)
    for (int xx = 0; xx < w; xx++) {
      unsigned sum = 0;
      for (int sy = 0; sy < 4; sy++)
        for (int sx = 0; sx < 4; sx++) {
          const int px = (xx * 4 + sx) * a.width / (w * 4) + a.width / (w * 8),
                    py = (y * 4 + sy) * a.height / (h * 4) + a.height / (h * 8);
          const uint32_t v = *(const uint32_t*)(im->data + (size_t)py * im->bytes_per_line + (size_t)px * 4);
          sum += (((v >> 16) & 255) * 77 + ((v >> 8) & 255) * 150 + (v & 255) * 29) >> 8;
        }
      grey[y * w + xx] = (uint8_t)(sum / 16);
    }
  XDestroyImage(im);
  return ok;
}

bool autostartSupported() { return !autostartFile().empty(); }
bool autostartOn() {
  const std::string f = autostartFile();
  return !f.empty() && access(f.c_str(), F_OK) == 0;
}
bool setAutostart(bool on) {
  const std::string f = autostartFile();
  if (f.empty()) return false;
  if (!on) return remove(f.c_str()) == 0 || access(f.c_str(), F_OK) != 0;
  char exe[4096];
  const ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
  if (n <= 0) return false;
  exe[n] = 0;
  const std::string dir = f.substr(0, f.rfind('/'));
  mkdir(dir.substr(0, dir.rfind('/')).c_str(), 0700);
  mkdir(dir.c_str(), 0700);
  FILE* o = fopen(f.c_str(), "w");
  if (!o) return false;
  fprintf(
      o,
      "[Desktop Entry]\nType=Application\nName=TeeVee\nComment=A little robot face on the desktop\nExec=\"%s\" --pet\nTerminal=false\nX-GNOME-Autostart-enabled=true\n",
      exe);
  return fclose(o) == 0;
}
}  // namespace pet

#elif defined(__APPLE__)
#include <CoreGraphics/CoreGraphics.h>
#include <limits.h>
#include <mach-o/dyld.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

namespace pet {
namespace {
std::string agentFile() {
  const char* home = getenv("HOME");
  return home ? std::string(home) + "/Library/LaunchAgents/local.teevee.pet.plist" : std::string();
}
int number(CFDictionaryRef d, CFStringRef key) {
  int v = 0;
  if (CFNumberRef n = (CFNumberRef)CFDictionaryGetValue(d, key)) CFNumberGetValue(n, kCFNumberIntType, &v);
  return v;
}
}  // namespace

// The windows on screen, front-most first (CGWindowList's order), in global points with the origin at the main display's top left, as SDL's.
// Their frames need no screen-recording permission (only window names would).
int otherWindows(SDL_Window*, PetRect* out, int max) {
  CFArrayRef list = CGWindowListCopyWindowInfo(
      kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID);
  if (!list) return 0;
  const int me = (int)getpid();
  int k = 0;
  for (CFIndex i = 0; i < CFArrayGetCount(list) && k < max; i++) {
    CFDictionaryRef w = (CFDictionaryRef)CFArrayGetValueAtIndex(list, i);
    if (number(w, kCGWindowLayer) != 0 || number(w, kCGWindowOwnerPID) == me) continue;
    CFDictionaryRef b = (CFDictionaryRef)CFDictionaryGetValue(w, kCGWindowBounds);
    CGRect r;
    if (!b || !CGRectMakeWithDictionaryRepresentation(b, &r)) continue;
    out[k++] = {(float)r.origin.x, (float)r.origin.y, (float)r.size.width, (float)r.size.height};
  }
  CFRelease(list);
  return k;
}

bool autostartSupported() { return !agentFile().empty(); }
bool autostartOn() {
  const std::string f = agentFile();
  return !f.empty() && access(f.c_str(), F_OK) == 0;
}
bool screenThumb(uint8_t*, int, int) {
  return false;
}  // (reading the screen needs Screen Recording permission: never asked for)

bool setAutostart(bool on) {
  const std::string f = agentFile();
  if (f.empty()) return false;
  if (!on) return remove(f.c_str()) == 0 || access(f.c_str(), F_OK) != 0;
  char exe[4096];
  uint32_t size = sizeof exe;
  if (_NSGetExecutablePath(exe, &size) != 0) return false;
  char real[PATH_MAX];
  std::string path;
  for (const char* c = realpath(exe, real) ? real : exe; *c; c++)
    path += *c == '&' ? "&amp;" : *c == '<' ? "&lt;" : *c == '>' ? "&gt;" : std::string(1, *c);
  mkdir(f.substr(0, f.rfind('/')).c_str(), 0755);
  FILE* o = fopen(f.c_str(), "w");
  if (!o) return false;
  fprintf(
      o,
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
      "<plist version=\"1.0\"><dict>\n"
      "  <key>Label</key><string>local.teevee.pet</string>\n"
      "  <key>ProgramArguments</key><array><string>%s</string><string>--pet</string></array>\n"
      "  <key>RunAtLoad</key><true/>\n"
      "</dict></plist>\n",
      path.c_str());
  return fclose(o) == 0;
}
}  // namespace pet

#else

namespace pet {
int otherWindows(SDL_Window*, PetRect*, int) { return 0; }
bool screenThumb(uint8_t*, int, int) { return false; }
bool autostartSupported() { return false; }
bool autostartOn() { return false; }
bool setAutostart(bool) { return false; }
}  // namespace pet
#endif
