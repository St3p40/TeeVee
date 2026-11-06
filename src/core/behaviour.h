#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "emotes.h"
#include "emotion.h"
#include "mind.h"

// Behaviour as a text file: how the robot behaves, changed without building anything. The same file works on the board (the card's /teevee/behaviour.txt, else
// /behaviour.txt in flash), the desktop app (behaviour.txt beside its saves) and the web page. First line: "teevee-behaviour 1". Then one setting a line, # for
// comments; what a file does not mention keeps its default. A bad line is skipped and reported, numbers are held to their range. `--behaviour-export`, `behaviour`
// on the console or {"get":"behaviour"} write out every setting. Pure logic, no Arduino deps. Host test: test/mind_test.cpp.
struct Behaviour {
    float attentionHours = 1.5f, restHours = 6, playHours = 3, sleepRestoreMinutes = 30;
    float emoteGapSeconds = 45, lookGapMinutes = 20, dayGapSeconds = 60, suggestGapMinutes = 30;
    Personality personality[5];
    EmoteRule rule[EV_COUNT] = {};
    mind::Instinct instinct[mind::MAX_INSTINCTS] = {};
    int instincts = 0;
    Behaviour() { memcpy(personality, PERSONALITIES, sizeof personality); }
};

inline Behaviour BEHAVIOUR;
constexpr int BEHAVIOUR_VERSION = 1;

struct BehaviourKey {
    const char* key;
    float Behaviour::* field;
    float lo, hi;
    const char* help;
};
inline const BehaviourKey BEHAVIOUR_KEYS[] = {
    {"needs.attention_hours", &Behaviour::attentionHours, 0.1f, 48, "hours until it badly wants attention"},
    {"needs.rest_hours", &Behaviour::restHours, 0.5f, 72, "hours awake until it badly wants to sleep"},
    {"needs.play_hours", &Behaviour::playHours, 0.1f, 48, "hours until it badly wants to play"},
    {"needs.sleep_restore_minutes", &Behaviour::sleepRestoreMinutes, 1, 600, "minutes of sleep that restore it fully"},
    {"emote.gap_seconds", &Behaviour::emoteGapSeconds, 5, 3600, "least time between emotes of its own"},
    {"look.gap_minutes", &Behaviour::lookGapMinutes, 1, 1440, "least time between its own look changes"},
    {"day.gap_seconds", &Behaviour::dayGapSeconds, 5, 3600, "about how long between antics that show what its day was like"},
    {"apps.suggest_gap_minutes", &Behaviour::suggestGapMinutes, 1, 1440, "least time between suggesting a game"},
};
struct PersonalityKey {
    const char* key;
    size_t offset;
    float lo, hi;
};
inline const PersonalityKey PERSONALITY_KEYS[] = {
    {"baseline_v", offsetof(Personality, baseline) + offsetof(PAD, v), -1, 1},
    {"baseline_a", offsetof(Personality, baseline) + offsetof(PAD, a), -1, 1},
    {"baseline_d", offsetof(Personality, baseline) + offsetof(PAD, d), -1, 1},
    {"sensitivity", offsetof(Personality, sensitivity), 0.1f, 3},
    {"emotion_half_life", offsetof(Personality, emotionHalfLife), 0.5f, 120},
    {"mood_half_life", offsetof(Personality, moodHalfLife), 10, 7200},
    {"mood_follow", offsetof(Personality, moodFollow), 0, 1},
    {"mood_kick", offsetof(Personality, moodKick), 0, 1},
    {"habituation", offsetof(Personality, habituation), 0, 3},
    {"habit_half_life", offsetof(Personality, habitHalfLife), 1, 600},
};
inline const char* const BEHAVIOUR_PERSONALITIES[5] = {"cheerful", "calm", "grumpy", "shy", "hyper"};

inline char* behaviourWord(char*& p) {
  while (*p == ' ' || *p == '\t') p++;
  if (!*p) return nullptr;
  char* word = p;
  while (*p && *p != ' ' && *p != '\t') p++;
  if (*p) *p++ = 0;
  return word;
}

inline bool behaviourLine(char* line, Behaviour& b) {
  char* save = line;
  char* key = behaviourWord(save);
  if (!key) return true;
  const auto number = [&](float lo, float hi, float& out) {
    char* word = behaviourWord(save);
    if (!word) return false;
    char* end = nullptr;
    const float v = strtof(word, &end);
    if (end == word || *end || v != v) return false;
    out = v < lo ? lo : v > hi ? hi : v;
    return true;
  };
  for (const BehaviourKey& k : BEHAVIOUR_KEYS)
    if (!strcmp(key, k.key)) return number(k.lo, k.hi, b.*(k.field));
  if (!strncmp(key, "personality.", 12)) {
    const char* who = key + 12;
    const char* dot = strchr(who, '.');
    if (!dot) return false;
    for (int p = 0; p < 5; p++)
      if (strlen(BEHAVIOUR_PERSONALITIES[p]) == (size_t)(dot - who) &&
          !strncmp(who, BEHAVIOUR_PERSONALITIES[p], dot - who))
        for (const PersonalityKey& k : PERSONALITY_KEYS)
          if (!strcmp(dot + 1, k.key))
            return number(k.lo, k.hi, *(float*)((char*)&b.personality[p] + k.offset));
    return false;
  }
  if (!strncmp(key, "react.", 6)) {
    int type = -1;
    for (int i = 0; i < EV_COUNT; i++)
      if (!strcmp(key + 6, EVENTS[i].name)) type = i;
    if (type < 0) return false;
    EmoteRule r{};
    for (char* word = behaviourWord(save); word; word = behaviourWord(save)) {
      if (!strcmp(word, "auto")) {
        b.rule[type] = EmoteRule{};
        return true;
      }
      const int e = !strcmp(word, "none") ? (int)EM_NONE : emoteIndex(word);
      float w = 0;
      if (e < 0 || r.n >= 6 || !number(0, 100, w)) return false;
      r.emote[r.n] = (uint8_t)e, r.weight[r.n] = w, r.n++;
    }
    if (!r.n) return false;
    b.rule[type] = r;
    return true;
  }
  if (!strcmp(key, "instinct")) {
    const char* urge = behaviourWord(save);
    const char* sense = behaviourWord(save);
    int u = -1, s = -1;
    for (int i = 0; urge && i < mind::OUT; i++)
      if (!strcmp(urge, mind::URGE_NAMES[i])) u = i;
    for (int i = 0; sense && i < mind::IN; i++)
      if (mind::INPUT_NAMES[i] && !strcmp(sense, mind::INPUT_NAMES[i])) s = i;
    float w = 0;
    if (u < 0 || s < 0 || b.instincts >= mind::MAX_INSTINCTS || !number(-3, 3, w)) return false;
    b.instinct[b.instincts++] = {(uint8_t)u, (uint8_t)s, w};
    return true;
  }
  return false;
}

inline bool behaviourParse(const char* text, size_t len, Behaviour& out, char* err, size_t errN,
                           int* skipped = nullptr) {
  if (errN) err[0] = 0;
  if (skipped) *skipped = 0;
  Behaviour b;
  size_t at = 0;
  bool header = false;
  int lineNo = 0;
  while (at < len) {
    size_t end = at;
    while (end < len && text[end] != '\n') end++;
    char line[128];
    size_t n = end - at;
    lineNo++;
    const bool tooLong = n >= sizeof line;
    n = tooLong ? sizeof line - 1 : n;
    memcpy(line, text + at, n);
    line[n] = 0;
    at = end + 1;
    if (char* hash = strchr(line, '#')) *hash = 0;
    size_t m = strlen(line);
    while (m && (line[m - 1] == '\r' || line[m - 1] == ' ' || line[m - 1] == '\t')) line[--m] = 0;
    const char* first = line;
    while (*first == ' ' || *first == '\t') first++;
    if (!*first) continue;
    if (!header) {
      int v = 0;
      if (sscanf(first, "teevee-behaviour %d", &v) != 1) {
        if (errN) snprintf(err, errN, "the first line must be: teevee-behaviour %d", BEHAVIOUR_VERSION);
        return false;
      }
      if (v != BEHAVIOUR_VERSION) {
        if (errN) snprintf(err, errN, "this file is version %d, this program reads %d", v, BEHAVIOUR_VERSION);
        return false;
      }
      header = true;
      continue;
    }
    if (tooLong || !behaviourLine(line, b)) {
      if (skipped) (*skipped)++;
      if (errN && !err[0]) snprintf(err, errN, "line %d skipped", lineNo);
    }
  }
  if (!header) {
    if (errN) snprintf(err, errN, "empty file");
    return false;
  }
  out = b;
  return true;
}

inline size_t behaviourExport(const Behaviour& b, char* out, size_t n) {
  size_t k = snprintf(out, n, "teevee-behaviour %d\n", BEHAVIOUR_VERSION);
  const auto add = [&](const char* fmt, auto... args) {
    if (k < n) k += snprintf(out + k, n - k, fmt, args...);
  };
  for (const BehaviourKey& e : BEHAVIOUR_KEYS) add("%s %g # %s\n", e.key, b.*(e.field), e.help);
  for (int p = 0; p < 5; p++)
    for (const PersonalityKey& e : PERSONALITY_KEYS)
      add("personality.%s.%s %g\n", BEHAVIOUR_PERSONALITIES[p], e.key,
          *(const float*)((const char*)&b.personality[p] + e.offset));
  add("# react.<event> <emote> <weight> ...: which emotes it picks from (none = no emote; auto = its own judgement)\n");
  for (int t = 0; t < EV_COUNT; t++) {
    if (!b.rule[t].n) {
      add("react.%s auto\n", EVENTS[t].name);
      continue;
    }
    add("react.%s", EVENTS[t].name);
    for (int i = 0; i < b.rule[t].n; i++)
      add(" %s %g", b.rule[t].emote[i] == EM_NONE ? "none" : EMOTE_NAMES[b.rule[t].emote[i]],
          b.rule[t].weight[i]);
    add("\n");
  }
  add("# instinct <urge> <sense> <weight>: the first nature of a new robot\n");
  for (int i = 0; i < b.instincts; i++)
    add("instinct %s %s %g\n", mind::URGE_NAMES[b.instinct[i].urge], mind::INPUT_NAMES[b.instinct[i].sense],
        b.instinct[i].w);
  return k < n ? k : (n ? n - 1 : 0);
}
