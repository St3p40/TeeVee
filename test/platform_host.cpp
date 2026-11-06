#include <chrono>
#include <stdlib.h>
#include "platform.h"

uint16_t* tvAllocPixels(size_t bytes) { return (uint16_t*)malloc(bytes); }
uint32_t tvMicros() {
  static const auto t0 = std::chrono::steady_clock::now();
  return (uint32_t)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() -
                                                                         t0)
      .count();
}
