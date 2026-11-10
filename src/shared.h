#pragma once
// State shared between the two cores. Core 0 (brain.cpp) writes the target face, panel text and style;
// core 1 (main.cpp) reads them each frame and owns the display. Guarded by one spinlock.
#include <Arduino.h>
#include "core/emotion.h"
#include "core/face_params.h"
#include "core/style.h"
#include "core/widgets.h"
#include "core/emotes.h"

extern portMUX_TYPE g_mux;
extern FaceParams g_target;
extern Widgets g_widgets;
extern FaceStyle g_style;
extern volatile bool g_bench, g_stats;
extern volatile bool g_shotWanted;  // core 0 asks for a screenshot on the SD card, core 1 takes it
extern volatile int g_madctlReq;  // core 0 asks for a display orientation, core 1 applies it
extern volatile uint8_t g_madctl;  // orientation in use (touch mapping on core 0 follows it)
extern volatile int g_parEX, g_parEY, g_parMX, g_parMY;  // the parallax shift of the eyes and the mouth, pixels (core 0 -> renderer)
extern volatile float g_dayHue, g_dayVal, g_pupilScale, g_turnX, g_turnY, g_roll,
    g_fisheye;  // day tint of the face colour (core 0 -> renderer), see daytint.h
extern volatile float
    g_glitchScale;  // how much of the style's own glitch shows now (activity; core 0 -> renderer)
extern volatile float g_glitchDrive;  // the mind net's wish for a picture glitch 0..1 (core 0 -> renderer)
extern volatile bool
    g_screenOff;  // core 0: nobody has done anything for a while; core 1 turns the display off and stops drawing
extern volatile bool g_darkMode;  // core 0: the setting `dark_mode`; core 1 hands it to the display driver and draws the screen again
extern volatile uint8_t g_brightness;  // backlight wanted by core 0 (settings, night dim), applied by core 1
extern Overlay g_overlay;  // current emote overlay (core 0 -> renderer)
extern QueueHandle_t g_events;

inline void setTarget(const FaceParams& p) {
  portENTER_CRITICAL(&g_mux);
  g_target = p;
  portEXIT_CRITICAL(&g_mux);
}
inline void setWidgets(const Widgets& w) {
  portENTER_CRITICAL(&g_mux);
  g_widgets = w;
  portEXIT_CRITICAL(&g_mux);
}
inline void setOverlay(const Overlay& o) {
  portENTER_CRITICAL(&g_mux);
  g_overlay = o;
  portEXIT_CRITICAL(&g_mux);
}
inline void setStyleShared(const FaceStyle& s) {
  portENTER_CRITICAL(&g_mux);
  g_style = s;
  portEXIT_CRITICAL(&g_mux);
}
