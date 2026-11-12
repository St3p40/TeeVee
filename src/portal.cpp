#include "portal.h"
#include "core/scratch.h"
#include <WiFi.h>
#include <LittleFS.h>
#include <WiFiUdp.h>
#include <esp_ota_ops.h>
#include "app_runtime.h"
#include "sdcard.h"
#include "core/ba_image.h"
#include "core/pack.h"
#include "core/json.h"
#include "net.h"
#include "portal_page_gz.h"
#include "portal_proto.h"
#include "sock_util.h"

namespace brain {
bool loadBehaviour(char* err);
bool packInstall(fs::FS& fs, const char* path, bool all, char* report, size_t n);
bool packExport(fs::FS& fs, const char* path, bool all, char* err, size_t n);
}  // namespace brain

namespace {
constexpr int MAX_HTTP = 2;
constexpr size_t MAX_BODY = 1024;
constexpr uint32_t HTTP_TIMEOUT_MS = 5000;

struct HttpClient {
    WiFiClient sock;
    bool open = false;
    // Firmware upload (POST /api/ota) streams straight into the spare app slot instead of the buffer.
    bool ota = false;
    esp_ota_handle_t otaHandle = 0;
    const esp_partition_t* otaPart = nullptr;
    size_t otaLeft = 0, otaTotal = 0;
    bool up = false;
    File upFile;
    char upName[12] = "";
    bool upSd = false;
    int8_t upPack = 0;
    bool upBehaviour = false;
    bool upImage = false;
    size_t upLeft = 0;
    uint32_t since = 0;
    size_t len = 0;
    char buf[2048 + MAX_BODY + 1];
};

WiFiServer http(80);
WiFiUDP dns;
bool dnsOn = false;
HttpClient clients[MAX_HTTP];
int s_otaPct = -1;
uint32_t s_rebootAt = 0;

void respond(HttpClient& c, int code, const char* type, const char* body, size_t len,
             const char* extra = "") {
  const char* reason = code == 200   ? "OK"
                       : code == 302 ? "Found"
                       : code == 400 ? "Bad Request"
                       : code == 401 ? "Unauthorized"
                                     : "Not Found";
  char head[256];
  int n = snprintf(head, sizeof head,
                   "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\nCache-Control: no-store\r\n"
                   "Connection: close\r\n%s\r\n",
                   code, reason, type, (unsigned)len, extra);
  if (!sendAll(c.sock, (const uint8_t*)head, n, 400) ||
      (len && !sendAll(c.sock, (const uint8_t*)body, len, len > 4000 ? 3000 : 400)))
    c.sock.stop();
}

void json(HttpClient& c, const char* body) { respond(c, 200, "application/json", body, strlen(body)); }

bool authorized(HttpClient& c, const portal::Request& r, bool hotspot) {
  IPAddress peer = c.sock.remoteIP(), ap = WiFi.softAPIP(), sta = WiFi.localIP();
  const bool sameSubnet = ap[0] == sta[0] && ap[1] == sta[1] && ap[2] == sta[2];
  const bool viaHotspot = hotspot && !sameSubnet && peer[0] == ap[0] && peer[1] == ap[1] && peer[2] == ap[2];
  return viaHotspot || netTokenOk(r.token);
}

void otaFail(HttpClient& c, const char* why) {
  if (c.otaHandle) esp_ota_abort(c.otaHandle);
  c.otaHandle = 0;
  c.ota = false;
  s_otaPct = -1;
  char out[96];
  snprintf(out, sizeof out, "{\"error\":\"%s\"}", why);
  respond(c, 400, "application/json", out, strlen(out));
  Serial.printf("ota: failed: %s\n", why);
}

void otaStart(HttpClient& c, const portal::Request& r, size_t clen, bool hotspot) {
  if (!authorized(c, r, hotspot)) {
    netShowPin();
    respond(c, 401, "application/json", "{\"error\":\"pin\"}", 15);
    return;
  }
  if (s_otaPct >= 0) return otaFail(c, "another update is running");
  c.otaPart = esp_ota_get_next_update_partition(nullptr);
  if (!c.otaPart) return otaFail(c, "no OTA slot in the partition table");
  if (clen < 1024 || clen > c.otaPart->size) return otaFail(c, "bad firmware size");
  esp_err_t err = esp_ota_begin(c.otaPart, OTA_WITH_SEQUENTIAL_WRITES, &c.otaHandle);
  if (err != ESP_OK) return otaFail(c, esp_err_to_name(err));
  c.ota = true;
  c.otaTotal = c.otaLeft = clen;
  size_t n = r.bodyLen < clen ? r.bodyLen : clen;
  if (n && esp_ota_write(c.otaHandle, r.body, n) != ESP_OK) return otaFail(c, "flash write failed");
  c.otaLeft -= n;
  s_otaPct = 0;
  Serial.printf("ota: receiving %u bytes into %s\n", (unsigned)clen, c.otaPart->label);
}

bool otaStep(HttpClient& c) {
  Scratch buf(4096);
  if (!buf) return true;
  uint8_t* const chunk = (uint8_t*)buf.p;
  const size_t chunkSize = buf.n;
  for (int i = 0; i < 16 && c.otaLeft; i++) {
    int avail = c.sock.available();
    if (avail <= 0) break;
    int n = c.sock.read(chunk, min((size_t)avail, min(chunkSize, c.otaLeft)));
    if (n <= 0) break;
    if (esp_ota_write(c.otaHandle, chunk, n) != ESP_OK) {
      otaFail(c, "flash write failed");
      return false;
    }
    c.otaLeft -= n;
    c.since = millis();
  }
  s_otaPct = (int)(100 - (uint64_t)c.otaLeft * 100 / c.otaTotal);
  if (c.otaLeft) {
    if (!c.sock.connected() || millis() - c.since > 10000) {
      otaFail(c, "upload stalled");
      return false;
    }
    return true;
  }
  esp_err_t err = esp_ota_end(c.otaHandle);
  c.otaHandle = 0;
  c.ota = false;
  if (err == ESP_OK) err = esp_ota_set_boot_partition(c.otaPart);
  if (err != ESP_OK) {
    s_otaPct = -1;
    char out[96];
    snprintf(out, sizeof out, "{\"error\":\"image rejected: %s\"}", esp_err_to_name(err));
    respond(c, 400, "application/json", out, strlen(out));
    Serial.printf("ota: %s\n", out);
    return false;
  }
  json(c, "{\"ok\":true,\"rebooting\":true}");
  Serial.printf("ota: done, booting %s\n", c.otaPart->label);
  s_rebootAt = millis() + 1500;
  return false;
}

//==============================
// app scripts: GET /api/app/<name> reads one, POST /api/app/<name> uploads one
//==============================
// Writing to the card while a transfer is coming in over Wi-Fi fails with I/O errors on the CYD, more often the slower the card runs (measured 2026-10-31: 4 of 10 good at 20 MHz,
// 0 of 10 at 10 and 4 MHz; on a quiet network every write was good). So an upload for the card lands in flash first and is copied when it has all arrived.
constexpr char UP_TMP[] = "/up.tmp";
bool cardCopy(const char* to) {
  Scratch buf(1024);
  if (!buf) return false;
  for (int attempt = 0; attempt < 3; attempt++) {
    if (attempt) vTaskDelay(pdMS_TO_TICKS(200));
    File in = LittleFS.open(UP_TMP, "r");
    File out = sdcard::fs().open(to, FILE_WRITE);
    bool ok = in && out;
    while (ok && in.available()) {
      const int n = in.read((uint8_t*)buf.p, buf.n);
      ok = n > 0 && out.write((const uint8_t*)buf.p, n) == (size_t)n;
      vTaskDelay(1);
    }
    if (in) in.close();
    if (out) out.close();
    if (ok) return true;
  }
  return false;
}

void upFinish(HttpClient& c, bool ok, const char* why) {
  if (!ok) Serial.printf("upload failed: %s (%u bytes left)\n", why ? why : "?", (unsigned)c.upLeft);
  char out[96];
  if (c.upFile) c.upFile.close();
  char tmp[40];
  if (c.upSd)
    apps::sdTmpPath(c.upName, tmp, sizeof tmp);
  else
    snprintf(tmp, sizeof tmp, "/apps/%s.tmp", c.upName);
  char dst[40];
  if (c.upPack) {  // installed from flash, and then it goes away
    const bool all = c.upPack == 2;
    c.up = false;
    c.upPack = 0;
    char report[160], reply[200];
    bool done = false;
    if (ok) {
      netBrainLock();
      done = brain::packInstall(LittleFS, "/pack.tmp", all, report, sizeof report);
      netBrainUnlock();
    }
    LittleFS.remove("/pack.tmp");
    if (done) {
      const int k = snprintf(reply, sizeof reply, "{\"ok\":true,\"report\":\"%s\"}", report);
      return respond(c, 200, "application/json", reply, k);
    }
    const int k = snprintf(reply, sizeof reply, "{\"error\":\"%s\"}",
                           ok ? report : (why && why[0] ? why : "could not save"));
    return respond(c, 400, "application/json", reply, k);
  }
  if (c.upBehaviour) {  // into flash, and a file that is not a behaviour file puts the old one back. (A copy on the card still wins, as always.)
    if (ok) {
      LittleFS.remove("/behaviour.old");
      const bool had = LittleFS.rename("/behaviour.txt", "/behaviour.old");
      netBrainLock();
      ok = LittleFS.rename("/behaviour.tmp", "/behaviour.txt") && brain::loadBehaviour(nullptr);
      if (!ok) {
        LittleFS.remove("/behaviour.txt");
        if (had) LittleFS.rename("/behaviour.old", "/behaviour.txt");
        brain::loadBehaviour(nullptr);
      }
      netBrainUnlock();
    } else {
      LittleFS.remove("/behaviour.tmp");
    }
    c.up = c.upBehaviour = false;
    if (ok) return respond(c, 200, "application/json", "{\"ok\":true}", 11);
    return respond(c, 400, "application/json", "{\"error\":\"not a behaviour file\"}", 32);
  }
  if (c.upImage) apps::imagePaths(c.upName, c.upSd, tmp, dst, sizeof tmp);
  if (c.upSd) {
    if (ok) ok = cardCopy(tmp);
    LittleFS.remove(UP_TMP);
  }
  if (ok)
    ok = c.upImage ? apps::commitImage(c.upName, c.upSd) : apps::commitUpload(c.upName, c.upSd);
  else if (c.upSd)
    sdcard::fs().remove(tmp);
  else
    LittleFS.remove(tmp);
  c.up = false;
  if (ok) {
    snprintf(out, sizeof out, "{\"ok\":true}");
    respond(c, 200, "application/json", out, strlen(out));
  } else {
    snprintf(out, sizeof out, "{\"error\":\"%s\"}", why && why[0] ? why : "could not save");
    respond(c, 400, "application/json", out, strlen(out));
  }
}

void upStart(HttpClient& c, const portal::Request& r, size_t clen, bool hotspot) {
  if (!authorized(c, r, hotspot)) {
    netShowPin();
    respond(c, 401, "application/json", "{\"error\":\"pin\"}", 15);
    return;
  }
  c.upPack = !strcmp(r.path, "/api/packall") ? 2 : !strcmp(r.path, "/api/pack") ? 1 : 0;
  if (c.upPack) {
    c.upBehaviour = c.upImage = false;
    c.upSd = false;
    if (clen < PACK_MAGIC_LEN || clen > 16UL * 1024 * 1024 ||
        (r.bodyLen >= 11 && strncmp(r.body, "teevee-pack", 11))) {
      c.upPack = 0;
      return respond(c, 400, "application/json", "{\"error\":\"not a pack\"}", 22);
    }
    if (apps::freeBytes() < clen + 16384) {
      c.upPack = 0;
      return respond(c, 400, "application/json",
                     "{\"error\":\"too big to send: put it in the card's /teevee/packs and use sd pack\"}",
                     77);
    }
    c.upFile = LittleFS.open("/pack.tmp", "w");
    if (!c.upFile) {
      c.upPack = 0;
      return respond(c, 400, "application/json", "{\"error\":\"cannot write the file\"}", 34);
    }
    c.up = true;
    size_t n = r.bodyLen < clen ? r.bodyLen : clen;
    if (n && c.upFile.write((const uint8_t*)r.body, n) != n) return upFinish(c, false, "write failed");
    c.upLeft = clen - n;
    c.since = millis();
    if (!c.upLeft) upFinish(c, true, nullptr);
    return;
  }
  c.upBehaviour = !strcmp(r.path, "/api/behaviour");
  if (c.upBehaviour) {
    if (clen < 1 || clen > 4096) return respond(c, 400, "application/json", "{\"error\":\"up to 4 KB\"}", 22);
    c.upImage = c.upSd = false;
    c.upFile = LittleFS.open("/behaviour.tmp", "w");
    if (!c.upFile) return respond(c, 400, "application/json", "{\"error\":\"cannot write the file\"}", 34);
    c.up = true;
    size_t n = r.bodyLen < clen ? r.bodyLen : clen;
    if (n && c.upFile.write((const uint8_t*)r.body, n) != n) return upFinish(c, false, "write failed");
    c.upLeft = clen - n;
    c.since = millis();
    if (!c.upLeft) upFinish(c, true, nullptr);
    return;
  }
  c.upImage = !strncmp(r.path, "/api/image/", 11);
  if (c.upImage) {  // a picture: to the card if there is one, else to flash; only its first bytes are checked, gfx.image checks the rest
    const char* name = r.path + 11;
    c.upSd = sdcard::mounted();
    snprintf(c.upName, sizeof c.upName, "%.11s", name);
    char tmp[40], dst[40];
    if (!apps::validName(name) || strlen(name) > 11 || !apps::imagePaths(name, c.upSd, tmp, dst, sizeof tmp))
      return respond(c, 400, "application/json", "{\"error\":\"bad image name\"}", 26);
    if (clen < BA_HEADER + 1 || (r.bodyLen >= 2 && (r.body[0] != 'b' || r.body[1] != 'A')))
      return respond(c, 400, "application/json", "{\"error\":\"not a Bitmap Array file\"}", 35);
    if (apps::freeBytes() < clen + 16384)
      return respond(c, 400, "application/json", "{\"error\":\"too big to send: put it on the card\"}", 44);
    c.upFile = LittleFS.open(c.upSd ? UP_TMP : tmp, "w");
    if (!c.upFile) return respond(c, 400, "application/json", "{\"error\":\"cannot write the file\"}", 34);
    c.up = true;
    size_t n = r.bodyLen < clen ? r.bodyLen : clen;
    if (n && c.upFile.write((const uint8_t*)r.body, n) != n) return upFinish(c, false, "write failed");
    c.upLeft = clen - n;
    c.since = millis();
    if (!c.upLeft) upFinish(c, true, nullptr);
    return;
  }
  c.upSd = !strncmp(r.path, "/api/sdapp/", 11);
  const char* name = r.path + (c.upSd ? 11 : 9);
  snprintf(c.upName, sizeof c.upName, "%.11s", name);
  if (!apps::validName(name) || strlen(name) > 11)
    return respond(c, 400, "application/json", "{\"error\":\"bad app name\"}", 25);
  if (clen < 1 || clen > apps::MAX_SCRIPT)
    return respond(c, 400, "application/json", "{\"error\":\"script must be 1 byte to 32 KB\"}", 42);
  if (c.upSd && !sdcard::mounted())
    return respond(c, 400, "application/json", "{\"error\":\"no SD card\"}", 24);
  if (apps::freeBytes() < clen + 8192)
    return respond(c, 400, "application/json", "{\"error\":\"the app storage is full\"}", 36);
  if (!apps::exists(name) && apps::count() >= apps::MAX_APPS)
    return respond(c, 400, "application/json", "{\"error\":\"too many apps\"}", 27);
  char tmp[40];
  if (c.upSd)
    apps::sdTmpPath(name, tmp, sizeof tmp);
  else
    snprintf(tmp, sizeof tmp, "/apps/%s.tmp", c.upName);
  c.upFile = LittleFS.open(c.upSd ? UP_TMP : tmp, "w");
  if (!c.upFile) return respond(c, 400, "application/json", "{\"error\":\"cannot write the file\"}", 34);
  c.up = true;
  size_t n = r.bodyLen < clen ? r.bodyLen : clen;
  if (n && c.upFile.write((const uint8_t*)r.body, n) != n) return upFinish(c, false, "flash write failed");
  c.upLeft = clen - n;
  c.since = millis();
  if (!c.upLeft) upFinish(c, true, nullptr);
}

bool upStep(HttpClient& c) {
  Scratch buf(1024);
  if (!buf) return true;
  uint8_t* const chunk = (uint8_t*)buf.p;
  const size_t chunkSize = buf.n;
  for (int i = 0; i < 8 && c.upLeft; i++) {
    int avail = c.sock.available();
    if (avail <= 0) break;
    int n = c.sock.read(chunk, min((size_t)avail, min(chunkSize, c.upLeft)));
    if (n <= 0) break;
    if (c.upFile.write(chunk, n) != (size_t)n) {
      upFinish(c, false, "flash write failed");
      return false;
    }
    c.upLeft -= n;
    c.since = millis();
  }
  if (!c.upLeft) {
    upFinish(c, true, nullptr);
    return false;
  }
  if (!c.sock.connected() || millis() - c.since > 10000) {
    upFinish(c, false, "upload stalled");
    return false;
  }
  return true;
}

// GET /api/export or /api/exportall: written to a file first (its size must be known), sent, and removed. Needs the login, as all of /api/ does.
void serveExport(HttpClient& c, bool all) {
  // In flash if it fits (Wi-Fi is quiet then, but pictures can be large); else on the card, a second try there if that was busy.
  bool toSd = false;
  char err[60], reply[90];
  netBrainLock();  // (the robot is quiet while it is packed: a second or two for a big one)
  bool made = brain::packExport(LittleFS, "/export.tmp", all, err, sizeof err);
  for (int attempt = 0; !made && sdcard::mounted() && attempt < 2; attempt++)
    toSd = made = brain::packExport(sdcard::fs(), "/teevee/export.tmp", all, err, sizeof err);
  netBrainUnlock();
  if (!made) {
    const int k = snprintf(reply, sizeof reply, "{\"error\":\"%s\"}", err);
    return respond(c, 400, "application/json", reply, k);
  }
  fs::FS& where = toSd ? (fs::FS&)sdcard::fs() : (fs::FS&)LittleFS;
  const char* path = toSd ? "/teevee/export.tmp" : "/export.tmp";
  File f = where.open(path, "r");
  if (!f) return respond(c, 500, "text/plain", "cannot read the export", 22);
  char head[220];
  const int n = snprintf(
      head, sizeof head,
      "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: %u\r\nContent-Disposition: attachment; filename=\"teevee.tvpack\"\r\n"
      "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
      (unsigned)f.size());
  bool ok = sendAll(c.sock, (const uint8_t*)head, n, 400);
  Scratch chunk(1024);  // (a whole backup: no large buffers on the brain task's stack)
  while (ok && chunk && f.available()) {
    int got = f.read((uint8_t*)chunk.p, chunk.n);
    if (got <= 0) break;
    ok = sendAll(c.sock, (const uint8_t*)chunk.p, got, 400);
  }
  f.close();
  where.remove(path);
  if (!ok) c.sock.stop();
}

void serveAppFile(HttpClient& c, const char* name) {
  char path[40];
  if (!apps::pathOf(name, path, sizeof path)) return respond(c, 400, "text/plain", "bad app name", 12);
  File f = apps::open(name);
  if (!f) return respond(c, 404, "text/plain", "no such app", 11);
  char head[160];
  int n = snprintf(
      head, sizeof head,
      "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: %u\r\nCache-Control: no-store\r\n"
      "Connection: close\r\n\r\n",
      (unsigned)f.size());
  bool ok = sendAll(c.sock, (const uint8_t*)head, n, 400);
  uint8_t chunk[1024];
  while (ok && f.available()) {
    int got = f.read(chunk, sizeof chunk);
    if (got <= 0) break;
    ok = sendAll(c.sock, chunk, got, 400);
  }
  f.close();
  if (!ok) c.sock.stop();
}

void handle(HttpClient& c, const portal::Request& r, bool hotspot) {
  const bool post = !strcmp(r.method, "POST");
  bool forUs = !strncmp(r.host, "192.168.4.1", 11) || !strncasecmp(r.host, "teevee", 6) ||
               !strcmp(r.host, WiFi.localIP().toString().c_str());
  if (hotspot && !forUs) {
    respond(c, 302, "text/plain", "", 0, "Location: http://192.168.4.1/\r\n");
    return;
  }
  if (!strncmp(r.path, "/api/", 5) && strcmp(r.path, "/api/login") && !authorized(c, r, hotspot)) {
    netShowPinQuiet();
    respond(c, 401, "application/json", "{\"error\":\"pin\"}", 15);
    return;
  }
  if (!strcmp(r.path, "/")) {
    respond(c, 200, "text/html; charset=utf-8", (const char*)PORTAL_PAGE_GZ, PORTAL_PAGE_GZ_LEN,
            "Content-Encoding: gzip\r\n");
  } else if (!strcmp(r.method, "GET") &&
             (!strcmp(r.path, "/api/export") || !strcmp(r.path, "/api/exportall"))) {
    serveExport(c, !strcmp(r.path, "/api/exportall"));
  } else if (!strcmp(r.method, "GET") && !strncmp(r.path, "/api/app/", 9)) {
    serveAppFile(c, r.path + 9);
  } else if (!strcmp(r.path, "/api/info")) {
    char out[256];
    netInfoJson(out, sizeof out);
    json(c, out);
  } else if (!strcmp(r.path, "/api/scan")) {
    Scratch out(1600);
    json(c, !out || netScanJson(out.p, out.n) < 0 ? "{\"scanning\":true}" : out.p);
  } else if (post && !strcmp(r.path, "/api/login")) {
    JsonView m;
    char pin[8] = "";
    if (jsonParse(r.body, r.bodyLen, m)) m.get("pin").str(pin, sizeof pin);
    const char* tok = netLogin(pin);
    char out[80];
    snprintf(out, sizeof out, tok ? "{\"token\":\"%s\"}" : "{\"error\":\"wrong pin\"}", tok);
    json(c, out);
  } else if (post && !strcmp(r.path, "/api/msg")) {
    // Only asking for a tab's worth of data can need a large reply (settings + metadata, ~9 KB). Everything else answers in a few bytes and must work when memory is short:
    // "stop" with an app running, and starting an app, which needs the room this buffer would take. A reply that does not fit says so (netReply).
    const bool big = strstr(r.body, "\"get\"") && !strstr(r.body, "\"state\"");
    size_t cap = 0;
    char* out = nullptr;
    if (big && !(out = (char*)malloc(12288)))
      apps::bgYield();  // the background app steps aside if that is what it takes
    if (out) free(out), out = nullptr;
    for (size_t size : {big ? (size_t)12288 : (size_t)1024, big ? (size_t)4096 : (size_t)512, (size_t)512})
      if ((out = (char*)malloc(size))) {
        cap = size;
        break;
      }
    if (!out) {
      json(c, "{\"error\":\"out of memory\"}");
    } else {
      netHandle(r.body, r.bodyLen, out, cap);
      json(c, out[0] ? out : "{\"ok\":true}");
      free(out);
    }
  } else if (post && (!strcmp(r.path, "/api/wifi") || !strcmp(r.path, "/api/tz"))) {
    JsonView m;
    char a[40] = "", b[72] = "";
    bool wifi = r.path[5] == 'w';
    bool ok = jsonParse(r.body, r.bodyLen, m) && m.get(wifi ? "ssid" : "tz").str(a, sizeof a) && a[0];
    if (wifi && ok) m.get("pass").str(b, sizeof b);
    json(c, ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"bad request\"}");
    c.sock.flush();
    if (ok && wifi) netSetWifi(a, b);
    if (ok && !wifi) netSetTz(a);
  } else {
    respond(c, 404, "text/plain", "not found", 9);
  }
}

void serveDns() {
  int size = dns.parsePacket();
  if (size <= 0) return;
  uint8_t q[512], out[600];
  int n = dns.read(q, sizeof q);
  IPAddress ip = WiFi.softAPIP();
  const uint8_t addr[4] = {ip[0], ip[1], ip[2], ip[3]};
  size_t len = portal::dnsReply(q, n > 0 ? n : 0, addr, out, sizeof out);
  if (!len) return;
  dns.beginPacket(dns.remoteIP(), dns.remotePort());
  dns.write(out, len);
  dns.endPacket();
}
}  // namespace

void portalBegin() { http.begin(); }

void portalLoop(bool hotspot) {
  if (hotspot && !dnsOn) dnsOn = dns.begin(53);
  if (!hotspot && dnsOn) {
    dns.stop();
    dnsOn = false;
  }
  if (dnsOn) serveDns();

  if (WiFiClient s = http.accept()) {
    int i = 0;
    while (i < MAX_HTTP && clients[i].open) i++;
    if (i == MAX_HTTP) {
      s.stop();
    } else {
      clients[i].sock = s;
      clients[i].open = true;
      clients[i].len = 0;
      clients[i].since = millis();
    }
  }
  for (HttpClient& c : clients) {
    if (!c.open) continue;
    if (c.ota) {
      if (otaStep(c)) continue;
      c.sock.stop();
      c.open = false;
      continue;
    }
    if (c.up) {
      if (upStep(c)) continue;
      c.sock.stop();
      c.open = false;
      continue;
    }
    int avail = c.sock.available();
    size_t room = sizeof c.buf - 1 - c.len;
    if (avail > 0 && room) {
      c.len += c.sock.read((uint8_t*)c.buf + c.len, min((size_t)avail, room));
      c.buf[c.len] = 0;
    }
    portal::Request r;
    size_t clen = 0;
    portal::Parse p = avail > 0 && c.len ? portal::parseHead(c.buf, c.len, r, clen) : portal::NEED_MORE;
    if (p == portal::DONE && !strcmp(r.method, "POST") && !strcmp(r.path, "/api/ota")) {
      otaStart(c, r, clen, hotspot);
      if (c.ota) continue;
      c.sock.stop();
      c.open = false;
      continue;
    }
    if (p == portal::DONE && !strcmp(r.method, "POST") &&
        (!strncmp(r.path, "/api/app/", 9) || !strncmp(r.path, "/api/sdapp/", 11) ||
         !strncmp(r.path, "/api/image/", 11) || !strcmp(r.path, "/api/behaviour") ||
         !strncmp(r.path, "/api/pack", 9))) {
      upStart(c, r, clen, hotspot);
      if (c.up) continue;
      c.sock.stop();
      c.open = false;
      continue;
    }
    if (p == portal::DONE) p = portal::parseBody(clen, MAX_BODY, r);
    if (p == portal::DONE) {
      const_cast<char*>(r.body)[r.bodyLen] = 0;
      handle(c, r, hotspot);
    } else if (p == portal::BAD) {
      respond(c, 400, "text/plain", "bad request", 11);
    } else if (c.sock.connected() && millis() - c.since < HTTP_TIMEOUT_MS) {
      continue;
    }
    c.sock.stop();
    c.open = false;
  }
  if (s_rebootAt && (int32_t)(millis() - s_rebootAt) >= 0) ESP.restart();
}

int portalOtaProgress() { return s_otaPct; }
