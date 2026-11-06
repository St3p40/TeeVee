-- hello: the smallest useful Teevee app, and a map of everything an app can do.
-- Upload it from the web page (Apps card) or run `python3 tools/apps.py put apps/hello.lua`.
--
-- Your script defines any of these functions (only draw() is required):
--   init()                 once, when the app starts
--   update(dt)             about 30 times a second; dt = seconds since the last call; return true to redraw
--   draw()                 when a redraw is needed: issue gfx calls (the screen is 320 x 240)
--   touch(kind, x, y)      kind is "down", "move" or "up"; return true to redraw
--   button(name, down)     a board with buttons (Lilka): "up" "down" "left" "right" "a" "b" "c" "d"; down is true / false
--                          (without it, A works as a tap in the middle of the screen)
--   stop()                 when the app is left
--   background(dt)         once a second with the app closed, if you chose it as the background app (one at a time; it pauses while an app is on the screen,
--                          and has no http). It shares store with the app. See alarm.lua.
--
-- Memory: about 22 KB for Lua on the CYD. Every sample fits; a much larger app stops with "not enough memory".
--
-- Drawing (colours are 0xRRGGBB numbers; y of text is its vertical centre):
--   gfx.clear(color)  gfx.rect(x, y, w, h, color [, corner_radius])  gfx.circle(x, y, r, color)
--   gfx.line(x1, y1, x2, y2, color [, thickness])
--   gfx.text(s, x, y, color [, size 1 or 2 [, "l", "c" or "r"]])  gfx.measure(s [, size])  gfx.rgb(r, g, b)
--   gfx.image(name, x, y [, frame]) -> w, h, frames   a Bitmap Array picture: the card's /teevee/images/<name>.ba or `apps.py image`; nothing if there is none
--   gfx.redraw()   gfx.W, gfx.H   gfx.X0, gfx.X1 (the columns the panel shows: 20..300 on the Lilka's narrower one)
-- The face (it is the same robot): face.event(name [, k])  face.emote(name)  face.show(name [, seconds])  face.look(x, y)  face.wake()  face.say(text)  face.urge(name)  face.mood()  face.needs()  gfx.face(x, y, width)
-- Also:
--   sys.ms()  sys.time() -> hour, min, sec, day, month, year, weekday (nil until the clock is set)
--   sys.light() -> 0..1   sys.log(...)   sys.exit()   sys.capture(true) -- you handle horizontal drags
--   sys.buttons() -> true on a board with buttons (show "A: ..." instead of "tap")
--   store.get(key [, default])  store.set(key, value)   -- remembered across restarts (16 keys)
--   http.get("http://...", function(status, body) ... end)   -- status <= 0 means it failed
--   sys.motion() -> ax, ay, az, gx, gy, gz (nil without a motion sensor)
-- The robot itself (the app is part of the face):
--   face.event(name [, strength 0..1])   pet poke praise scold startle win fail greet tired annoy love confuse bore (3 a second)
--   face.emote(name)                     heart star laugh cry think sweat question ... (1 a second)
--   face.mood() -> valence, arousal, dominance    gfx.face(x, y, width)   -- draws the live face in your screen
--   face.needs() -> attention, rest, play         -- 0 content .. 1 urgent (nil if unknown); petting, praise, wins meet them
-- Swipe left/right switches apps; hold a finger still for 2.5 seconds to go back to the face.
-- With buttons: SELECT or START = the next app, hold it = back to the face.

local taps = store.get("taps", 0)
local BTN = sys.buttons()

function draw()
  gfx.clear(0x101820)
  gfx.text("Hello, TeeVee!", 160, 90, 0xffffff, 2, "c")
  gfx.text((BTN and "presses: " or "taps: ") .. taps, 160, 130, 0x7fb0ff, 1, "c")
  gfx.text(BTN and "A: count   hold START: leave" or "swipe or hold to leave", 160, 220, 0x607080, 1, "c")
end

local function count()
  taps = taps + 1
  store.set("taps", taps)
  return true -- redraw
end

function touch(kind, x, y)
  if kind == "down" then return count() end
end

function button(name, down)
  if down and name == "a" then return count() end
end
