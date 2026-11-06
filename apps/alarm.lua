-- Alarm: the robot wakes up (and wakes the screen) at the time you set, even with this app closed. Set the time here, then press BACKGROUND
-- on the page's Apps tab, or `app bg alarm` on the console. If another app was on the screen at that moment it goes off as soon as that app closes.
-- Touch: the top half changes the hour, the bottom half the minutes, the bar turns it on or off. Buttons: up / down = hour, left / right = minutes, A = on / off.
local hour, min, on = store.get("hour", 7), store.get("min", 30), store.get("on", false)
local fired = store.get("fired", -1)  -- the day it last went off
local CATCH_UP = 30                   -- minutes late it may still go off

local function save()
  store.set("hour", hour); store.set("min", min); store.set("on", on); store.set("fired", fired)
end

local function ring()
  face.wake()
  face.event("greet", 0.9)
  face.emote("music")
end

function background(dt)
  if not on then return end
  local h, m, _, day = sys.time()
  if not h or fired == day then return end
  local late = (h * 60 + m) - (hour * 60 + min)
  if late >= 0 and late <= CATCH_UP then
    fired = day
    store.set("fired", fired)
    ring()
  end
end

local function change(dh, dm)
  hour = (hour + dh) % 24
  min = (min + dm) % 60
  fired = -1
  save()
  return true
end
local function toggle()
  on = not on
  fired = -1
  save()
  return true
end

function touch(kind, x, y)
  if kind ~= "down" then return end
  if y >= 190 then return toggle() end
  local up = x >= (gfx.X0 + gfx.X1) // 2 and 1 or -1
  if y < 95 then return change(up, 0) end
  return change(0, up * 5)
end
function button(name, down)
  if not down then return end
  if name == "a" then return toggle() end
  if name == "up" then return change(1, 0) end
  if name == "down" then return change(-1, 0) end
  if name == "right" then return change(0, 5) end
  if name == "left" then return change(0, -5) end
end

function draw()
  gfx.clear(0x100d14)
  local cx = (gfx.X0 + gfx.X1) // 2
  gfx.text(string.format("%02d:%02d", hour, min), cx, 90, on and 0xf4ecf1 or 0xa99dab, 2, "c")
  gfx.text("left less, right more: top hours, below minutes", cx, 135, 0xa99dab, 1, "c")
  gfx.rect(gfx.X0 + 10, 190, gfx.X1 - gfx.X0 - 20, 40, on and 0xf25259 or 0x241c2b, 10)
  gfx.text(on and "On" or "Off", cx, 210, 0xf4ecf1, 1, "c")
end
