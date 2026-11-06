#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// A pack: one file that holds looks, a behaviour file, apps, pictures and, in a full backup, what a robot has learned. The same file goes to the board, the desktop
// app and the web page; each takes what it can use and says what it skipped. Layout, all readable with a text editor apart from binary entries:
//     teevee-pack 1
//     <kind> <name> <bytes>      then that many bytes and a newline, for each entry
//     end <entries>
// The last line tells a cut-off file from a whole one. Kinds nobody knows yet are skipped, so a newer pack still installs. Never holds Wi-Fi details, the PIN or the
// login token. Pure logic, no Arduino deps. Host test: test/pack_test.cpp.
static constexpr char PACK_MAGIC[] = "teevee-pack 1\n";
static constexpr size_t PACK_MAGIC_LEN = sizeof PACK_MAGIC - 1;

struct PackEntry {
    char kind[12], name[12];
    uint32_t size, offset;
};

inline bool packNameOk(const char* n) {
  const size_t len = n ? strlen(n) : 0;
  if (len < 1 || len > 11) return false;
  for (const char* p = n; *p; p++)
    if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '-')) return false;
  return true;
}

// What a robot has learned and its own settings: only with the owner's say-so, because installing them replaces what is there. A shared pack has none of these.
inline bool packIsRobot(const char* kind) {
  static const char* const K[] = {"settings", "custom",  "tastes", "diary",  "mind",
                                  "wardrobe", "looknet", "cfg",    "appdata"};
  for (const char* k : K)
    if (!strcmp(k, kind)) return true;
  return false;
}

inline size_t packHead(char* out, size_t n, const char* kind, const char* name, uint32_t size) {
  return (size_t)snprintf(out, n, "%s %s %lu\n", kind, name, (unsigned long)size);
}

// read(offset, buffer, length) -> false if it cannot.
template <typename Read>
inline bool packLine(Read read, uint32_t size, uint32_t& at, char* line, size_t cap) {
  if (at >= size) return false;
  size_t n = size - at < cap - 1 ? size - at : cap - 1;
  if (!read(at, line, n)) return false;
  line[n] = 0;
  char* nl = (char*)memchr(line, '\n', n);
  if (!nl) return false;
  *nl = 0;
  at += (uint32_t)(nl - line) + 1;
  return true;
}

template <typename Read>
inline bool packOpen(Read read, uint32_t size, uint32_t& at) {
  char head[PACK_MAGIC_LEN];
  at = (uint32_t)PACK_MAGIC_LEN;
  return size >= PACK_MAGIC_LEN && read(0, head, PACK_MAGIC_LEN) && !memcmp(head, PACK_MAGIC, PACK_MAGIC_LEN);
}

enum PackStep { PACK_ENTRY, PACK_END, PACK_BAD };

template <typename Read>
inline PackStep packNext(Read read, uint32_t size, uint32_t& at, PackEntry& e, int& seen) {
  char line[48];
  if (!packLine(read, size, at, line, sizeof line)) return PACK_BAD;
  int count = -1, used = 0;
  if (sscanf(line, "end %d%n", &count, &used) == 1 && !line[used])
    return count == seen && at == size ? PACK_END : PACK_BAD;
  unsigned long bytes = 0;
  if (sscanf(line, "%11s %11s %lu%n", e.kind, e.name, &bytes, &used) != 3 || line[used]) return PACK_BAD;
  for (const char* p = e.kind; *p; p++)
    if (*p < 'a' || *p > 'z') return PACK_BAD;
  if (!packNameOk(e.name) || at >= size || bytes >= size - at)
    return PACK_BAD;  // (">=": its newline must fit too)
  e.size = (uint32_t)bytes, e.offset = at;
  at += e.size;
  char nl;
  if (!read(at, &nl, 1) || nl != '\n') return PACK_BAD;
  at += 1;
  seen++;
  return PACK_ENTRY;
}

// Walks the whole file, so that nothing is installed from a damaged one. entries: how many.
template <typename Read>
inline bool packCheck(Read read, uint32_t size, int* entries = nullptr) {
  uint32_t at;
  if (!packOpen(read, size, at)) return false;
  PackEntry e;
  int seen = 0;
  for (;;) {
    const PackStep s = packNext(read, size, at, e, seen);
    if (s == PACK_BAD) return false;
    if (s == PACK_END) break;
  }
  if (entries) *entries = seen;
  return true;
}

struct PackReport {
    int installed = 0, skipped = 0;
    char what[80] = "", why[60] = "";
    static void note(char* list, size_t n, const char* kind) {
      if (strstr(list, kind)) return;
      const size_t k = strlen(list);
      if (k + strlen(kind) + 3 < n) snprintf(list + k, n - k, "%s%s", k ? ", " : "", kind);
    }
    void done(const char* kind) { installed++, note(what, sizeof what, kind); }
    void skip(const char* kind) { skipped++, note(why, sizeof why, kind); }
    size_t text(char* out, size_t n) const {
      return (size_t)snprintf(out, n, "installed %d (%s); skipped %d (%s)", installed, what, skipped, why);
    }
};
