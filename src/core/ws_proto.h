#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

// WebSocket (RFC 6455) server-side pieces: handshake (own SHA-1 + base64), frame parse/build.
// Pure functions, no Arduino deps, so they're host-tested against the Python client.
namespace wsproto {

static void sha1(const uint8_t* data, size_t len, uint8_t out[20]) {
  uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
  auto rol = [](uint32_t x, int n) { return (x << n) | (x >> (32 - n)); };
  uint64_t bits = (uint64_t)len * 8;
  size_t total = ((len + 8) / 64 + 1) * 64;
  for (size_t off = 0; off < total; off += 64) {
    uint8_t blk[64];
    for (int i = 0; i < 64; i++) {
      size_t k = off + i;
      blk[i] = k < len          ? data[k]
               : k == len       ? 0x80
               : k >= total - 8 ? (uint8_t)(bits >> (8 * (total - 1 - k)))
                                : 0;
    }
    uint32_t w[80];
    for (int i = 0; i < 16; i++)
      w[i] = (uint32_t)blk[4 * i] << 24 | blk[4 * i + 1] << 16 | blk[4 * i + 2] << 8 | blk[4 * i + 3];
    for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; i++) {
      uint32_t f, k;
      if (i < 20) {
        f = (b & c) | (~b & d);
        k = 0x5A827999;
      } else if (i < 40) {
        f = b ^ c ^ d;
        k = 0x6ED9EBA1;
      } else if (i < 60) {
        f = (b & c) | (b & d) | (c & d);
        k = 0x8F1BBCDC;
      } else {
        f = b ^ c ^ d;
        k = 0xCA62C1D6;
      }
      uint32_t t = rol(a, 5) + f + e + k + w[i];
      e = d;
      d = c;
      c = rol(b, 30);
      b = a;
      a = t;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
  }
  for (int i = 0; i < 20; i++) out[i] = (uint8_t)(h[i / 4] >> (24 - 8 * (i % 4)));
}

static void base64(const uint8_t* in, size_t n, char* out) {
  static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t o = 0;
  for (size_t i = 0; i < n; i += 3) {
    uint32_t v = (uint32_t)in[i] << 16 | (i + 1 < n ? in[i + 1] << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
    out[o++] = T[v >> 18 & 63];
    out[o++] = T[v >> 12 & 63];
    out[o++] = i + 1 < n ? T[v >> 6 & 63] : '=';
    out[o++] = i + 2 < n ? T[v & 63] : '=';
  }
  out[o] = 0;
}

// The login token a client sent in its upgrade request (X-Teevee-Token, as on the HTTP API). False if there is none or it does not fit.
static bool requestToken(const char* req, char* out, size_t n) {
  for (const char* line = req; line && *line && strncmp(line, "\r\n", 2);) {
    if (!strncasecmp(line, "X-Teevee-Token:", 15)) {
      const char* v = line + 15;
      while (*v == ' ') v++;
      size_t len = strcspn(v, " \r\n");
      if (!len || len >= n) return false;
      memcpy(out, v, len);
      out[len] = 0;
      return true;
    }
    line = strchr(line, '\n');
    if (line) line++;
  }
  return false;
}

static void acceptKey(const char* key, char out[29]) {
  char buf[96];
  int n = snprintf(buf, sizeof buf, "%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11", key);
  uint8_t dig[20];
  sha1((const uint8_t*)buf, (size_t)n, dig);
  base64(dig, 20, out);
}

static size_t handshake(const char* req, char* out, size_t n) {
  const char* line = req;
  char key[64] = "";
  while (line && *line) {
    if (!strncasecmp(line, "Sec-WebSocket-Key:", 18)) {
      const char* v = line + 18;
      while (*v == ' ') v++;
      size_t k = 0;
      while (v[k] && v[k] != '\r' && v[k] != '\n' && v[k] != ' ' && k < sizeof key - 1) key[k] = v[k], k++;
      key[k] = 0;
    }
    line = strchr(line, '\n');
    if (line) line++;
  }
  if (!key[0]) return 0;
  char acc[29];
  acceptKey(key, acc);
  int len = snprintf(out, n,
                     "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                     "Sec-WebSocket-Accept: %s\r\n\r\n",
                     acc);
  return len > 0 && (size_t)len < n ? (size_t)len : 0;
}

// Host part of the request's Origin header ("http://teevee.local:80" -> "teevee.local") into out; false if there is none.
// Browsers always send Origin with a WebSocket upgrade (and ignore CORS for it); the PC tools send none.
static bool originHost(const char* req, char* out, size_t n) {
  for (const char* line = req; line && *line; line = strchr(line, '\n'), line = line ? line + 1 : nullptr) {
    if (strncasecmp(line, "Origin:", 7)) continue;
    const char* v = line + 7;
    while (*v == ' ') v++;
    const char* eol = strchr(v, '\n');
    const char* sch = strstr(v, "://");
    if (sch && (!eol || sch < eol)) v = sch + 3;
    size_t k = 0;
    while (v[k] && v[k] != ':' && v[k] != '/' && v[k] != '\r' && v[k] != '\n' && v[k] != ' ' && k < n - 1)
      out[k] = v[k], k++;
    out[k] = 0;
    return true;
  }
  return false;
}

enum Parse { NEED_MORE, FRAME, BAD };

struct Frame {
    uint8_t opcode;
    bool fin;
    uint8_t* payload;
    size_t len;
    size_t consumed;
};

// Parses one client frame from buf (client frames must be masked). Payloads over maxLen are BAD.
static Parse parse(uint8_t* buf, size_t n, size_t maxLen, Frame& f) {
  if (n < 2) return NEED_MORE;
  f.fin = buf[0] & 0x80;
  f.opcode = buf[0] & 0x0F;
  if (!(buf[1] & 0x80)) return BAD;
  uint64_t len = buf[1] & 0x7F;
  size_t pos = 2;
  if (len == 126) {
    if (n < 4) return NEED_MORE;
    len = (uint64_t)buf[2] << 8 | buf[3];
    pos = 4;
  } else if (len == 127) {
    if (n < 10) return NEED_MORE;
    len = 0;
    for (int i = 0; i < 8; i++) len = len << 8 | buf[2 + i];
    pos = 10;
  }
  if (len > maxLen) return BAD;
  if (n < pos + 4 + len) return NEED_MORE;
  const uint8_t* mask = buf + pos;
  f.payload = buf + pos + 4;
  for (size_t i = 0; i < len; i++) f.payload[i] ^= mask[i & 3];
  f.len = (size_t)len;
  f.consumed = pos + 4 + (size_t)len;
  return FRAME;
}

static size_t header(uint8_t opcode, size_t len, uint8_t out[4]) {
  out[0] = 0x80 | opcode;
  if (len < 126) {
    out[1] = (uint8_t)len;
    return 2;
  }
  out[1] = 126;
  out[2] = (uint8_t)(len >> 8);
  out[3] = (uint8_t)len;
  return 4;
}

}  // namespace wsproto
