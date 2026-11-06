#include "brain_internal.h"
#include <Preferences.h>
#include "app_runtime.h"
#include "core/behaviour.h"
#include "core/ba_image.h"
#include "core/pack.h"
#include "sdcard.h"

// Packs on the board (core/pack.h): from the page, tools/pack.py or the card's /teevee/packs. Wi-Fi details, the PIN and the login token live elsewhere and are never read here.
namespace brain {
namespace {
constexpr size_t BLOB_MAX = 8192;

struct FileRead {
    File& f;
    bool operator()(uint32_t off, void* buf, size_t n) const {
      return f.seek(off) && f.read((uint8_t*)buf, n) == n;
    }
};

bool copyOut(File& in, uint32_t offset, uint32_t size, File& out) {
  Scratch buf(1024);
  if (!buf || !in.seek(offset)) return false;
  while (size) {
    const int n = in.read((uint8_t*)buf.p, size < buf.n ? size : buf.n);
    if (n <= 0 || out.write((const uint8_t*)buf.p, n) != (size_t)n) return false;
    size -= n;
    vTaskDelay(1);  // the watchdog and everything else on this core
  }
  return true;
}

bool installApp(File& in, const PackEntry& e) {
  if (e.size < 1 || e.size > apps::MAX_SCRIPT || (!apps::exists(e.name) && apps::count() >= apps::MAX_APPS))
    return false;
  const bool toSd = apps::freeBytes() < e.size + 8192;
  if (toSd && !sdcard::mounted()) return false;
  char tmp[40];
  if (toSd)
    apps::sdTmpPath(e.name, tmp, sizeof tmp);
  else
    snprintf(tmp, sizeof tmp, "/apps/%s.tmp", e.name);
  File out = toSd ? sdcard::fs().open(tmp, FILE_WRITE) : LittleFS.open(tmp, "w");
  const bool ok = out && copyOut(in, e.offset, e.size, out);
  if (out) out.close();
  if (ok) return apps::commitUpload(e.name, toSd);
  if (toSd)
    sdcard::fs().remove(tmp);
  else
    LittleFS.remove(tmp);
  return false;
}

bool installImage(File& in, const PackEntry& e) {
  uint8_t head[BA_HEADER];
  BaInfo info;
  if (e.size < BA_HEADER || !in.seek(e.offset) || in.read(head, sizeof head) != sizeof head ||
      !baParse(head, e.size, info))
    return false;
  const bool toSd = sdcard::mounted();
  if (!toSd && apps::freeBytes() < e.size + 16384) return false;
  char tmp[40], dst[40];
  if (!apps::imagePaths(e.name, toSd, tmp, dst, sizeof tmp)) return false;
  for (int attempt = 0; attempt < 3;
       attempt++) {  // (a card write can fail while Wi-Fi is busy: portal.cpp, cardCopy)
    File out = toSd ? sdcard::fs().open(tmp, FILE_WRITE) : LittleFS.open(tmp, "w");
    const bool ok = out && copyOut(in, e.offset, e.size, out);
    if (out) out.close();
    if (ok) return apps::commitImage(e.name, toSd);
    vTaskDelay(pdMS_TO_TICKS(200));
  }
  return false;
}

bool installBehaviour(File& in, const PackEntry& e) {
  if (e.size < 1 || e.size > 4096) return false;
  Scratch text(e.size);
  Behaviour* check = new Behaviour;
  const bool ok = text && check && in.seek(e.offset) && in.read((uint8_t*)text.p, e.size) == e.size &&
                  behaviourParse(text.p, e.size, *check, nullptr, 0);
  delete check;
  if (!ok) return false;
  File out = LittleFS.open("/behaviour.txt", "w");
  if (!out) return false;
  out.write((const uint8_t*)text.p, e.size);
  out.close();
  return loadBehaviour(nullptr);
}

bool installLook(File& in, const PackEntry& e) {
  Scratch keep(
      STYLE_TEXT_MAX);  // (this runs deep in the brain task's stack: nothing large on the stack in this file)
  char* const text = keep.p;
  if (!text || e.size < 1 || e.size >= keep.n || !in.seek(e.offset) ||
      in.read((uint8_t*)text, e.size) != e.size)
    return false;
  text[e.size] = 0;
  if (!strcmp(e.name, "current")) return applyStyleText(text);
  FaceStyle check = STYLE_DEFAULT;
  if (styleApplyText(check, text) <= 0 || !sdcard::mounted()) return false;
  if (!sdcard::fs().exists("/teevee/looks")) sdcard::fs().mkdir("/teevee/looks");
  char path[40];
  snprintf(path, sizeof path, "/teevee/looks/%s.txt", e.name);
  File out = sdcard::fs().open(path, FILE_WRITE);
  if (!out) return false;
  out.write((const uint8_t*)text, e.size);
  out.close();
  return true;
}

bool installRobot(File& in, const PackEntry& e) {
  if (e.size < 1 || e.size > BLOB_MAX) return false;
  Scratch b(e.size);
  if (!b || !in.seek(e.offset) || in.read((uint8_t*)b.p, e.size) != e.size) return false;
  if (!strcmp(e.kind, "settings")) {
    settingsLoad(settings, b.p, e.size);
    saveSettings();
    return true;
  }
  if (!strcmp(e.kind, "custom")) {
    if (e.size != sizeof custom) return false;
    CustomStore* c = (CustomStore*)b.p;
    if (c->version != CUSTOM_VERSION || c->nFaces > MAX_CUSTOM || c->nEmotes > MAX_CUSTOM) return false;
    memcpy(&custom, c, sizeof custom);
    customSanitize(custom);
    store.putBytes("custom", &custom, sizeof custom);
    return true;
  }
  if (!strcmp(e.kind, "tastes")) return tastes.load(b.p, e.size) && blobWrite("tastes", b.p, e.size);
  if (!strcmp(e.kind, "diary"))
    return e.size == sizeof(Diary::Saved) && diary.load(b.p, e.size) && blobWrite("diary", b.p, e.size);
  if (!strcmp(e.kind, "mind")) return mindNet.load(b.p, e.size) && blobWrite("mind", b.p, e.size);
  if (!strcmp(e.kind, "wardrobe"))
    return e.size <= sizeof(Wardrobe::Saved) && wardrobe.load(b.p, e.size, styleOldPreset) &&
           store.putBytes("wardrobe", b.p, e.size);
  if (!strcmp(e.kind, "appdata")) {
    Preferences p;
    if (e.size > 1024 || !p.begin("tv_apps", false)) return false;
    p.putBytes(e.name, b.p, e.size);
    p.end();
    return true;
  }
  return false;
}
}  // namespace

bool packInstall(fs::FS& fs, const char* path, bool all, char* report, size_t n) {
  File in = fs.open(path, "r");
  if (!in) {
    snprintf(report, n, "cannot open the pack");
    return false;
  }
  const uint32_t size = in.size();
  const FileRead read{in};
  if (!packCheck(read, size)) {
    in.close();
    snprintf(report, n, "not a pack, or a damaged one: nothing was changed");
    return false;
  }
  PackReport rep;
  uint32_t at;
  packOpen(read, size, at);
  PackEntry e;
  int seen = 0;
  while (packNext(read, size, at, e, seen) == PACK_ENTRY) {
    const uint32_t next = at;
    bool ok = false;
    if (packIsRobot(e.kind))
      ok = all && installRobot(in, e);
    else if (!strcmp(e.kind, "app"))
      ok = installApp(in, e);
    else if (!strcmp(e.kind, "image"))
      ok = installImage(in, e);
    else if (!strcmp(e.kind, "look"))
      ok = installLook(in, e);
    else if (!strcmp(e.kind, "behaviour"))
      ok = installBehaviour(in, e);
    else if (!strcmp(e.kind, "about"))
      continue;
    if (ok)
      rep.done(e.kind);
    else
      rep.skip(e.kind);
    at = next;
  }
  in.close();
  rep.text(report, n);
  return true;
}

bool packExport(fs::FS& fs, const char* path, bool all, char* err, size_t n) {
  File out = fs.open(path, FILE_WRITE);
  if (!out) {
    snprintf(err, n, "cannot write the file");
    return false;
  }
  int count = 0;
  bool ok = out.print(PACK_MAGIC) == PACK_MAGIC_LEN;
  const auto head = [&](const char* kind, const char* name, uint32_t size) {
    char line[48];
    const size_t k = packHead(line, sizeof line, kind, name, size);
    ok = ok && out.write((const uint8_t*)line, k) == k;
    count++;
  };
  const auto bytes = [&](const char* kind, const char* name, const void* data, uint32_t size) {
    head(kind, name, size);
    ok = ok && out.write((const uint8_t*)data, size) == size && out.write((uint8_t)'\n') == 1;
  };
  const auto file = [&](const char* kind, const char* name, File& in) {
    head(kind, name, in.size());
    ok = ok && copyOut(in, 0, in.size(), out) && out.write((uint8_t)'\n') == 1;
  };
  Scratch work(1024 > STYLE_TEXT_MAX ? 1024 : STYLE_TEXT_MAX);
  if (!work) {
    out.close();
    fs.remove(path);
    snprintf(err, n, "not enough memory");
    return false;
  }
  styleDescribe(style, work.p, work.n);
  bytes("look", "current", work.p, strlen(work.p));
  File bf = sdcard::mounted() && sdcard::fs().exists("/teevee/behaviour.txt")
                ? sdcard::fs().open("/teevee/behaviour.txt", "r")
                : File();
  if (!bf && LittleFS.exists("/behaviour.txt")) bf = LittleFS.open("/behaviour.txt", "r");
  if (bf) {
    file("behaviour", "main", bf);
    bf.close();
  }
  for (int i = 0; ok && i < apps::count(); i++)
    if (File a = apps::open(apps::name(i))) {
      file("app", apps::name(i), a);
      a.close();
    }
  // Pictures and saved looks, by their file names (a card's picture wins over one of that name in flash).
  const auto folder = [&](fs::FS& from, const char* dirPath, const char* kind, const char* ext,
                          bool skipIfOnCard) {
    File dir = from.open(dirPath);
    if (!dir || !dir.isDirectory()) return;
    for (File f = dir.openNextFile(); ok && f; f = dir.openNextFile()) {
      String name = f.name();
      name = name.substring(name.lastIndexOf('/') + 1);
      if (f.isDirectory() || !name.endsWith(ext)) continue;
      name.remove(name.length() - strlen(ext));
      if (!packNameOk(name.c_str())) continue;
      if (skipIfOnCard && sdcard::mounted() && sdcard::fs().exists(String("/teevee/images/") + name + ext))
        continue;
      file(kind, name.c_str(), f);
    }
  };
  if (sdcard::mounted()) {
    folder(sdcard::fs(), "/teevee/images", "image", ".ba", false);
    folder(sdcard::fs(), "/teevee/looks", "look", ".txt", false);
  }
  folder(LittleFS, "/images", "image", ".ba", true);
  if (all) {
    bytes("settings", "main", &settings, sizeof settings);
    bytes("custom", "main", &custom, sizeof custom);
    if (Tastes::Saved* t = new Tastes::Saved(tastes.save())) {
      bytes("tastes", "main", t, sizeof *t);
      delete t;
    }
    bytes("diary", "main", &diary.saved(), sizeof(Diary::Saved));
    bytes("mind", "main", &mindNet.weights(), sizeof(MindNet::Weights));
    const Wardrobe::Saved w = wardrobe.save(STYLE_PRESET_COUNT + 1);
    bytes("wardrobe", "main", &w, sizeof w);
    Preferences p;
    if (p.begin("tv_apps", true)) {
      for (int i = 0; ok && i < apps::count(); i++) {
        const size_t k = p.getBytesLength(apps::name(i));
        if (k && k <= work.n && p.getBytes(apps::name(i), work.p, k) == k)
          bytes("appdata", apps::name(i), work.p, k);
      }
      p.end();
    }
  }
  char end[24];
  const size_t k = snprintf(end, sizeof end, "end %d\n", count);
  ok = ok && out.write((const uint8_t*)end, k) == k;
  out.close();
  if (!ok) {
    fs.remove(path);
    snprintf(err, n, "not enough room for the export");
  }
  return ok;
}
}  // namespace brain
