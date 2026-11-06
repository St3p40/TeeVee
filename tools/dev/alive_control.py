#!/usr/bin/env python3
"""Writes a deliberately scripted copy of src/core/appraisal.h, to check that tools/dev/alive_probe.cpp can still catch a script:
no delay, no change of meaning, one fixed emote. Compile the probe with this folder first on the include path; it must report SCRIPTED rows.

  python3 tools/dev/alive_control.py <empty folder>
  g++ -O2 -std=c++17 -w -DTEEVEE_MIND_N=128 -I<folder> -Isrc/core -Idesktop tools/dev/alive_probe.cpp desktop/engine.cpp -o alive_scripted
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
s = open(os.path.join(ROOT, "src/core/appraisal.h")).read()
CHANGES = [
    ("    EmotionEvent out = meaning(ev, feel, now);\n    const uint32_t due = now + delayFor(out.type, feel, sleepy);",
     "    EmotionEvent out = ev;\n    const uint32_t due = now;"),
    ("    const float r = type < EV_COUNT ? _habit[type] : 0.f;\n    Cand c[6];",
     "    if (type == EV_PET) return feel.v > 0.8f ? EM_LOVE : feel.v > 0.5f ? EM_HEART : EM_NONE;\n"
     "    if (type == EV_WIN) return EM_STAR;\n"
     "    const float r = type < EV_COUNT ? _habit[type] : 0.f;\n    Cand c[6];"),
]
for old, new in CHANGES:
    if s.count(old) != 1:
        sys.exit("appraisal.h changed: update alive_control.py\n" + old)
    s = s.replace(old, new)
os.makedirs(sys.argv[1], exist_ok=True)
open(os.path.join(sys.argv[1], "appraisal.h"), "w").write(s)
print("scripted appraisal.h written to", sys.argv[1])
