# How the face works, and how to customise it

For the owner and for anyone working on Teevee.

## 1. The pipeline (one frame)

```
events (pet, poke, scold, ...) ──► emotion engine (PAD: valence, arousal, dominance) ──► mapEmotion ──► FaceParams (the base face)
                                        ▲ needs, temperament, mind net urges (idle life) ──────────────┘          │
                                                                                                                  ▼
emotes (heart, bubble, ...) replace or add to the base face ──► idle life (blinks, breathing, look-around) ──► spring (per parameter)
                                                                                                                  │
                                              look (FaceStyle: colours, eye and mouth shapes, effects) ──► renderer ──► screen
                                                                                                                  │
                                         turn (box head) and lens (fisheye) warp the picture on desktop and web only ┘
```

- **FaceParams** (`src/core/face_params.h`) is the expression: eye openness, brows, mouth curve and width, gaze, blush, pupil position. 13 named anchors (`ANCHORS`: neutral, happy, sad, angry, surprised, sleepy, excited, content, scared, bored, skeptical, confused, smug) are fixed starting points.
- **Emotion** (`src/core/emotion.h`) is a PAD model. Events move it: each event has a push in valence, arousal and dominance (`EVENTS`). The mood drifts back to the personality baseline. `mapEmotion` (`src/core/mapper.h`) turns the emotion into FaceParams.
- **Idle life** (`src/core/idle.h`) adds blinks, breathing, squash and stretch, look-around, sleep and wake. `spring.h` moves each parameter smoothly toward its target.
- **Look** (`FaceStyle`, `src/core/style.h`) decides how the face is drawn: colours, eye and pupil shapes, mouth style, glow, pixelate, glitch, day tint, speed and bounce. The look never changes the expression, only the drawing.
- **Renderer** (`src/core/renderer.cpp`, `render_*.cpp`) draws only the changed regions: left eye, right eye and mouth, in 112 x 40 strips. Board, desktop and web all use it.

## 2. Files

| What | Where |
|---|---|
| Expression fields and anchors | `src/core/face_params.h` |
| Look options, presets, shapes, random looks | `src/core/style.h` (one table `STYLE_DEFS` drives everything) |
| Drawing: eyes, pupils, mouth, glow, turn, lens | `src/core/renderer.cpp`, `render_common.cpp/.h`, `render_fx.cpp`, `render_widgets.cpp` |
| Behaviour settings (sensitivity, sleep, wake-up, turn, lens, mind) | `src/core/settings.h` (one table `SETTING_DEFS`) |
| Events, emotion, emotes | `src/core/emotion.h`, `src/core/emotes.h`, `src/core/mapper.h` |
| Idle life, sleep, wake | `src/core/idle.h` |
| Touch zones (board) | `src/core/touchzone.h` |
| Mind net (urges that drive the idle life) | `src/core/mind.h`, `mind_actions.h` |
| Look net (desktop and web: scores looks) | `src/core/look_net.h` |
| Needs, temperament, diary, wardrobe | `needs.h`, `temper.h`, `diary.h`, `wardrobe.h` |
| Desktop and web glue (poke sides, looks, saving) | `desktop/engine.cpp`, `web/wasm_main.cpp` |
| Board glue | `src/brain.cpp`, `src/main.cpp`, `src/protocol.cpp`, `src/portal_page.h` |

## 3. Look: changing how the face is drawn

A look is a `FaceStyle`: 34 options, each a row in `STYLE_DEFS`. The same row drives the console (`style`), the WebSocket, the portal, the web Look tab and the desktop F2 panel. To change a look:

- **Text:** `eye spiral pupil none hue 320 glow 0.8` is a look. Any option not named keeps its value. Presets are the same text (`STYLE_PRESETS`, 40 of them; new ones go at the end, and must not be an existing look in a new colour). The wardrobe holds home + up to 47 (`Wardrobe::MAX`, a compile check says when it is full). New presets go at the end of the list, so older ones keep their numbers. `led` is the owner's own look and what a fresh board starts with.
- **Web:** the Look tab, or a share link `#look=...` (the page applies it on open).
- **Board:** serial `style ...`, the portal Look tab, or `look_save` / `look_load` to the SD card (`/teevee/looks`).
- **Random:** `styleRandomize` draws a random look that always looks like a face. Options the owner set are kept.

### The options

- **Colour:** `hue`, `sat`, `val`, `bg`, `tint` (colour follows mood), `daytint` (drifts through the day: warm at dusk, dim at night).
- **Eyes:** `eye` (the eye body: solid, outline, bars, dots, spiral, sparkle, cross), `eye_w`, `eye_h`, `round`, `eye_gap`, `eye_y`, `lid` (sleepy lids), `lashes`, `brows`, `blush`, `glow`.
- **Per-eye:** `eye_split` on lets the right eye have its own `eye_r`, `pupil_r`, `eye_w_r`, `eye_h_r`, resting `lid_r`, a colour shift `hue_r` (degrees; 0 = the face colour) and its own brow `brows_r`. Off, both eyes match.
- **Pupil colour:** `pupil_ink` (dark = the background, white, or the face colour).
- **Pupils:** `pupil` (none, round, slit, heart, star, square, outline = hollow eye, cross, diamond, moon), `pupil_size`, `glint`.
- **Mouth:** `mouth` (curve, cat, none, wave, zigzag, matrix, bar, dot, fangs, teeth), `mouth_solid`, `mouth_w`, `mouth_y`, `line` (thickness; also scales the matrix dots).
- **Motion:** `speed`, `bounce`.
- **Effects:** `glow` (a soft halo behind the whole face, in the face colour), `pixel` (block size; it removes thin shapes below about 4 px), `glitch` (0 off, 10 constant chaos; the mind net can raise it).

### Adding an option (what to do)

1. Add the field at the **end** of `FaceStyle` (saved looks are prefixes; a field in the middle breaks them). Give it a default in `STYLE_DEFAULT`, appended in the same order.
2. Add an `SDEF` / `SDEFE` row in `STYLE_DEFS` with its range and help text. Enum options also need an `_OPTIONS` string and a `_COUNT`.
3. Use it in the renderer. Keep the default look pixel-identical (the golden check).
4. Add it to the groups in `web/index.html` (`GROUPS`, `LABEL`, `NOTE`) and to `LOOK_GROUPS`, `LOOK_LABEL`, `LOOK_NOTE` in `src/portal_page.h`. Anything not in a group lands under "More".
5. Check the style reply still fits its buffer (`protocol.cpp` `Scratch`, and `style_test`). It was at its limit once.
6. If it's an enum, the look net (`lookFeatures`) reads it; its feature count is fixed, so only change what it reads.

### Adding an eye body or pupil shape

Add the name to `EYE_OPTIONS` / `PUPIL_OPTIONS` and bump the `_COUNT`. Draw it in `drawEyeBody` (eye body) or the pupil `switch` in `drawEye` (pupil). Mind the names: `eye star` is an alias for a solid eye with a star pupil (old text), so a body named "star" would clash. Refresh the golden only for frames that use the new shape, and check that nothing else changed.

## 4. Behaviour settings

`SETTING_DEFS` in `src/core/settings.h` (about 60): personality, sensitivity, recovery, brightness, night dim, sleep after, night sleep, wake-up at power-on, clock 24 h, temperature unit, weather coordinates, CI repo, mind amount, drift, learn rate, explore, the rewards, wardrobe (opt-in), head turn and fisheye (desktop and web). New settings are appended at the end of the struct, like style fields.

## 5. Reactions

- **Events** (`EVENTS`, 13): pet, poke, praise, scold, startle, win, fail, greet, tired, annoy, love, confuse, bore. Each one moves the emotion and feeds the needs, temperament, diary and the mind net's reward.
- **Emotes** (`EMOTE_NAMES`, 19 built in, plus custom ones in Studio): heart, star, dizzy, cry, sweat, bubble (the nose bubble of its sleep), angry, question, exclaim, music, laugh, love, think, shy, rage, money, glitch, sparkle, dead. They play for a few seconds and replace the base face.
- **Poke on one eye** (desktop and web): a poke lands on the eye on the side where the pointer was last used (within 1.5 s); a button poke picks an eye at random. That eye squints shut for 0.6 s and opens again. A touch that starts on an eye (`touchzone.h`) squints that eye the same way.
- **Touch zones** (board): eye = flinch, mouth = ticklish, cheek = shy, forehead stroke = calm. Zones are set in `touchzone.h`.
- **BOOT button** (board): short = next screen; long = back / sleep; on the face, two presses = praise, three = scold.
- **Event map** (portal and console): change which event each action sends.

## 6. Learning

- **Mind net** (`mind.h`): 29 senses into a recurrent state of `TEEVEE_MIND_N` units (32 on the board, 64 on desktop and web), out to 17 urges (gaze, energy, blink, mouth, glitch, yawn, emote, pupils, sleep, wardrobe, lens). It starts from a prior and learns from pets and praise (+) and pokes and scolds (-). Its weights are saved in `/state/mind.bin`.
- **Wardrobe** (opt-in `wardrobe`): changes the look when the mind net's wardrobe urge is high. Its mouth-only changes still use its own scores.
- **Look net** (desktop and web, `look_net.h`): a 2-layer network (64 and 32 units, about 5,600 weights). It scores home and each preset from mood, needs, temperament and the look, and picks the best (with noise). Praise and petting raise the look it's wearing; scolding lowers it. Saved as `lookmind.bin`. The desktop Mind panel and the web Mind tab show its count and the look it wears.

## 7. Turn and lens (desktop and web only)

- **Turn** (`head_turn`, 0 to 35 degrees): the face is the front of a 320 x 240 x 240 box turning about its centre, seen from 520 px. The sides are not drawn. The mind net's gaze drives it, but not directly: the gaze urges are averaged over about 2 s, small ones are ignored (dead band), the turn stops at 60% of the setting, and it drifts there over about a second. Asleep, the head goes back to the middle. (Measured over 10 min with petting: average 13 degrees, about 2 direction changes a minute; before, it sat at its limit and swung about 11 times a minute.)
- **Lens** (`fisheye`, 0 to 1): bends the whole picture. Always 3/4 of the slider; the lens urge adds the rest.
- Both keep the glow and the background. They are compiled out of the board build (`TEEVEE_NO_WARP=1`), because they are too slow on the board.

## 8. Testing a change

- `make -C test`: host tests (style, mind, look net, settings, protocol, portal, apps).
- `make -C test golden-check`: 658 frames. A renderer change must keep them identical, or be deliberate: review the diff, then `golden-save`.
- Preview without a board: `tools/dev/faces.cpp` (static frames) and the `turnshot` / `fishshot` approach (set the turn or lens, write a PPM, convert to PNG).
- Web: `web/build.sh`, served on :8090. Headless Firefox (`tools/dev/marionette.py`) can capture the canvas.
- Board: `pio run`, then `python3 tools/regress.py --host <board-ip>` (27 checks).

## 9. Ideas for more customisation

Roughly in order of value:

1. **Per-eye blush** (lids, colour and brows per eye are done; blush is still shared).
2. **A pupil colour of its own** (`pupil_ink` picks one of three; a free colour is next).
3. **Reaction map per eye:** how much each eye reacts to a poke or a touch (the rule is fixed now).
4. **Expression editor:** change the 13 anchors and save them as custom faces (Studio already has a start).
5. **Look packs:** named sets of looks on the SD card, shared as files or links.
6. **Pin a look from the desktop panel too** (the web Mind tab has 'Keep this look' now; it is not saved between runs).
7. **Mouth and eye animations as options:** speed, rhythm and how often they run, per look.
8. **Board one-eye poke:** the desktop and web have it; the board still flinches both eyes.
9. **Props** (glasses, sweat drop, sleep cap) as drawn items the mind net can show.

## 10. Known gaps

- Per-eye settings cover body, pupil, size, resting lid, colour and brow; blush is shared. The pin is not saved between runs.
- The board has no one-eye poke reaction yet (its eye touch zones flinch both eyes).
- The look net's next-best readout is in the web Mind tab only; it shows "home" until the first look change.
- The turned and lensed picture is slower on the board, so it stays off there.
- Looks are drawn at the panel's 320 x 240 only; no other resolutions.
- The golden record changes whenever a preset or the default look changes; review and re-record deliberately.
