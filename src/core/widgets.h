#pragma once
#include <stdint.h>

// Feature panels beside the mouth (bottom-left / bottom-right corners). Filled on core 0, drawn on core 1,
// shared under the same lock as the target FaceParams. Plain text only: the renderer just lays it out.
enum CiStatus : uint8_t { CI_NONE, CI_PASS, CI_FAIL, CI_RUNNING };

struct Widgets {
    char clock[8];
    char leftSub[20];
    bool pomoActive;
    char temp[8];
    char weather[14];
    uint8_t ci;
    char ciName[14];
    bool setup;
    char apSsid[16];
    char apPass[12];
    char pin[5];
    bool panels;
};
