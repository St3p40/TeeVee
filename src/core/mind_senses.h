#pragma once
#include <math.h>
#include <stdint.h>

#include "emotion.h"
#include "idle.h"
#include "mind.h"
#include "needs.h"
#include "taste.h"

// The one place that turns the robot's state into the mind net's inputs. Each build gathers its raw readings into a
// MindSources (a field it cannot read keeps its default, and the net reads 0 for it: see mind.h for which build has what),
// then calls fillMindInputs, so the board and the desktop cannot disagree about a formula.

struct MindSources {
    PAD emotion{}, mood{};
    float hour = -1;                  // local hour 0..24, -1 = unknown
    int weekDay = -1, weekHour = 0;   // -1 = unknown
    float petRate = 0, pokeRate = 0;
    float light = -1;                 // 0..1 brightness, -1 = no reading
    uint32_t lastTouch = 0, lastEvent = 0, presentAt = 0;  // ms stamps, 0 = never
    float trust = 0;
    float eyeOpenL = 0.5f, eyeOpenR = 0.5f;
    bool emote = false;
    bool hasTouch = false;            // a touch screen or a mouse: the touch fields below are read only then
    bool touching = false;
    float tx = 0, ty = 0;
    bool hasMotion = false;           // an IMU (or the desktop's own motion model): the motion fields are read only then
    bool carried = false;
    float motion = 0, tiltX = 0, tiltY = 0;
    float batteryLow = 0, watched = 0, screen = 0;  // desktop only
};

inline void fillMindInputs(MindInputs& in, const MindSources& s, uint32_t now, const IdleGen& idle, const Needs& needs,
                           const Tastes& tastes) {
    in.v = s.emotion.v;
    in.a = s.emotion.a;
    in.d = s.emotion.d;
    in.mood = s.mood.v;
    in.hour = s.hour;
    if (s.weekDay >= 0) in.weekKnown = true, in.weekLike = tastes.weekLike(s.weekDay, s.weekHour);
    in.petRate = s.petRate;
    in.pokeRate = s.pokeRate;
    in.light = s.light >= 0 ? s.light * 2 - 1 : 0.f;
    in.sinceTouch = s.lastTouch ? (now - s.lastTouch) * 1e-3f : 1e9f;
    in.sinceEvent = s.lastEvent ? (now - s.lastEvent) * 1e-3f : 1e9f;
    in.idleSec = idle.idleSeconds(now);
    in.sleepy = idle.sleepiness(now);
    in.presence = s.presentAt ? expf(-(float)(now - s.presentAt) / 120000.f) : 0.f;  // (float first: unsigned would wrap)
    in.trust = s.trust;
    in.streak = tastes.streak;
    in.openness = (s.eyeOpenL + s.eyeOpenR) * 0.5f;
    in.emote = s.emote;
    in.needAttention = needs.attention;
    in.needRest = needs.rest;
    in.needPlay = needs.play;
    in.batteryLow = s.batteryLow;
    in.watched = s.watched;
    in.screen = s.screen;
    if (s.hasTouch) {
        in.touching = s.touching;
        in.tx = s.tx;
        in.ty = s.ty;
    }
    if (s.hasMotion) {
        in.motion = s.motion;
        in.tiltX = s.tiltX;
        in.tiltY = s.tiltY;
        in.carried = s.carried;
    }
}
