#pragma once
#include <stddef.h>
#include <stdint.h>

uint16_t* tvAllocPixels(size_t bytes);  // a zeroed-or-not buffer for pixels; DMA-capable memory on the board
uint32_t tvMicros();
