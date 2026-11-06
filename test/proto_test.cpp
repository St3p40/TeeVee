#include <cstdio>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include "json.h"
#include "ws_proto.h"
int fails = 0;
#define CHECK(c)                                                                                             \
  do {                                                                                                       \
    if (!(c)) {                                                                                              \
      printf("FAIL line %d: %s\n", __LINE__, #c);                                                            \
      fails++;                                                                                               \
    }                                                                                                        \
  } while (0)
static bool parse(const char* s, JsonView& r) { return jsonParse(s, strlen(s), r); }
int main(int argc, char** argv) {
  char acc[29];
  wsproto::acceptKey("dGhlIHNhbXBsZSBub25jZQ==", acc);
  CHECK(!strcmp(acc, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));
  uint8_t d[20];
  wsproto::sha1((const uint8_t*)"abc", 3, d);
  CHECK(d[0] == 0xa9 && d[19] == 0x9d);
  std::string m(1000, 'a');
  wsproto::sha1((const uint8_t*)m.data(), 1000, d);
  CHECK(d[0] == 0x29 && d[1] == 0x1e);
  char host[64];  // Origin: browsers send it, the PC tools do not
  CHECK(!wsproto::originHost("GET / HTTP/1.1\r\nHost: x\r\n\r\n", host, sizeof host));
  {  // the login token of an upgrade request
    char t[40];
    CHECK(!wsproto::requestToken("GET / HTTP/1.1\r\nHost: x\r\n\r\n", t, sizeof t));
    CHECK(
        wsproto::requestToken("GET / HTTP/1.1\r\nx-teevee-token:  abc123\r\nHost: x\r\n\r\n", t, sizeof t) &&
        !strcmp(t, "abc123"));
    CHECK(!wsproto::requestToken("GET / HTTP/1.1\r\nX-Teevee-Token:\r\n\r\n", t, sizeof t));
    CHECK(!wsproto::requestToken("GET / HTTP/1.1\r\nX-Teevee-Token: abcdef\r\n\r\n", t,
                                 4));  // too long: refused, never cut
    CHECK(!wsproto::requestToken("GET / HTTP/1.1\r\n\r\nX-Teevee-Token: abc\r\n\r\n", t, sizeof t));
  }
  CHECK(wsproto::originHost("GET / HTTP/1.1\r\norigin: http://Evil.example:8080/a\r\n\r\n", host,
                            sizeof host) &&
        !strcmp(host, "Evil.example"));
  CHECK(wsproto::originHost("GET / HTTP/1.1\r\nOrigin: null\r\n\r\n", host, sizeof host) &&
        !strcmp(host, "null"));
  CHECK(wsproto::originHost("GET / HTTP/1.1\r\nOrigin:   http://192.168.4.1\r\nX: a://b\r\n\r\n", host,
                            sizeof host) &&
        !strcmp(host, "192.168.4.1"));
  JsonView r;
  char s[32];
  CHECK(parse("{\"event\":\"pet\",\"intensity\":0.5}", r) && r.get("event").eq("pet") &&
        r.get("intensity").num(1) == 0.5f);
  CHECK(parse(" {\"emotion\":{\"v\":-0.6,\"a\":0.3,\"d\":1e-1},\"ttl\":10} ", r));
  CHECK(r.get("emotion").isObject() && r.get("emotion").get("v").num(0) == -0.6f &&
        r.get("emotion").get("d").num(0) == 0.1f && r.get("ttl").num(0) == 10);
  CHECK(!r.get("nope").ok() && r.get("nope").num(7) == 7);
  CHECK(parse("{\"style\":{\"hue\":200,\"bg\":[10, 0 ,20],\"tint\":true}}", r));
  JsonView st = r.get("style"), bg = st.get("bg");
  CHECK(bg.isArray() && bg.at(0).num(-1) == 10 && bg.at(2).num(-1) == 20 && !bg.at(3).ok() &&
        st.get("tint").num(0) == 1);
  const char* it = nullptr;
  char k[16];
  JsonView v;
  int n = 0;
  while (st.next(it, k, sizeof k, v)) n++;
  CHECK(n == 3);
  CHECK(parse("{\"show\":{\"ci\":{\"status\":\"fail\",\"name\":\"ma\\\"in\"},\"weather\":\"off\"}}", r));
  CHECK(r.get("show").get("ci").get("name").str(s, sizeof s) && !strcmp(s, "ma\"in") &&
        r.get("show").get("weather").eq("off"));
  CHECK(parse("{\"a\":[],\"b\":{},\"c\":null,\"d\":false}", r) && r.get("c").isNull() &&
        r.get("d").num(5) == 0);
  const char* bad[] = {"",
                       "{",
                       "{\"a\":}",
                       "{\"a\":1,}",
                       "{\"a\" 1}",
                       "[1 2]",
                       "{\"a\":tru}",
                       "{\"a\":1} x",
                       "\"unterminated",
                       "{\"a\":[[[[[[[[[[[[[[[[[[1]]]]]]]]]]]]]]]]]]}",
                       "{\"a\":-nan}",
                       "{\"a\":-inf}",
                       "{\"a\":-infinity}",
                       "{\"a\":0x10}",
                       "{\"a\":01}",
                       "{\"a\":1.}",
                       "{\"a\":.5}",
                       "{\"a\":1e}",
                       "{\"a\":-}"};
  CHECK(parse("{\"a\":1e999,\"b\":-0.5E+2,\"c\":0}", r) && r.get("a").num(3) == 3 &&
        r.get("b").num(0) == -50 && r.get("c").num(1) == 0);
  for (const char* b : bad) {
    if (parse(b, r)) {
      printf("FAIL accepted bad json: %s\n", b);
      fails++;
    }
  }
  printf("unit tests: %s\n", fails ? "FAILED" : "ok");
  if (argc < 2) return fails;
  int srv = socket(AF_INET, SOCK_STREAM, 0), one = 1;
  setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(atoi(argv[1]));
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  bind(srv, (sockaddr*)&a, sizeof a);
  listen(srv, 1);
  printf("listening\n");
  fflush(stdout);
  int c = accept(srv, nullptr, nullptr);
  char req[1024];
  size_t rl = 0;
  while (!strstr(std::string(req, rl).c_str(), "\r\n\r\n")) {
    ssize_t k2 = read(c, req + rl, sizeof req - 1 - rl);
    if (k2 <= 0) return 1;
    rl += k2;
  }
  req[rl] = 0;
  char resp[256];
  size_t hl = wsproto::handshake(req, resp, sizeof resp);
  write(c, resp, hl);
  uint8_t buf[2048];
  size_t bl = 0;
  for (;;) {
    ssize_t k2 = read(c, buf + bl, sizeof buf - bl);
    if (k2 <= 0) break;
    bl += k2;
    wsproto::Frame f;
    wsproto::Parse p;
    while ((p = wsproto::parse(buf, bl, 1024, f)) == wsproto::FRAME) {
      if (f.opcode == 8) {
        close(c);
        return fails;
      }
      std::string txt((char*)f.payload, f.len);
      JsonView root;
      bool okj = jsonParse(txt.c_str(), txt.size(), root);
      char ev[16] = "-";
      root.get("event").str(ev, sizeof ev);
      std::string out = "{\"ok\":" + std::string(okj ? "true" : "false") + ",\"event\":\"" + ev +
                        "\",\"len\":" + std::to_string(f.len) + ",\"pad\":\"" +
                        std::string(f.len > 200 ? 300 : 0, 'x') + "\"}";
      uint8_t h[4];
      size_t hn = wsproto::header(1, out.size(), h);
      write(c, h, hn);
      write(c, out.data(), out.size());
      memmove(buf, buf + f.consumed, bl - f.consumed);
      bl -= f.consumed;
    }
    if (p == wsproto::BAD) {
      printf("bad frame\n");
      break;
    }
  }
  return fails;
}
