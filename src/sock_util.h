#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <errno.h>
#include <lwip/sockets.h>

// Sends all of d[0..n) without blocking for long. The framework's WiFiClient::write() can block for several seconds
// when the peer stopped reading, which would stall the whole brain loop (core 0); this gives a peer at most
// budgetMs to take the data and returns false if it didn't (the caller should drop the connection).
static inline bool sendAll(WiFiClient& sock, const uint8_t* d, size_t n, uint32_t budgetMs = 200) {
  const int fd = sock.fd();
  if (fd < 0) return false;
  const uint32_t t0 = millis();
  while (n) {
    int r = send(fd, d, n, MSG_DONTWAIT);
    if (r > 0) {
      d += r;
      n -= r;
    } else if (r == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != ENOMEM && errno != ENOBUFS)) {
      return false;
    } else if (millis() - t0 >= budgetMs) {
      return false;
    } else {
      vTaskDelay(1);
    }
  }
  return true;
}
