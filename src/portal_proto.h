#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

namespace portal {

static size_t dnsReply(const uint8_t* q, size_t n, const uint8_t ip[4], uint8_t* out, size_t cap) {
  if (n < 12 || n + 16 > cap) return 0;
  if (q[2] & 0x80) return 0;
  if ((q[2] >> 3 & 0x0F) != 0) return 0;
  if (q[4] != 0 || q[5] != 1) return 0;
  size_t p = 12;
  while (p < n && q[p]) {
    if (q[p] & 0xC0) return 0;
    p += q[p] + 1;
  }
  if (p + 5 > n) return 0;
  uint16_t qtype = q[p + 1] << 8 | q[p + 2];
  size_t qend = p + 5;
  memcpy(out, q, qend);
  out[2] = 0x84 | (q[2] & 0x01);
  out[3] = 0x80;
  out[6] = 0;
  out[7] = qtype == 1 ? 1 : 0;
  out[8] = out[9] = out[10] = out[11] = 0;
  if (qtype != 1) return qend;
  static const uint8_t ans[] = {0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4};
  memcpy(out + qend, ans, sizeof ans);
  memcpy(out + qend + sizeof ans, ip, 4);
  return qend + sizeof ans + 4;
}

struct Request {
    char method[8];
    char path[64];
    char host[64];
    char token[40];  // X-Teevee-Token (web page login), "" if absent
    const char* body;
    size_t bodyLen;
};

enum Parse { NEED_MORE, DONE, BAD };

static void copyValue(const char* v, char* out, size_t n) {
  while (*v == ' ') v++;
  size_t k = 0;
  while (v[k] && v[k] != '\r' && k < n - 1) out[k] = v[k], k++;
  out[k] = 0;
}

static Parse parseHead(const char* buf, size_t n, Request& r, size_t& clen) {
  const char* hend = strstr(buf, "\r\n\r\n");
  if (!hend) return n > 2048 ? BAD : NEED_MORE;
  const char* sp1 = strchr(buf, ' ');
  if (!sp1 || sp1 - buf >= (int)sizeof r.method) return BAD;
  const char* sp2 = strchr(sp1 + 1, ' ');
  if (!sp2 || sp2 > hend) return BAD;
  memcpy(r.method, buf, sp1 - buf);
  r.method[sp1 - buf] = 0;
  size_t plen = sp2 - sp1 - 1;
  if (plen >= sizeof r.path) plen = sizeof r.path - 1;
  memcpy(r.path, sp1 + 1, plen);
  r.path[plen] = 0;
  if (char* qm = strchr(r.path, '?')) *qm = 0;
  r.host[0] = 0;
  r.token[0] = 0;
  clen = 0;
  for (const char* line = strstr(buf, "\r\n") + 2; line < hend; line = strstr(line, "\r\n") + 2) {
    if (!strncasecmp(line, "Content-Length:", 15)) {
      clen = strtoul(line + 15, nullptr, 10);
    } else if (!strncasecmp(line, "Host:", 5)) {
      copyValue(line + 5, r.host, sizeof r.host);
    } else if (!strncasecmp(line, "X-Teevee-Token:", 15)) {
      copyValue(line + 15, r.token, sizeof r.token);
    }
  }
  r.body = hend + 4;
  r.bodyLen = n - (size_t)(r.body - buf);
  return DONE;
}

static Parse parseBody(size_t clen, size_t maxBody, Request& r) {
  if (clen > maxBody) return BAD;
  if (r.bodyLen < clen) return NEED_MORE;
  r.bodyLen = clen;
  return DONE;
}

static inline Parse parseRequest(const char* buf, size_t n, size_t maxBody, Request& r) {
  size_t clen;
  Parse p = parseHead(buf, n, r, clen);
  return p == DONE ? parseBody(clen, maxBody, r) : p;
}

}  // namespace portal
