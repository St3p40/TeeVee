#include "net.h"
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_app_desc.h>
#include <esp_ota_ops.h>
#include <time.h>
#include "core/json.h"
#include "portal.h"
#include "sock_util.h"
#include "core/ws_proto.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

namespace {
constexpr int MAX_CLIENTS = 4;
constexpr size_t MAX_MSG = 1024;
constexpr uint32_t HANDSHAKE_MS = 5000;  // drop clients that never finish the upgrade

struct WsClient {
    WiFiClient sock;
    bool open = false, upgraded = false;
    uint32_t since = 0;
    size_t len = 0;
    uint8_t buf[MAX_MSG + 16];
};

WiFiServer server(81);
WsClient clients[MAX_CLIENTS];
NetMessageFn s_onMsg = nullptr;
SemaphoreHandle_t s_brainMux = nullptr;  // the robot's state: held by the brain's loop and by anything the web server changes in it
char s_bcast[256] = "";                  // the newest state report, copied by the brain (under the lock), sent by the web task
bool s_bcastNew = false;
Preferences prefs;
bool s_started = false, s_online = false, s_haveCreds = false;
constexpr uint8_t HTTP_CLIENT = 0xFF;
bool s_apOn = false;
char s_apSsid[20] = "", s_apPass[12] = "";
uint32_t s_forceUntil = 0, s_downSince = 0, s_upSince = 0, s_lastTry = 0;
bool s_stopAp = false;
char* s_reply = nullptr;
size_t s_replyCap = 0;
char s_status[20] = "no wifi";
// Web page login on the home network: 4-digit PIN (shown on screen on demand) -> long random token.
char s_pin[5] = "", s_token[33] = "";
uint32_t s_pinShowUntil = 0, s_lockUntil = 0;
int s_badPins = 0;

void newPin() {
  snprintf(s_pin, sizeof s_pin, "%04lu", (unsigned long)(esp_random() % 10000));
  for (int i = 0; i < 4; i++) snprintf(s_token + 8 * i, 9, "%08lx", (unsigned long)esp_random());
  prefs.putString("pin", s_pin);
  prefs.putString("token", s_token);
}
String s_tz;

// A frame that can't be delivered in time leaves the stream unusable, so the client is dropped.
void sendFrame(WsClient& c, uint8_t opcode, const uint8_t* data, size_t len) {
  uint8_t h[4];
  size_t hn = wsproto::header(opcode, len, h);
  if (sendAll(c.sock, h, hn) && (!len || sendAll(c.sock, data, len, len > 4000 ? 1000 : 200))) return;
  c.sock.stop();
  c.open = c.upgraded = false;
  c.len = 0;
  Serial.println("ws: dropped a client that stopped reading");
}

void drop(int i, const char* why) {
  WsClient& c = clients[i];
  if (c.upgraded) Serial.printf("ws: client %d %s\n", i, why);
  c.sock.stop();
  c.open = c.upgraded = false;
  c.len = 0;
}

void accept() {
  WiFiClient s = server.accept();
  if (!s) return;
  for (int i = 0; i < MAX_CLIENTS; i++)
    if (!clients[i].open) {
      WsClient& c = clients[i];
      c.sock = s;
      c.sock.setNoDelay(true);
      c.open = true;
      c.upgraded = false;
      c.len = 0;
      c.since = millis();
      return;
    }
  s.stop();
}

// True for the PC tools (no Origin) and the board's own pages (teevee.local, its address, the hotspot address).
bool originOk(const char* req) {
  char origin[64];
  if (!wsproto::originHost(req, origin, sizeof origin) || !strcasecmp(origin, "teevee.local")) return true;
  for (IPAddress ip : {WiFi.localIP(), WiFi.softAPIP()}) {
    char a[16];
    snprintf(a, sizeof a, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    if (!strcmp(origin, a)) return true;
  }
  return false;
}

// The same rule as the HTTP API: the login token, or a client on the board's own hotspot (it knows the hotspot password).
bool wsAuthorized(const WsClient& c) {
  char token[40];
  if (wsproto::requestToken((const char*)c.buf, token, sizeof token) && netTokenOk(token)) return true;
  if (!(WiFi.getMode() & WIFI_AP)) return false;
  IPAddress peer = const_cast<WsClient&>(c).sock.remoteIP(), ap = WiFi.softAPIP(), sta = WiFi.localIP();
  const bool sameSubnet = ap[0] == sta[0] && ap[1] == sta[1] && ap[2] == sta[2];
  return !sameSubnet && peer[0] == ap[0] && peer[1] == ap[1] && peer[2] == ap[2];
}

void serve(int i) {
  WsClient& c = clients[i];
  if (!c.sock.connected()) return drop(i, "disconnected");
  int avail = c.sock.available();
  if (avail > 0) {
    size_t room = sizeof c.buf - 1 - c.len;
    if (room == 0) return drop(i, "overflow");
    c.len += c.sock.read(c.buf + c.len, min((size_t)avail, room));
  }
  if (!c.upgraded) {
    c.buf[c.len] = 0;
    const char* hend = strstr((char*)c.buf, "\r\n\r\n");
    if (!hend) {
      if (millis() - c.since > HANDSHAKE_MS) drop(i, "timeout");
      return;
    }
    // A web page from elsewhere must not drive the robot through the visitor's browser: only the board's own pages may connect.
    if (!originOk((char*)c.buf)) {
      c.sock.print("HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n");
      return drop(i, "foreign origin");
    }
    if (!wsAuthorized(c)) {
      netShowPinQuiet();
      c.sock.print("HTTP/1.1 401 Unauthorized\r\nContent-Length: 0\r\n\r\n");
      return drop(i, "no login");
    }
    char resp[192];
    size_t n = wsproto::handshake((char*)c.buf, resp, sizeof resp);
    if (!n) {
      c.sock.print("HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n");
      return drop(i, "bad handshake");
    }
    if (!sendAll(c.sock, (const uint8_t*)resp, n)) return drop(i, "handshake not taken");
    c.upgraded = true;
    const size_t head = (size_t)(hend + 4 - (char*)c.buf);
    memmove(c.buf, c.buf + head, c.len - head);
    c.len -= head;
    Serial.printf("ws: client %d connected (%s)\n", i, c.sock.remoteIP().toString().c_str());
    return;
  }
  wsproto::Frame f;
  wsproto::Parse r;
  while ((r = wsproto::parse(c.buf, c.len, MAX_MSG, f)) == wsproto::FRAME) {
    if (f.opcode == 0x8) {
      sendFrame(c, 0x8, nullptr, 0);
      return drop(i, "closed");
    }
    if (f.opcode == 0x9) sendFrame(c, 0xA, f.payload, f.len);
    if (f.opcode == 0x1 && f.fin && s_onMsg) {
      uint8_t keep = f.payload[f.len];
      f.payload[f.len] = 0;
      netBrainLock();
      s_onMsg((const char*)f.payload, f.len, (uint8_t)i);
      netBrainUnlock();
      f.payload[f.len] = keep;
    } else if (f.opcode == 0x1 || f.opcode == 0x0) {
      const char* err = "{\"error\":\"fragmented messages not supported\"}";
      sendFrame(c, 0x1, (const uint8_t*)err, strlen(err));
    }
    if (!c.upgraded) return;
    memmove(c.buf, c.buf + f.consumed, c.len - f.consumed);
    c.len -= f.consumed;
  }
  if (r == wsproto::BAD) drop(i, "bad frame (unmasked or too large)");
}

void connect() {
  s_lastTry = millis();
  String ssid = prefs.getString("ssid", ""), pass = prefs.getString("pass", "");
  s_haveCreds = ssid.length() > 0;
  if (!s_haveCreds) return;
  WiFi.disconnect();
  WiFi.begin(ssid.c_str(), pass.c_str());
  Serial.printf("wifi: connecting to %s\n", ssid.c_str());
}

void startAP(bool keepSta) {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(s_apSsid, sizeof s_apSsid, "TeeVee-%02X%02X", mac[4], mac[5]);
  snprintf(s_apPass, sizeof s_apPass, "%08lu", (unsigned long)(esp_random() % 100000000UL));
  WiFi.setAutoReconnect(false);
  if (!keepSta) WiFi.disconnect();
  WiFi.mode(WIFI_AP_STA);
  bool ok = WiFi.softAP(s_apSsid, s_apPass);
  s_apOn = true;
  Serial.printf("setup: hotspot %s password %s -> http://%s/ (channel %d)%s\n", s_apSsid, s_apPass,
                WiFi.softAPIP().toString().c_str(), (int)WiFi.channel(), ok ? "" : " FAILED to start");
}

void stopAP() {
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  s_apOn = false;
  Serial.println("setup: hotspot off");
}
}  // namespace

static void netTask(void*);  // (the web task, below)

void netBegin(NetMessageFn onMessage) {
  s_onMsg = onMessage;
  // Which app slot is running / set to boot, and each slot's OTA state (0 new, 1 pending, 2 valid, 3 invalid, 4 aborted,
  // -1 never set).
  const esp_partition_t *run = esp_ota_get_running_partition(), *boot = esp_ota_get_boot_partition();
  Serial.printf("firmware: running %s, boot %s", run ? run->label : "?", boot ? boot->label : "?");
  for (int i = 0; i < 2; i++) {
    const esp_partition_t* p = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, (esp_partition_subtype_t)(ESP_PARTITION_SUBTYPE_APP_OTA_0 + i), nullptr);
    esp_ota_img_states_t st;
    if (p && esp_ota_get_state_partition(p, &st) == ESP_OK) Serial.printf(", %s state %d", p->label, (int)st);
  }
  Serial.println();
  prefs.begin("teevee", false);
  s_tz = prefs.getString("tz", "UTC0");
  String pin = prefs.getString("pin", ""), tok = prefs.getString("token", "");
  if (pin.length() == 4 && tok.length() == 32) {
    memcpy(s_pin, pin.c_str(), sizeof s_pin);
    memcpy(s_token, tok.c_str(), sizeof s_token);
  } else {
    newPin();
  }
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("teevee");
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  connect();
  if (!s_brainMux) s_brainMux = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(netTask, "net", 10240, nullptr, 1, nullptr, 0);
}

static void netTask(void*) {
  for (;;) {
    netLoop();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void netPowerSave(bool on) {
  if (s_apOn) return;  // the setup hotspot is an access point: it cannot doze
  WiFi.setSleep(on ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE);
}

static void flushBroadcast();  // (below)

void netLoop() {
  const uint32_t now = millis();
  const bool up = WiFi.status() == WL_CONNECTED;
  if (up) {
    s_downSince = 0;
    if (!s_upSince) s_upSince = now;
  } else {
    s_upSince = 0;
    if (!s_downSince) s_downSince = now;
  }
  const bool forced = s_forceUntil && (int32_t)(now - s_forceUntil) < 0;
  if (!forced) s_forceUntil = 0;
  if (!s_apOn && (forced || !s_haveCreds || (!up && now - s_downSince > 30000))) startAP(up);
  if (s_apOn && !forced && up && (s_stopAp || now - s_upSince > 60000)) stopAP();
  s_stopAp = false;
  if (s_apOn && s_haveCreds && !up && now - s_lastTry > 120000) connect();

  if ((up || s_apOn) && !s_started) {
    s_started = true;
    server.begin();
    portalBegin();
  }
  if (up && !s_online) {
    s_online = true;
    MDNS.begin("teevee");
    MDNS.addService("ws", "tcp", 81);
    MDNS.addService("http", "tcp", 80);
    configTzTime(s_tz.c_str(), "pool.ntp.org", "time.nist.gov");
    // This firmware reached the network, so it can be updated again: keep it (cancels a pending rollback
    // if the bootloader has that enabled; harmless otherwise).
    esp_ota_mark_app_valid_cancel_rollback();
    Serial.printf("wifi: connected, http://%s/ ws://%s:81 (teevee.local)\n",
                  WiFi.localIP().toString().c_str(), WiFi.localIP().toString().c_str());
  }
  if (up)
    snprintf(s_status, sizeof s_status, "%s", WiFi.localIP().toString().c_str());
  else
    snprintf(s_status, sizeof s_status, "%s", s_apOn ? "setup mode" : s_haveCreds ? "connecting" : "no wifi");
  flushBroadcast();
  if (s_started) {
    accept();
    for (int i = 0; i < MAX_CLIENTS; i++)
      if (clients[i].open) serve(i);
    portalLoop(s_apOn);
  }
}

void netBroadcast(const char* json) {
  snprintf(s_bcast, sizeof s_bcast, "%s", json);
  s_bcastNew = true;
}

// Sends the newest state report to every page that is connected (web task only: the sockets are its own).
static void flushBroadcast() {
  if (!s_bcastNew) return;
  char local[sizeof s_bcast];
  netBrainLock();
  memcpy(local, s_bcast, sizeof local);
  s_bcastNew = false;
  netBrainUnlock();
  for (WsClient& c : clients)
    if (c.upgraded) sendFrame(c, 0x1, (const uint8_t*)local, strlen(local));
}

void netBrainLock() {
  if (s_brainMux) xSemaphoreTake(s_brainMux, portMAX_DELAY);
}
void netBrainUnlock() {
  if (s_brainMux) xSemaphoreGive(s_brainMux);
}

void netReply(uint8_t client, const char* json) {
  if (client == HTTP_CLIENT && s_reply) {
    if (strlen(json) < s_replyCap)
      strcpy(s_reply, json);
    else
      snprintf(s_reply, s_replyCap, "{\"error\":\"out of memory\"}");  // never half a reply
    return;
  }
  if (client < MAX_CLIENTS && clients[client].upgraded)
    sendFrame(clients[client], 0x1, (const uint8_t*)json, strlen(json));
}

void netSetWifi(const char* ssid, const char* pass) {
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  connect();
}

void netSetTz(const char* tz) {
  s_tz = tz;
  prefs.putString("tz", tz);
  if (s_online) configTzTime(s_tz.c_str(), "pool.ntp.org", "time.nist.gov");
}

bool netConnected() { return WiFi.status() == WL_CONNECTED; }
bool netSetupMode() { return s_apOn; }

const char* netPin() { return s_pin; }
void netNewPin() { newPin(); }
void netShowPin() { s_pinShowUntil = millis() + 120000; }
void netShowPinQuiet() {
  static uint32_t lastShown = 0;
  const uint32_t now = millis();
  if (lastShown && now - lastShown < 600000) return;
  lastShown = now;
  netShowPin();
}
bool netPinShowing() { return s_pinShowUntil && (int32_t)(millis() - s_pinShowUntil) < 0; }
bool netTokenOk(const char* token) {
  if (!token || strlen(token) != 32 || !s_token[0]) return false;
  uint8_t diff = 0;
  for (int i = 0; i < 32; i++) diff |= (uint8_t)(token[i] ^ s_token[i]);
  return diff == 0;
}

const char* netLogin(const char* pin) {
  uint32_t now = millis();
  if (s_lockUntil && (int32_t)(now - s_lockUntil) < 0) return nullptr;
  if (pin && !strcmp(pin, s_pin)) {
    s_badPins = 0;
    s_pinShowUntil = 0;
    return s_token;
  }
  if (++s_badPins >= 5) {
    s_badPins = 0;
    s_lockUntil = now + 60000;
  }
  return nullptr;
}
const char* netApSsid() { return s_apSsid; }
const char* netApPass() { return s_apPass; }
void netStartSetup(bool on) {
  s_forceUntil = on ? millis() + 600000 : 0;
  s_stopAp = !on;
}

size_t netHandle(const char* json, size_t len, char* reply, size_t n) {
  reply[0] = 0;
  s_reply = reply;
  s_replyCap = n;
  if (s_onMsg) {
    netBrainLock();
    s_onMsg(json, len, HTTP_CLIENT);
    netBrainUnlock();
  }
  s_reply = nullptr;
  return strlen(reply);
}

int netInfoJson(char* out, size_t n) {
  char ssid[72];
  jsonEscape(prefs.getString("ssid", "").c_str(), ssid, sizeof ssid);
  bool up = WiFi.status() == WL_CONNECTED;
  char build[12];
  esp_app_get_elf_sha256(build, sizeof build);
  const esp_partition_t *run = esp_ota_get_running_partition(), *boot = esp_ota_get_boot_partition();
  return snprintf(
      out, n,
      "{\"ssid\":\"%s\",\"ip\":\"%s\",\"setup\":%s,\"hotspot\":\"%s\",\"build\":\"%s\",\"slot\":\"%s\","
      "\"boot\":\"%s\"}",
      ssid, up ? WiFi.localIP().toString().c_str() : "", s_apOn ? "true" : "false", s_apOn ? s_apSsid : "",
      build, run ? run->label : "?", boot ? boot->label : "?");
}

int netScanJson(char* out, size_t n) {
  int count = WiFi.scanComplete();
  if (count == WIFI_SCAN_FAILED) WiFi.scanNetworks(true);
  if (count < 0) return -1;
  size_t k = snprintf(out, n, "{\"networks\":[");
  for (int i = 0; i < count && k + 120 < n; i++) {
    String s = WiFi.SSID(i);
    if (!s.length()) continue;
    bool dup = false;
    for (int j = 0; j < i && !dup; j++) dup = WiFi.SSID(j) == s;
    if (dup) continue;
    char esc[72];
    jsonEscape(s.c_str(), esc, sizeof esc);
    k += snprintf(out + k, n - k, "%s{\"ssid\":\"%s\",\"rssi\":%d,\"lock\":%s}", out[k - 1] == '[' ? "" : ",",
                  esc, (int)WiFi.RSSI(i), WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "false" : "true");
  }
  snprintf(out + k, n - k, "]}");
  WiFi.scanDelete();
  return (int)strlen(out);
}
const char* netStatus() { return s_status; }

bool netLocalTime(int& hour, int& min) {
  if (!s_online) return false;
  struct tm t;
  if (!getLocalTime(&t, 0)) return false;
  hour = t.tm_hour;
  min = t.tm_min;
  return true;
}
