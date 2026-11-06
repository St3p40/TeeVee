// Packs: a whole file walks to its end; a damaged one of any kind is refused, wherever it was cut. Run: make -C test
#include <stdio.h>
#include <string>
#include "pack.h"
static int fails = 0;
#define CHECK(c)                                                                                             \
  do {                                                                                                       \
    if (!(c)) {                                                                                              \
      printf("FAIL line %d: %s\n", __LINE__, #c);                                                            \
      fails++;                                                                                               \
    }                                                                                                        \
  } while (0)
struct Mem {
    const std::string& s;
    bool operator()(uint32_t off, void* buf, size_t n) const {
      if (off + n > s.size()) return false;
      memcpy(buf, s.data() + off, n);
      return true;
    }
};
static std::string entry(const char* kind, const char* name, const std::string& body) {
  char h[48];
  packHead(h, sizeof h, kind, name, (uint32_t)body.size());
  return h + body + "\n";
}
int main() {
  const std::string binary("a\nend 0\nb\0c\n\xff", 14);
  const std::string body = entry("look", "current", "hue 40") +
                           entry("app", "snake", "function draw() end\n") + entry("image", "logo", binary) +
                           entry("zzz", "future", "x");
  const std::string good = PACK_MAGIC + body + "end 4\n";
  const auto check = [](const std::string& s, int* n = nullptr) {
    return packCheck(Mem{s}, (uint32_t)s.size(), n);
  };
  int n = 0;
  CHECK(check(good, &n) && n == 4);
  {
    const Mem read{good};
    uint32_t at;
    CHECK(packOpen(read, (uint32_t)good.size(), at));
    PackEntry e;
    int seen = 0;
    CHECK(packNext(read, (uint32_t)good.size(), at, e, seen) == PACK_ENTRY && !strcmp(e.kind, "look") &&
          !strcmp(e.name, "current") && e.size == 6);
    CHECK(good.substr(e.offset, e.size) == "hue 40");
    packNext(read, (uint32_t)good.size(), at, e, seen);
    CHECK(packNext(read, (uint32_t)good.size(), at, e, seen) == PACK_ENTRY && e.size == binary.size() &&
          good.substr(e.offset, e.size) == binary);
    CHECK(packNext(read, (uint32_t)good.size(), at, e, seen) == PACK_ENTRY && !strcmp(e.kind, "zzz"));
    CHECK(packNext(read, (uint32_t)good.size(), at, e, seen) == PACK_END);
  }
  for (size_t cut = 0; cut < good.size(); cut++) CHECK(!check(good.substr(0, cut)));
  CHECK(!check(""));
  CHECK(!check("teevee-pack 2\nend 0\n"));
  CHECK(check(std::string(PACK_MAGIC) + "end 0\n"));
  CHECK(!check(std::string(PACK_MAGIC) + "end 1\n"));
  CHECK(!check(good + "x"));
  const auto one = [&](const std::string& line) { return check(PACK_MAGIC + line + "end 1\n"); };
  CHECK(one("app ok 1\nx\n"));
  CHECK(!one("app ../etc 1\nx\n"));
  CHECK(!one("app a/b 1\nx\n"));
  CHECK(!one("app UP 1\nx\n"));
  CHECK(!one("app toolongname12 1\nx\n"));
  CHECK(!one("A1 ok 1\nx\n"));
  CHECK(!one("app ok 99999999\nx\n"));
  CHECK(!one("app ok 4294967295\nx\n"));  // would overflow
  CHECK(!one("app ok 1\nxy\n"));
  CHECK(!one("app ok\nx\n"));
  CHECK(!one("app ok 1 more\nx\n"));
  CHECK(packIsRobot("mind") && packIsRobot("settings") && !packIsRobot("app") && !packIsRobot("look") &&
        !packIsRobot("behaviour"));
  CHECK(!packNameOk("") && !packNameOk(nullptr) && packNameOk("a-b_1"));
  PackReport r;
  r.done("app");
  r.done("app");
  r.done("look");
  r.skip("mind");
  char text[120];
  r.text(text, sizeof text);
  CHECK(!strcmp(text, "installed 3 (app, look); skipped 1 (mind)"));
  for (int i = 0; i < 40; i++) r.done("someverylongkind"), r.skip(i % 2 ? "another" : "kindofthing");
  CHECK(r.text(text, sizeof text) > 0);
  printf("pack_test: %s\n", fails ? "FAILED" : "ok");
  return fails ? 1 : 0;
}
