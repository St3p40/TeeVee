#pragma once
#include <stddef.h>
#include <stdint.h>

// Wi-Fi + WebSocket server (port 81, mDNS teevee.local) + NTP. Runs on core 0 from brainTask.
// Credentials and timezone live in NVS, set over Serial (`wifi`, `tz`), so nothing secret is in the source.
// Own small WebSocket server (ws_proto.h); messages arrive as NUL-terminated JSON text.
using NetMessageFn = void (*)(const char* json, size_t len, uint8_t client);

void netBegin(NetMessageFn onMessage);
void netLoop();
// Saving power while nobody is looking: the Wi-Fi radio dozes between the access point's beacons (it still hears everything, a
// little later). Off again for a quick web page.
void netPowerSave(bool on);
// The web server runs in its own task (netTask, core 0). The robot's state is changed only by the brain, so the brain holds
// netBrainLock() for each pass of its loop, and anything the web server does to the robot takes the same lock (a message, a pack
// install or export, a behaviour reload). A firmware upload touches only flash and takes no lock: it no longer stops the brain.
void netBrainLock();
void netBrainUnlock();
// The state report for the web page's sockets: copied here by the brain (call it with the lock held), sent by the web task.
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
