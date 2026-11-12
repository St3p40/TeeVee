#pragma once
#include <stddef.h>
#include <stdint.h>

uint16_t* tvAllocPixels(size_t bytes);  // a zeroed-or-not buffer for pixels; DMA-capable memory on the board
uint32_t tvMicros();
// Runs fn(arg) on the other core, and returns at once; tvBackgroundWait() returns when it has finished. One job at a time. Only the
// turned face uses it (the board: core 0 works on the lower rows while core 1 does the upper). The host and desktop builds run it
// at once.
void tvBackground(void (*fn)(void*), void* arg);
void tvBackgroundWait();
