#include "brain_internal.h"
#include "sdcard.h"
#include "app_runtime.h"
#include <esp_ota_ops.h>

// What lives on the SD card (all optional, everything under /teevee/ so the card's other files are never touched):
//   apps/<name>.lua       scripts (app_runtime.cpp)
//   backup/<key>.bin      copies of what is saved in the board's flash: look, settings, custom faces, the mind net, needs, diary
//   log/diary.csv         one line per finished day, kept for as long as the card has room
//   looks/<name>.txt      saved looks, as the text the `style` command takes
//   shots/shotNNN.ba      screenshots (Bitmap Array files; sdcard.cpp)
//   images/<name>.ba      pictures and animations for apps (gfx.image; app_runtime.cpp)
//   store/<name>.lua      ready apps to install: copy them there from a PC
//   update/firmware.bin   a firmware to install (`sd update`, or the page)
namespace brain {
namespace {
const char* const KEYS[] = {"style", "settings", "custom", "needs"};
const char BACKUP_DIR[] = "/teevee/backup", LOG_DIR[] = "/teevee/log", LOOKS_DIR[] = "/teevee/looks";

bool ensureDir(const char* path) {
  if (!sdcard::mounted()) return false;
  if (sdcard::fs().exists(path)) return true;
  return sdcard::fs().mkdir(path);
}

bool validLookName(const char* n) {
  size_t len = n ? strlen(n) : 0;
  if (len < 1 || len > 11) return false;
  for (const char* p = n; *p; p++)
    if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '-')) return false;
  return true;
}
}  // namespace

// Writes what is saved in flash (after saving the live mind net / needs / diary) to /teevee/backup/. Returns the number of files written.
int sdBackup() {
  if (!sdcard::mounted() || !ensureDir("/teevee") || !ensureDir(BACKUP_DIR)) return -1;
  const Needs::Saved ns = needs.save();
  store.putBytes("needs", &ns, sizeof ns);
  int n = 0;
  auto put = [&](const char* key, const void* data, size_t len) {
    char path[40];
    snprintf(path, sizeof path, "%s/%s.bin", BACKUP_DIR, key);
    File f = sdcard::fs().open(path, FILE_WRITE);
    if (!f) return;
    n += f.write((const uint8_t*)data, len) == len;
    f.close();
  };
  put("mind", &mindNet.weights(), sizeof(MindNet::Weights));
  put("diary", &diary.saved(), sizeof(Diary::Saved));
  {
    if (Tastes::Saved* tast = new Tastes::Saved(tastes.save())) {
      put("tastes", tast, sizeof *tast);
      delete tast;
    }
  }
  for (const char* key : KEYS) {
    const size_t len = store.getBytesLength(key);
    if (!len) continue;
    Scratch buf(len);
    if (!buf) continue;
    store.getBytes(key, buf.p, len);
    char path[40];
    snprintf(path, sizeof path, "%s/%s.bin", BACKUP_DIR, key);
    File f = sdcard::fs().open(path, FILE_WRITE);
    if (!f) continue;
    n += f.write((const uint8_t*)buf.p, len) == len;
    f.close();
  }
  return n;
}

// Puts the backed up files back into flash. The caller restarts the board so everything loads from there. Returns the number restored.
int sdRestore() {
  if (!sdcard::mounted()) return -1;
  int n = 0;
  for (const char* key : {"mind", "diary", "tastes"}) {  // files in the flash filesystem
    char path[40];
    snprintf(path, sizeof path, "%s/%s.bin", BACKUP_DIR, key);
    File f = sdcard::fs().open(path, "r");
    if (!f) continue;
    const size_t len = f.size();
    Scratch buf(len);
    if (buf && len && len <= 8192 && f.read((uint8_t*)buf.p, len) == len) n += blobWrite(key, buf.p, len);
    f.close();
  }
  for (const char* key : KEYS) {
    char path[40];
    snprintf(path, sizeof path, "%s/%s.bin", BACKUP_DIR, key);
    File f = sdcard::fs().open(path, "r");
    if (!f) continue;
    const size_t len = f.size();
    Scratch buf(len);
    if (buf && len && len <= 8192 && f.read((uint8_t*)buf.p, len) == len)
      n += store.putBytes(key, buf.p, len) == len;
    f.close();
  }
  return n;
}

void sdLogDiaryDay(const Diary::Day& d) {
  if (!sdcard::mounted() || !ensureDir("/teevee") || !ensureDir(LOG_DIR)) return;
  char path[40];
  snprintf(path, sizeof path, "%s/diary.csv", LOG_DIR);
  const bool fresh = !sdcard::fs().exists(path);
  File f = sdcard::fs().open(path, FILE_APPEND);
  if (!f) return;
  if (fresh) f.println("day,pet,poke,praise,scold,greet,love,annoy,win,mood");
  int sum = 0, cnt = 0;
  for (int h = 0; h < Diary::HOURS; h++)
    if (d.moodN[h]) sum += d.moodSum[h] / d.moodN[h], cnt++;
  f.printf("%lu", (unsigned long)d.id);
  for (int k = 0; k < Diary::KINDS; k++) f.printf(",%u", d.n[k]);
  if (cnt)
    f.printf(",%d\n", sum / cnt);
  else
    f.println(",");
  f.close();
}

// Installs /teevee/update/firmware.bin into the spare slot, checked as a web upload is (the old firmware stays if anything is wrong), then restarts.
// Only on request, and the file is renamed first thing after it took, so it can never loop.
const char* sdUpdate() {
  const char PATH[] = "/teevee/update/firmware.bin";
  if (!sdcard::mounted()) return "no card";
  File f = sdcard::fs().open(PATH, FILE_READ);
  if (!f) return "no /teevee/update/firmware.bin";
  const esp_partition_t* part = esp_ota_get_next_update_partition(nullptr);
  if (!part) return "no spare slot";
  const size_t size = f.size();
  if (size < 4096 || size > part->size) return "wrong size for a firmware";
  if (f.peek() != 0xE9) return "not a firmware file";
  esp_ota_handle_t h = 0;
  if (esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &h) != ESP_OK) return "could not open the spare slot";
  Scratch buf(1024);
  if (!buf) {
    esp_ota_abort(h);
    return "no memory";
  }
  size_t left = size;
  while (left) {
    const int n = f.read((uint8_t*)buf.p, min(left, (size_t)buf.n));
    if (n <= 0 || esp_ota_write(h, buf.p, n) != ESP_OK) {
      esp_ota_abort(h);
      return "read or write failed: the old firmware stays";
    }
    left -= n;
    vTaskDelay(1);
  }
  f.close();
  if (esp_ota_end(h) != ESP_OK) return "the file failed its check: the old firmware stays";
  if (esp_ota_set_boot_partition(part) != ESP_OK) return "could not switch: the old firmware stays";
  sdcard::fs().remove("/teevee/update/firmware.old");
  sdcard::fs().rename(PATH, "/teevee/update/firmware.old");
  Serial.println("sd: firmware installed, restarting");
  delay(300);
  ESP.restart();
  return nullptr;
}

size_t sdDiaryLog(char* out, size_t n) {
  Scratch csv(1400);
  size_t got = 0;
  if (csv && sdcard::mounted()) {
    File f = sdcard::fs().open("/teevee/log/diary.csv", FILE_READ);
    if (f) {
      const size_t size = f.size();
      f.seek(size > csv.n - 1 ? size - (csv.n - 1) : 0);
      got = f.read((uint8_t*)csv.p, csv.n - 1);
      f.close();
    }
  }
  return diaryLogJson(csv ? (const char*)csv.p : "", got, out, n);
}

size_t sdStoreJson(char* out, size_t n) {
  size_t k = snprintf(out, n, "{\"store\":[");
  File dir = sdcard::mounted() ? sdcard::fs().open("/teevee/store") : File();
  bool first = true;
  if (dir && dir.isDirectory())
    for (File e = dir.openNextFile(); e && k + 20 < n; e = dir.openNextFile()) {
      String name = e.name();
      name = name.substring(name.lastIndexOf('/') + 1);
      if (e.isDirectory() || !name.endsWith(".lua")) continue;
      name.remove(name.length() - 4);
      if (!apps::validName(name.c_str())) continue;
      k += snprintf(out + k, n - k, "%s\"%s\"", first ? "" : ",", name.c_str());
      first = false;
    }
  return k + snprintf(out + k, n - k, "]}");
}

bool sdStoreInstall(const char* name) {
  if (!sdcard::mounted() || !apps::validName(name)) return false;
  char from[48], tmp[40];
  snprintf(from, sizeof from, "/teevee/store/%s.lua", name);
  File in = sdcard::fs().open(from, FILE_READ);
  if (!in || in.size() < 1 || in.size() > apps::MAX_SCRIPT || apps::freeBytes() < in.size() + 8192)
    return false;
  if (!apps::exists(name) && apps::count() >= apps::MAX_APPS) return false;
  snprintf(tmp, sizeof tmp, "/apps/%s.tmp", name);
  File out = LittleFS.open(tmp, "w");
  Scratch buf(1024);
  bool ok = out && buf;
  while (ok && in.available()) {
    const int n = in.read((uint8_t*)buf.p, buf.n);
    ok = n > 0 && out.write((const uint8_t*)buf.p, n) == (size_t)n;
  }
  if (out) out.close();
  in.close();
  if (!ok) {
    LittleFS.remove(tmp);
    return false;
  }
  return apps::commitUpload(name, false);
}

//==============================
// looks
//==============================
int sdLooksJson(char* out, size_t n) {
  size_t k = snprintf(out, n, "{\"sdok\":%s,\"looks\":[", sdcard::mounted() ? "true" : "false");
  bool first = true;
  if (sdcard::mounted()) {
    File dir = sdcard::fs().open(LOOKS_DIR);
    if (dir && dir.isDirectory())
      while (File f = dir.openNextFile()) {
        String nm = f.name();
        int slash = nm.lastIndexOf('/');
        if (slash >= 0) nm = nm.substring(slash + 1);
        if (!f.isDirectory() && nm.endsWith(".txt") && k + 20 < n) {
          nm = nm.substring(0, nm.length() - 4);
          if (validLookName(nm.c_str()))
            k += snprintf(out + k, n - k, "%s\"%s\"", first ? "" : ",", nm.c_str()), first = false;
        }
        f.close();
      }
  }
  k += snprintf(out + k, n - k, "]}");
  return (int)k;
}

bool sdLookSave(const char* name) {
  if (!validLookName(name) || !ensureDir("/teevee") || !ensureDir(LOOKS_DIR)) return false;
  char path[40], text[STYLE_TEXT_MAX];
  snprintf(path, sizeof path, "%s/%s.txt", LOOKS_DIR, name);
  styleDescribe(style, text, sizeof text);
  File f = sdcard::fs().open(path, FILE_WRITE);
  if (!f) return false;
  f.println(text);
  f.close();
  return true;
}

bool sdLookLoad(const char* name) {
  if (!validLookName(name) || !sdcard::mounted()) return false;
  char path[40];
  snprintf(path, sizeof path, "%s/%s.txt", LOOKS_DIR, name);
  File f = sdcard::fs().open(path, "r");
  if (!f) return false;
  String text = f.readStringUntil('\n');
  f.close();
  text.trim();
  return text.length() && applyStyleText(text.c_str());
}

bool sdLookDelete(const char* name) {
  if (!validLookName(name) || !sdcard::mounted()) return false;
  char path[40];
  snprintf(path, sizeof path, "%s/%s.txt", LOOKS_DIR, name);
  return sdcard::fs().remove(path);
}
}  // namespace brain
