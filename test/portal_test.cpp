#include <cstdio>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include "portal_proto.h"
int fails = 0;
#define CHECK(c)                                                                                             \
  do {                                                                                                       \
    if (!(c)) {                                                                                              \
      printf("FAIL line %d: %s\n", __LINE__, #c);                                                            \
      fails++;                                                                                               \
    }                                                                                                        \
  } while (0)
int main(int argc, char** argv) {
  portal::Request r;
  const char* g = "GET /api/scan?x=1 HTTP/1.1\r\nHost: 192.168.4.1\r\nAccept: */*\r\n\r\n";
  CHECK(portal::parseRequest(g, strlen(g), 1024, r) == portal::DONE && !strcmp(r.method, "GET") &&
        !strcmp(r.path, "/api/scan") && !strcmp(r.host, "192.168.4.1") && r.bodyLen == 0);
  std::string p =
      "POST /api/wifi HTTP/1.1\r\nhost: teevee.local\r\ncontent-length: 27\r\n\r\n{\"ssid\":\"My Net\",\"pass\":\"x";
  CHECK(portal::parseRequest(p.c_str(), p.size(), 1024, r) == portal::NEED_MORE);
  p += "\"}";
  CHECK(portal::parseRequest(p.c_str(), p.size(), 1024, r) == portal::DONE && r.bodyLen == 27 &&
        !strncmp(r.body, "{\"ssid\"", 7) && !strcmp(r.host, "teevee.local"));
  const char* big = "POST / HTTP/1.1\r\nContent-Length: 99999\r\n\r\n";
  CHECK(portal::parseRequest(big, strlen(big), 1024, r) == portal::BAD);
  CHECK(portal::parseRequest("GET / HTTP/1.1\r\n", 16, 1024, r) == portal::NEED_MORE);
  CHECK(portal::parseRequest("garbage\r\n\r\n", 11, 1024, r) == portal::BAD);
  printf("http tests: %s\n", fails ? "FAILED" : "ok");
  if (argc < 2) return fails;
  int s = socket(AF_INET, SOCK_DGRAM, 0);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(atoi(argv[1]));
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  bind(s, (sockaddr*)&a, sizeof a);
  const uint8_t ip[4] = {192, 168, 4, 1};
  for (int i = 0; i < 10; i++) {
    uint8_t q[512], out[600];
    sockaddr_in from{};
    socklen_t fl = sizeof from;
    ssize_t n = recvfrom(s, q, sizeof q, 0, (sockaddr*)&from, &fl);
    size_t len = portal::dnsReply(q, n, ip, out, sizeof out);
    if (len) sendto(s, out, len, 0, (sockaddr*)&from, fl);
  }
  return fails;
}
