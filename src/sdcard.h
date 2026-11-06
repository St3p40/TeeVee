#pragma once
#include <Arduino.h>
#include <FS.h>
#include "board.h"

namespace sdcard {
bool begin();
bool mounted();
fs::FS& fs();
uint64_t totalBytes();
uint64_t usedBytes();
const char* typeName();
// A screenshot, /teevee/shots/shotNNN.ba (a Bitmap Array file, type b2, 320 x 240). Begin fills it with the background; every strip the display gets
// while it is open lands in it (shotPush); end closes it. Called on the display's core. False: no card, or it cannot be written.
bool shotBegin(uint16_t background);
void shotPush(int x, int y, int w, int h, const uint16_t* pixels);
void shotEnd();
}  // namespace sdcard
