#pragma once
#include <Arduino.h>
#include "core/emotion.h"
#include "core/hearing.h"

// Sound on boards with an audio codec (BOARD_HAS_AUDIO): the microphone and the voice share one I2S port at 16 kHz, run by
// one task on core 0 in 20 ms blocks. The mic only measures loudness (Hearing); nothing is recorded or sent. The voice is
// synthesized (core/voice.h). While it speaks, and a moment after, the mic does not listen, so it never startles at
// itself. The speaker amplifier is on only while it speaks.

namespace audio {
void enable(bool mic, bool sound);  // the settings `hearing` and `sound`
void sensitivity(float s);
void volume(float v);
bool popEvent(HearingOut& out);
bool talking();   // someone is heard talking
float level();    // 0..1 above the room's floor
void say(uint8_t ev, const PAD& mood, float k);
bool speaking();
const char* status();  // for the console
}  // namespace audio
