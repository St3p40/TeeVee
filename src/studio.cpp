#include "brain_internal.h"

namespace brain {
void saveCustom() { store.putBytes("custom", &custom, sizeof custom); }

uint8_t emote = EM_NONE;
uint32_t emoteStart = 0, emoteEnd = 0, emoteMs = 1, lastAutoEmote = 0;

bool validEmote(int e) {
  return (e > 0 && e < EM_COUNT) || (e >= EMOTE_CUSTOM && e - EMOTE_CUSTOM < custom.nEmotes);
}

static const char* s_emoteText = nullptr;
void playEmoteText(uint8_t e, const char* text) {
  s_emoteText = text;
  playEmote(e);
  s_emoteText = nullptr;
}

void playEmote(uint8_t e) {
  if (!validEmote(e)) return;
  Overlay ov{};
  if (e < EM_COUNT) {
    ov.eyes = EMOTE_PARTS[e].eyes;
    ov.deco = EMOTE_PARTS[e].deco;
    emoteMs = EMOTE_MS[e];
  } else {
    const CustomEmote& c = custom.emotes[e - EMOTE_CUSTOM];
    ov.eyes = c.eyes;
    ov.deco = c.deco;
    memcpy(ov.text, c.text, sizeof ov.text);
    emoteMs = c.ms ? c.ms : 2500;
  }
  if (s_emoteText) {
    ov.deco = DF_TEXT;
    snprintf(ov.text, sizeof ov.text, "%s", s_emoteText);
  }
  emote = e;
  emoteStart = ov.start = now;
  emoteEnd = now + emoteMs;
  ov.duration = emoteMs;
  setOverlay(ov);
  if (e != EM_BUBBLE) Serial.printf("emote: %s\n", emoteName(custom, e));
}

FaceParams emoteFaceAt(float t) {
  if (emote < EM_COUNT) return emoteFace(emote, t);
  const CustomEmote& c = custom.emotes[emote - EMOTE_CUSTOM];
  FaceParams p;
  if (!faceByName(custom, c.face, p)) p = ANCHORS[A_NEUTRAL];
  p.squash += c.wobble * 0.25f * sinf(t * 6.2832f * 3);
  return p;
}
bool emoteActive() { return emote != EM_NONE && (int32_t)(now - emoteEnd) < 0; }

bool showFace(const char* name, uint32_t ttlMs) {
  FaceParams p;
  if (!faceByName(custom, name, p)) return false;
  manual = true;
  manualUntil = ttlMs ? now + ttlMs : 0;
  setBase(p);
  return true;
}

size_t customJson(char* out, size_t n) {
  size_t k = snprintf(out, n, "{\"builtin_faces\":[");
  for (int i = 0; i < A_COUNT && k < n; i++)
    k += snprintf(out + k, n - k, "%s\"%s\"", i ? "," : "", ANCHOR_NAMES[i]);
  k += snprintf(out + k, n - k, "],\"faces\":[");
  for (int i = 0; i < custom.nFaces && k < n; i++) {
    const CustomFace& f = custom.faces[i];
    k += snprintf(out + k, n - k, "%s{\"name\":\"%s\",\"mood\":%d,\"pad\":[%.2f,%.2f,%.2f]}", i ? "," : "",
                  f.name, f.mood, f.pad.v, f.pad.a, f.pad.d);
  }
  k += snprintf(out + k, n - k, "],\"builtin_emotes\":[");
  for (int i = 1; i < EM_COUNT && k < n; i++)
    k += snprintf(out + k, n - k, "%s\"%s\"", i > 1 ? "," : "", EMOTE_NAMES[i]);
  k += snprintf(out + k, n - k, "],\"emotes\":[");
  for (int i = 0; i < custom.nEmotes && k < n; i++) {
    const CustomEmote& e = custom.emotes[i];
    char text[24];
    jsonEscape(e.text, text, sizeof text);
    k += snprintf(
        out + k, n - k,
        "%s{\"name\":\"%s\",\"face\":\"%s\",\"eyes\":\"%s\",\"deco\":\"%s\",\"text\":\"%s\",\"ms\":%u,\"wobble\":%.2f}",
        i ? "," : "", e.name, e.face, EYEFX_NAMES[e.eyes % EF_COUNT], DECO_NAMES[e.deco % DF_COUNT], text,
        (unsigned)e.ms, e.wobble);
  }
  k += snprintf(out + k, n - k, "],\"eyes\":[");
  for (int i = 0; i < EF_COUNT && k < n; i++)
    k += snprintf(out + k, n - k, "%s\"%s\"", i ? "," : "", EYEFX_NAMES[i]);
  k += snprintf(out + k, n - k, "],\"decos\":[");
  for (int i = 0; i < DF_COUNT && k < n; i++)
    k += snprintf(out + k, n - k, "%s\"%s\"", i ? "," : "", DECO_NAMES[i]);
  k += snprintf(out + k, n - k, "],\"map\":{");
  for (int i = 0; i < EV_COUNT && k < n; i++)
    k += snprintf(out + k, n - k, "%s\"%s\":\"%s\"", i ? "," : "", EVENTS[i].name,
                  emoteName(custom, custom.evMap[i]));
  k += snprintf(out + k, n - k, "},\"fields\":[");
  for (int i = 0; i < FACE_FIELD_COUNT && k < n; i++) {
    const FaceField& f = FACE_FIELDS[i];
    k += snprintf(out + k, n - k, "%s{\"name\":\"%s\",\"min\":%g,\"max\":%g,\"value\":%.2f}", i ? "," : "",
                  f.name, f.min, f.max, faceField(base, f));
  }
  if (k < n) k += snprintf(out + k, n - k, "]}");
  return k < n ? k : n - 1;
}

bool setFaceField(const char* name, float v) {
  for (const FaceField& f : FACE_FIELDS)
    if (!strcmp(f.name, name)) {
      FaceParams b = base;
      faceField(b, f) = v < f.min ? f.min : (v > f.max ? f.max : v);
      manual = true;
      manualUntil = 0;
      base = b;
      return true;
    }
  return false;
}

bool parseEmote(const char* spec, CustomEmote& e) {
  memset(&e, 0, sizeof e);
  strcpy(e.face, "neutral");
  e.ms = 2500;
  e.wobble = 0.3f;
  char tok[32];
  int used = 0;
  if (sscanf(spec, " %11s%n", e.name, &used) != 1) return false;
  spec += used;
  while (sscanf(spec, " %31s%n", tok, &used) == 1) {
    spec += used;
    char* eq = strchr(tok, '=');
    if (!eq) return false;
    *eq = 0;
    const char* v = eq + 1;
    if (!strcmp(tok, "face"))
      snprintf(e.face, sizeof e.face, "%s", v);
    else if (!strcmp(tok, "eyes"))
      e.eyes = (uint8_t)nameIndex(EYEFX_NAMES, EF_COUNT, v);
    else if (!strcmp(tok, "deco") || !strcmp(tok, "fx"))
      e.deco = (uint8_t)nameIndex(DECO_NAMES, DF_COUNT, v);
    else if (!strcmp(tok, "text"))
      snprintf(e.text, sizeof e.text, "%s", v), e.deco = DF_TEXT;
    else if (!strcmp(tok, "ms")) {
      const long ms = strtol(v, nullptr, 10);
      e.ms = (uint16_t)(ms < 0 ? 0 : ms > 65535 ? 65535 : ms);
    } else if (!strcmp(tok, "wobble")) {
      const float w = strtof(v, nullptr);
      e.wobble = w >= 0 && w <= 1 ? w : (w > 1 ? 1.f : 0.f);
    }  // NaN -> 0
    else
      return false;
  }
  FaceParams unused;
  return faceByName(custom, e.face, unused) && e.eyes < EF_COUNT && e.deco < DF_COUNT && e.ms >= 300;
}
}  // namespace brain
