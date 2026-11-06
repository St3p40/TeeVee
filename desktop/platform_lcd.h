#pragma once
#include <stdint.h>

namespace desktop {
constexpr int W = 320, H = 240;
const uint16_t* framebuffer();
void toArgb(uint32_t* out);
void toRgba(uint32_t* out);
void setTransparentKey(uint16_t key, bool on);
}  // namespace desktop
