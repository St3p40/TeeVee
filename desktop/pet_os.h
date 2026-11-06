#pragma once
#include <SDL3/SDL.h>
#include "pet.h"

namespace pet {
int otherWindows(SDL_Window* self, PetRect* out, int max);
bool autostartSupported();
bool autostartOn();
bool setAutostart(bool on);
// A tiny greyscale picture of the main screen (w x h, 0..255 each), for the screen senses (brightness, how much it changes): only when the
// owner switched "look at the screen" on. Kept in memory, never saved or sent. False where it cannot (macOS: never asks for permission;
// Wayland: no). Windows: GDI. Linux: X11.
bool screenThumb(uint8_t* grey, int w, int h);
}  // namespace pet
