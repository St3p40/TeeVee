#pragma once
// Internals shared by the brain's source files. Core 0 runs one task (brainTask in brain.cpp); the rest of its work
// is split by job:
//   brain.cpp     state, saving/loading, the per-tick steps (senseStimuli, think, tickTimers, publishWidgets)
//   console.cpp   Serial console (commands, help)
//   protocol.cpp  WebSocket / HTTP JSON messages
//   studio.cpp    emotes, custom expressions and emotes, the face editor
// Everything in namespace `brain` is touched from core 0 only (including the network callback it drives).
#include <Arduino.h>
#include "board.h"
#include <LittleFS.h>
#include "core/screen_idle.h"
#include "core/taste.h"
#include <Preferences.h>
#include "core/custom.h"
#include "core/daytint.h"
#include "core/diary.h"
#include "core/emotes.h"
#include "core/emotion.h"
#include "core/idle.h"
#include "core/json.h"
#include "core/lcd.h"
#include "core/mapper.h"
#include "core/mind.h"
#include "core/mind_senses.h"
#include "core/mind_actions.h"
#include "core/needs.h"
#include "core/temper.h"
#include "core/wardrobe.h"
#include "net.h"
#include "portal.h"
#include "core/scratch.h"
#include "sdcard.h"
#include "core/settings.h"
#include "shared.h"
#include "core/spring.h"
#include "core/stimuli.h"
#include "core/touchzone.h"

void printHelp();
int anchorIndex(const String& s);
FaceParams blendAnchors(int a, int b, float t);
void printPad(const EmotionEngine& emo);
extern const char* const STATE_NAMES[];

namespace brain {
//==============================
// state (defined in brain.cpp)
//==============================
extern IdleGen idle;
extern EmotionEngine emo;
extern TouchGestures touch;
extern LightSense light;
extern DayClock dayClock;
extern FaceParams base;
extern bool manual;
extern uint32_t now, manualUntil;  // manualUntil 0 = manual face holds until `auto` / next event
extern PAD holdPad;
extern uint32_t holdUntil;  // network {"emotion":..,"ttl":s} keeps the emotion pinned until then
extern uint32_t pomoEnd, pomoDoneUntil;
extern char weather[14], ciName[14];
extern float tempC;
extern bool hasTemp;
extern Settings settings;
extern CustomStore custom;
extern uint8_t ci;
extern FaceStyle style;
extern MindNet mindNet;
extern Diary diary;
extern Temper temper;
extern Wardrobe wardrobe;
extern Wardrobe mouthWardrobe;
extern FaceStyle homeStyle;
extern Needs needs;
extern Preferences store;
extern float petRate, pokeRate;  // decayed recent counts (half-life 5 min), mind net sense
extern uint32_t mindDirty, lastMindSave;
extern TouchPanel touchPanel;
extern float simLight;
extern bool logTouch, logPad;

//==============================
// brain.cpp
//==============================
void saveStyle();
void saveSettings();
Personality currentPersonality();
void applySettings();
void loadPersisted();
bool blobWrite(const char* key, const void* data, size_t len);  // /state/<key>.bin in the flash filesystem
size_t blobRead(const char* key, void* buf, size_t cap);
void mindSense(MindInputs& in);
void mindStep();
bool applyStyleText(const char* text);
void setBase(const FaceParams& p);
void pushEvent(uint8_t type, float k);
int dominantAnchor();
void startPomodoro(float minutes);
void setCi(uint8_t status, const char* name);
int ciFromString(const char* s);
void setWeather(float celsius, const char* text);

//==============================
// sdbackup.cpp (the SD card: backups, the diary log, saved looks)
//==============================
int sdBackup();  // flash contents -> /teevee/backup/ (count, -1 = no card)
int sdRestore();
const char* sdUpdate();  // firmware from /teevee/update/firmware.bin: restarts into it; returns why not
size_t sdDiaryLog(char* out, size_t n);
size_t sdStoreJson(char* out, size_t n);
bool sdStoreInstall(const char* name);  // copies /teevee/store/<name>.lua to the board's flash
bool packInstall(fs::FS& fs, const char* path, bool all, char* report, size_t n);
bool packExport(fs::FS& fs, const char* path, bool all, char* err, size_t n);
extern ScreenIdle screenIdle;
extern Tastes tastes;
extern char suggestedApp[12];
bool loadBehaviour(char* err);
void playEmoteText(uint8_t e,
                   const char* text);  // an emote showing a short text instead of its own decoration
void sdLogDiaryDay(const Diary::Day& d);
int sdLooksJson(char* out, size_t n);
bool sdLookSave(const char* name);
bool sdLookLoad(const char* name);
bool sdLookDelete(const char* name);

//==============================
// studio.cpp
//==============================
extern uint8_t emote;
extern uint32_t emoteStart, emoteEnd, emoteMs, lastAutoEmote;
void saveCustom();
bool validEmote(int e);
void playEmote(uint8_t e);
bool emoteActive();
FaceParams emoteFaceAt(float t);
bool showFace(const char* name, uint32_t ttlMs);
bool setFaceField(const char* name, float v);
bool parseEmote(const char* spec, CustomEmote& e);
size_t customJson(char* out, size_t n);

//==============================
// protocol.cpp
//==============================
int stateJson(char* buf, size_t n);
void onNetMessage(const char* json, size_t len, uint8_t client);

//==============================
// console.cpp
//==============================
void readSerial();
void printBrain();
}  // namespace brain
