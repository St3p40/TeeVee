#pragma once
#include <stddef.h>
#include <stdint.h>

// Wi-Fi + WebSocket server (port 81, mDNS teevee.local) + NTP. Runs on core 0 from brainTask.
// Credentials and timezone live in NVS, set over Serial (`wifi`, `tz`), so nothing secret is in the source.
// Own small WebSocket server (ws_proto.h); messages arrive as NUL-terminated JSON text.
using NetMessageFn = void (*)(const char* json, size_t len, uint8_t client);

void netBegin(NetMessageFn onMessage);
void netLoop();
void netBroadcast(const char* json);
void netReply(uint8_t client, const char* json);
void netSetWifi(const char* ssid, const char* pass);
void netSetTz(const char* tz);
bool netConnected();
const char* netStatus();
bool netLocalTime(int& hour, int& min);

bool netSetupMode();
const char* netApSsid();
const char* netApPass();
void netStartSetup(bool on = true);
// Web page login on the home network (hotspot clients don't need it).
const char* netPin();
void netNewPin();
void netShowPin();
bool netPinShowing();
void netShowPinQuiet();  // for requests without the right login: at most once every 10 minutes (an old page polling must not keep the screen on)
bool netTokenOk(const char* token);
const char* netLogin(const char* pin);
size_t netHandle(const char* json, size_t len, char* reply, size_t n);
int netInfoJson(char* out, size_t n);
int netScanJson(char* out, size_t n);
