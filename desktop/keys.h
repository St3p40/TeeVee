#pragma once
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <string>
#include "engine.h"

// Pieces shared by the terminal mode and the browser build: the key map (plain ASCII keys, as in the window).
// The SDL window has its own copy because it maps SDL key codes.
inline const char* const PERSONALITY_NAMES[5] = {"cheerful", "calm", "grumpy", "shy", "hyper"};

struct KeyState {
    int heldFace = -1, emoteNo = 0;
};

// Applies one key (ASCII; '\t' = look, 127 / 8 = back to the mood). Returns false if the key means nothing; otherwise `msg`
// says what happened ("pet", "look: sweet", ...).
inline bool applyKey(Engine& engine, KeyState& ks, int k, bool shift, std::string& msg) {
  auto fire = [&](uint8_t type, const char* name) {
    engine.event(type);
    msg = name;
    return true;
  };
  switch (k) {
    case ' ':
      engine.toggleSleep();
      msg = "sleep / wake";
      return true;
    case 127:
    case 8:
      engine.autoFace();
      ks.heldFace = -1;
      msg = "following the mood";
      return true;
    case ',':
    case '.':
      ks.heldFace = ((ks.heldFace < 0 ? 0 : ks.heldFace) + (k == '.' ? 1 : A_COUNT - 1)) % A_COUNT;
      engine.showFace(ks.heldFace);
      msg = ANCHOR_NAMES[ks.heldFace];
      return true;
    case 'e':
    case 'E':
      ks.emoteNo =
          ((ks.emoteNo - 1 + (k == 'E' ? -1 : 1)) % (EM_COUNT - 1) + (EM_COUNT - 1)) % (EM_COUNT - 1) + 1;
      engine.playEmote(ks.emoteNo);
      msg = EMOTE_NAMES[ks.emoteNo];
      return true;
    case '\t':
      engine.setStyle(engine.styleIndex() + (shift ? -1 : 1));
      msg = std::string("look: ") + STYLE_PRESETS[engine.styleIndex()].name;
      return true;
    case 'r':
    case 'R':
      engine.randomLook(shift || k == 'R');
      msg = shift || k == 'R' ? "a new look" : "a part of the look changed";
      return true;
    case 'q':
    case 'Q':
      engine.setPersonality(engine.personality() + 1);
      msg = std::string("personality: ") + PERSONALITY_NAMES[engine.personality() % 5];
      return true;
    case 'p':
    case 'P': return fire(EV_PET, "pet");
    case 'o':
    case 'O': return fire(EV_POKE, "poke");
    case 'v':
    case 'V': return fire(EV_PRAISE, "praise");
    case 's':
    case 'S': return fire(EV_SCOLD, "scold");
    case 'd':
    case 'D': return fire(EV_STARTLE, "startle");
    case 'w':
    case 'W': return fire(EV_WIN, "win");
    case 'f':
    case 'F': return fire(EV_FAIL, "fail");
    case 'g':
    case 'G': return fire(EV_GREET, "greet");
    case 'z':
    case 'Z': return fire(EV_TIRED, "tired");
    case 'n':
    case 'N': return fire(EV_ANNOY, "annoy");
    case 'l':
    case 'L': return fire(EV_LOVE, "love");
    case 'c':
    case 'C': return fire(EV_CONFUSE, "confuse");
    case 'b':
    case 'B': return fire(EV_BORE, "bore");
    default:
      if (k >= '1' && k <= '9') {
        ks.heldFace = k - '1';
        engine.showFace(ks.heldFace);
        msg = ANCHOR_NAMES[ks.heldFace];
        return true;
      }
      return false;
  }
}
