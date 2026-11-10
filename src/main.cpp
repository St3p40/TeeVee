//  ######### ####### ####### ##   ## ####### #######
//     ##     ##      ##      ##   ## ##      ##
//     ##     #####   #####   ##   ## #####   #####
//     ##     ##      ##       ## ##  ##      ##
//     ##     ####### #######   ###   ####### #######
// MADE BY ST3P40
#include <Arduino.h>
#include <Preferences.h>
#include "app_runtime.h"
#include "board.h"
#include "brain.h"
#include "core/lcd.h"
#include "core/mapper.h"
#include "core/idle.h"
#include "core/render_common.h"
#include "core/renderer.h"
#include "sdcard.h"
#include "shared.h"
#include "core/spring.h"

// Core 1: display side only. Core 0 runs the brain (brain.cpp); they meet in shared.h.
static Lcd lcd;
static FaceRenderer renderer;
static FaceSpring spring;

portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
FaceParams g_target = sleepingPose(mapEmotion(PERSONALITY.baseline));
Widgets g_widgets{};
FaceStyle g_style = styleInitial();
volatile bool g_bench = false, g_stats = false;
volatile int g_madctlReq = -1;
volatile uint8_t g_madctl = LCD_MADCTL;
volatile uint8_t g_brightness = 200;
volatile bool g_screenOff = false;
volatile bool g_darkMode = false;
volatile float g_glitchDrive = 0;
volatile float g_glitchScale = 1;
volatile int g_parEX = 0, g_parEY = 0, g_parMX = 0, g_parMY = 0;
volatile float g_dayHue = 0, g_dayVal = 1, g_pupilScale = 1, g_turnX = 0, g_turnY = 0, g_roll = 0, g_fisheye = 0;
Overlay g_overlay{};
QueueHandle_t g_events;

void setup() {
  for (int pin : BOARD_QUIET_PINS)
    if (pin >= 0) {
      pinMode(pin, OUTPUT);
      digitalWrite(pin, LOW);
    }
  if (BOARD_POWER >= 0) {
    pinMode(BOARD_POWER, OUTPUT);
    digitalWrite(BOARD_POWER, HIGH);
  }
  if (!BOARD_HAS_SD && BOARD_SD_CS >= 0) {
    pinMode(BOARD_SD_CS, OUTPUT);
    digitalWrite(BOARD_SD_CS, HIGH);
  }
  Serial.begin(115200);
  Preferences p;
  p.begin("teevee_x", true);
  uint8_t madctl = p.getUChar("madctl", LCD_MADCTL);
  p.end();
  lcd.begin(madctl);
  g_madctl = madctl;
  lcd.fillScreen(0);
  renderer.begin(&lcd);
  renderer.setVisibleColumns(LCD_CROP, LCD_CROP + LCD_PANEL_W);
  spring.reset(sleepingPose(mapEmotion(PERSONALITY.baseline)));
  g_events = xQueueCreate(16, sizeof(EmotionEvent));
  xTaskCreatePinnedToCore(brainTask, "brain", 10240, nullptr, 1, nullptr, 0);
}

static int faceFrame(const FaceStyle& style, bool bench) {
  static uint32_t lastStep = micros();
  FaceParams target;
  portENTER_CRITICAL(&g_mux);
  target = g_target;
  portEXIT_CRITICAL(&g_mux);
  renderer.setStyle(style);
  spring.setFeel(style.speed, style.bounce);
  uint32_t now = micros();
  float dt = fminf((now - lastStep) * 1e-6f, 0.05f);
  lastStep = now;
  spring.step(target, dt);
  Overlay ov;
  portENTER_CRITICAL(&g_mux);
  ov = g_overlay;
  portEXIT_CRITICAL(&g_mux);
  renderer.setGlitchDrive(g_glitchDrive);
  renderer.setGlitchScale(g_glitchScale);
  renderer.setDayTint(g_dayHue, g_dayVal);
  renderer.setPupilScale(g_pupilScale);
  renderer.setParallax(g_parEX, g_parEY, g_parMX, g_parMY);
  renderer.setTurn(g_turnX, g_turnY, g_roll);
  renderer.setFisheye(g_fisheye);
  int drawn = renderer.render(spring.value(), ov, millis(), bench);
  Widgets w;
  portENTER_CRITICAL(&g_mux);
  w = g_widgets;
  portEXIT_CRITICAL(&g_mux);
  return drawn + renderer.renderWidgets(w);
}

volatile bool g_shotWanted = false;
static AppFrame s_appCopy;
static uint32_t s_appVersion = 0;
static bool s_appRepaint =
    false;  // paint the copy even if the app hasn't sent a new frame (after a screen clear)
static bool s_appHasFace = false;

static int appFrame() {
  bool fresh = apps::takeFrame(s_appCopy, s_appVersion);
  if (fresh) {
    s_appHasFace = false;
    for (int i = 0; i < s_appCopy.count; i++) s_appHasFace = s_appHasFace || s_appCopy.cmds[i].op == OP_FACE;
  }
  if (!fresh && !s_appRepaint && !s_appHasFace) return 0;
  renderer.setAppFace(spring.value());
  s_appRepaint = false;
  return renderer.renderApp(s_appCopy);
}

// Core 1 (Arduino loop): the face, or the running app's screen.
void loop() {
  static uint32_t frames = 0, busyUs = 0, lastReport = millis();
  static bool inApp = false;
  FaceStyle style;
  portENTER_CRITICAL(&g_mux);
  style = g_style;
  portEXIT_CRITICAL(&g_mux);
  if (g_madctlReq >= 0) {
    lcd.setMadctl((uint8_t)g_madctlReq);
    g_madctl = (uint8_t)g_madctlReq;
    g_madctlReq = -1;
    lcd.fillScreen(0);
    renderer.forgetMargins();
    if (inApp) {
      renderer.enterApp();
      s_appRepaint = true;
    } else {
      renderer.setStyle(style);
      renderer.render(spring.value(), Overlay{}, millis(), true);
    }
  }
  static uint8_t brightness = 200;
  if (g_brightness != brightness) lcd.setBrightness(brightness = g_brightness);
  static bool dark = false;
  // The whole screen again, from nothing: after the display was off (it may have lost its picture, and the AMOLED's was
  // blacked out on purpose) and after dark mode changed (every pixel is sent differently).
  auto repaint = [&] {
    if (inApp) {
      renderer.enterApp();
      s_appRepaint = true;
    } else {
      renderer.setStyle(style);
      renderer.enterFace();
      renderer.render(spring.value(), Overlay{}, millis(), true);
    }
  };
  if (g_screenOff != dark) {
    lcd.setDisplayOn(!(dark = g_screenOff));
    Serial.printf("panel: %s\n", dark ? "off" : "on");
    if (!dark) {
      lcd.setBrightness(brightness);  // again, now that the panel is on: one sent while it was off may not have counted
      repaint();
    }
  }
  if (g_darkMode != g_lcdSparse) {
    g_lcdSparse = g_darkMode;
    if (!dark) repaint();
  }

  const bool appMode = apps::active();
  if (appMode != inApp) {
    inApp = appMode;
    if (appMode) {
      s_appCopy.reset();  // nothing from the previous app: black until the new one draws
      s_appHasFace = false;
      s_appRepaint = true;
      renderer.enterApp();
    } else {
      renderer.setStyle(style);
      renderer.enterFace();
    }
  }

  bool bench = g_bench;
  extern void (*g_lcdTee)(int, int, int, int, const uint16_t*);
  const bool shot = g_shotWanted && sdcard::shotBegin(rd::BG);
  g_shotWanted = false;
  if (shot) {
    g_lcdTee = sdcard::shotPush;
    bench = true;
    if (inApp) {
      renderer.enterApp();
      s_appRepaint = true;
    }
  }
  uint32_t t0 = micros();
  int drawn = dark && !shot ? 0 : inApp ? appFrame() : faceFrame(style, bench);
  if (shot) {
    g_lcdTee = nullptr;
    sdcard::shotEnd();
    Serial.println("sd: screenshot saved in /teevee/shots/");
  }
  if (drawn) {
    frames++;
    busyUs += micros() - t0;
  } else {
    vTaskDelay(dark ? pdMS_TO_TICKS(20) : 1);
  }

  if (millis() - lastReport >= 1000) {
    if (bench || g_stats)
      Serial.printf("fps=%lu frame=%luus heap=%lu min_heap=%lu\n", (unsigned long)frames,
                    (unsigned long)(frames ? busyUs / frames : 0), (unsigned long)ESP.getFreeHeap(),
                    (unsigned long)ESP.getMinFreeHeap());
    if (g_stats)
      Serial.printf("  region us/frame: eyeL=%lu eyeR=%lu mouth=%lu\n",
                    (unsigned long)(frames ? renderer.regionUs[0] / frames : 0),
                    (unsigned long)(frames ? renderer.regionUs[1] / frames : 0),
                    (unsigned long)(frames ? renderer.regionUs[2] / frames : 0));
    for (auto& us : renderer.regionUs) us = 0;
#ifndef TEEVEE_NO_WARP
    if (g_stats && renderer.warpFrames)  // per turned frame, not per frame: the head turns only part of the time
      Serial.printf("  warp us/turned frame (%lu): texture=%lu rows=%lu (effects %lu) push=%lu\n",
                    (unsigned long)renderer.warpFrames, (unsigned long)(renderer.warpUs[0] / renderer.warpFrames),
                    (unsigned long)(renderer.warpUs[1] / renderer.warpFrames),
                    (unsigned long)(renderer.warpUs[3] / renderer.warpFrames),
                    (unsigned long)(renderer.warpUs[2] / renderer.warpFrames));
    for (auto& us : renderer.warpUs) us = 0;
    renderer.warpFrames = 0;
#endif
    frames = busyUs = 0;
    lastReport = millis();
  }
}
