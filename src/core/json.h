#pragma once
#include <initializer_list>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__)
#define JSON_NOINLINE __attribute__((noinline))  // called from many places: one copy saves flash on the board
#else
#define JSON_NOINLINE
#endif

struct JsonView {
    const char* p = nullptr;
    const char* e = nullptr;

    bool ok() const { return p != nullptr; }
    bool isObject() const { return p && *p == '{'; }
    bool isArray() const { return p && *p == '['; }
    bool isString() const { return p && *p == '"'; }
    bool isNumber() const { return p && (*p == '-' || (*p >= '0' && *p <= '9')); }
    bool isBool() const { return p && (*p == 't' || *p == 'f'); }
    bool isTrue() const { return p && *p == 't'; }
    bool isNull() const { return p && *p == 'n'; }

    JSON_NOINLINE float num(float def) const {
      if (isBool()) return isTrue() ? 1.f : 0.f;
      if (!isNumber()) return def;
      const float f = strtof(p, nullptr);
      return isfinite(f) ? f : def;
    }
    JSON_NOINLINE float num(float def, float lo, float hi) const {
      const float f = num(def);
      return f < lo ? lo : (f > hi ? hi : f);
    }

    bool str(char* out, size_t n) const {
      if (!isString() || n == 0) return false;
      size_t k = 0;
      for (const char* s = p + 1; s < e - 1; s++) {
        char c = *s;
        if (c == '\\') {
          c = *++s;
          if (c == 'n')
            c = '\n';
          else if (c == 't')
            c = '\t';
          else if (c == 'r')
            c = '\r';
          else if (c == 'b' || c == 'f')
            c = ' ';
          else if (c == 'u') {
            s += 4;
            c = '?';
          }
        }
        if (k + 1 >= n) return false;
        out[k++] = c;
      }
      out[k] = 0;
      return true;
    }

    bool eq(const char* s) const {
      char buf[64];
      return str(buf, sizeof buf) && !strcmp(buf, s);
    }

    JsonView get(const char* key) const {
      const char* it = nullptr;
      char k[32];
      JsonView v;
      while (next(it, k, sizeof k, v))
        if (!strcmp(k, key)) return v;
      return JsonView();
    }

    JsonView at(int index) const {
      JsonView v;
      if (!isArray()) return v;
      const char* s = ws(p + 1);
      for (int i = 0; s < e && *s != ']'; i++) {
        const char* end = skip(s, e, 0);
        if (!end) break;
        if (i == index) {
          v.p = s;
          v.e = end;
          return v;
        }
        s = ws(end);
        if (*s == ',') s = ws(s + 1);
      }
      return v;
    }

    bool next(const char*& it, char* key, size_t kn, JsonView& val) const {
      if (!isObject()) return false;
      const char* s = ws(it ? it : p + 1);
      if (*s == ',') s = ws(s + 1);
      if (s >= e || *s != '"') return false;
      const char* kend = skip(s, e, 0);
      if (!kend) return false;
      size_t len = (size_t)(kend - s - 2);
      if (len >= kn) len = kn - 1;
      memcpy(key, s + 1, len);
      key[len] = 0;
      s = ws(kend);
      if (*s != ':') return false;
      s = ws(s + 1);
      const char* vend = skip(s, e, 0);
      if (!vend) return false;
      val.p = s;
      val.e = vend;
      it = vend;
      return true;
    }

    // JSON number grammar only (strtod also takes "-nan", "-inf" and hex): -?(0|[1-9]d*)(.d+)?([eE][+-]?d+)?
    static const char* number(const char* s, const char* end) {
      auto digits = [&](const char* q) {
        while (q < end && *q >= '0' && *q <= '9') q++;
        return q;
      };
      if (s < end && *s == '-') s++;
      if (s >= end || *s < '0' || *s > '9') return nullptr;
      s = *s == '0' ? s + 1 : digits(s);
      if (s < end && *s == '.') {
        const char* f = digits(s + 1);
        if (f == s + 1) return nullptr;
        s = f;
      }
      if (s < end && (*s == 'e' || *s == 'E')) {
        s++;
        if (s < end && (*s == '+' || *s == '-')) s++;
        const char* x = digits(s);
        if (x == s) return nullptr;
        s = x;
      }
      return s;
    }

    static const char* ws(const char* s) {
      while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
      return s;
    }

    static const char* skip(const char* s, const char* end, int depth) {
      if (s >= end || depth > 16) return nullptr;
      if (*s == '"') {
        for (s++; s < end; s++) {
          if (*s == '\\')
            s++;
          else if (*s == '"')
            return s + 1;
        }
        return nullptr;
      }
      if (*s == '{' || *s == '[') {
        const bool obj = *s == '{';
        const char close = obj ? '}' : ']';
        s = ws(s + 1);
        if (s < end && *s == close) return s + 1;
        while (s < end) {
          if (obj) {
            if (*s != '"' || !(s = skip(s, end, depth + 1))) return nullptr;
            s = ws(s);
            if (s >= end || *s != ':') return nullptr;
            s = ws(s + 1);
          }
          if (!(s = skip(s, end, depth + 1))) return nullptr;
          s = ws(s);
          if (s < end && *s == ',')
            s = ws(s + 1);
          else if (s < end && *s == close)
            return s + 1;
          else
            return nullptr;
        }
        return nullptr;
      }
      if (*s == '-' || (*s >= '0' && *s <= '9')) return number(s, end);
      for (const char* lit : {"true", "false", "null"}) {
        size_t n = strlen(lit);
        if ((size_t)(end - s) >= n && !strncmp(s, lit, n)) return s + n;
      }
      return nullptr;
    }
};

static inline size_t jsonEscape(const char* s, char* out, size_t n) {
  size_t k = 0;
  for (; *s && k + 7 < n; s++) {
    unsigned char c = (unsigned char)*s;
    if (c == '"' || c == '\\')
      out[k++] = '\\', out[k++] = (char)c;
    else if (c < 0x20)
      k += snprintf(out + k, n - k, "\\u%04x", c);
    else
      out[k++] = (char)c;
  }
  out[k] = 0;
  return k;
}

// Validates a whole message (must be NUL-terminated at s[n]); false on malformed JSON or trailing junk.
static inline bool jsonParse(const char* s, size_t n, JsonView& root) {
  const char* end = s + n;
  const char* b = JsonView::ws(s);
  const char* v = JsonView::skip(b, end, 0);
  if (!v || JsonView::ws(v) != end) return false;
  root.p = b;
  root.e = v;
  return true;
}
