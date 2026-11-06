#pragma once
#include <FS.h>
#include <Arduino.h>
#include "core/app_cmds.h"

// Lua apps on the board (the VM itself is app_vm.h): scripts live as files in /apps on the LittleFS partition, the
// running app gets its own task on core 0, and its drawing is handed to the renderer on core 1 as a command list.
namespace apps {
static constexpr int MAX_APPS = 32;
static constexpr size_t MAX_SCRIPT = 32 * 1024;

bool begin();
void rescan();
int count();
const char* name(int i);
bool exists(const char* name);
bool imagePaths(const char* name, bool toSd, char* tmp, char* dst, size_t size);
bool commitImage(const char* name, bool toSd);
bool validName(const char* name);
bool pathOf(const char* name, char* buf, size_t n);
bool commitUpload(const char* name, bool toSd = false);
bool onSd(int i);  // app i has a copy on the SD card (it wins over a flash copy)
File open(const char* name);  // the script, from the SD card if it is there, else from flash
bool sdTmpPath(const char* name, char* buf, size_t n);
bool remove(const char* name);
size_t freeBytes();
size_t totalBytes();

// One app may also run in the background: background(dt) once a second, nothing on the screen. The RAM does not hold it and an app on the screen together, so they
// take turns: it pauses while an app is up and comes back after.
bool bgSet(const char* name);
const char* bgName();
const char* bgError();  // why it stopped; "" when it did not
void bgYield();
void bgStep(uint32_t now);
void setUrges(const float* u);       // the mind net's urges, published by the brain for face.urge
bool takeFaceSay(char* text, size_t n);  // what an app asked it to say
bool takeFaceWake();

bool start(const char* name);
void stop();
bool active();
const char* current();
bool exitRequested();
bool capturesSwipes();
void touch(uint8_t kind, int x, int y);
void button(int index, bool down);
void setLight(float level);
void setMood(float v, float a, float d);
void setNeeds(float attention, float rest, float play);
bool takeFaceShow(char* name, size_t n, uint32_t& ms);
bool takeFaceLook(float& x, float& y);
uint8_t takeFaceEmote();

// For the renderer (core 1): copies the newest frame if it is newer than `version`.
bool takeFrame(AppFrame& out, uint32_t& version);
int debugInfo(char* buf, size_t n);
}  // namespace apps
