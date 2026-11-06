-- Weather: the temperature from wttr.in (plain http; the board has no TLS), and how the robot feels about it. Tap to refresh.
-- Your place: store "place" (empty = wttr.in guesses from your address). It refreshes by itself every 20 minutes.
local place = store.get("place", "")
local temp, note, busy, waited = nil, "loading...", false, 0

local function load()
  if busy then return end
  busy, waited = true, 0
  local id, why = http.get("http://wttr.in/" .. place .. "?format=%t", function(code, body)
    busy = false
    local t = code == 200 and tonumber((body or ""):match("[-+]?%d+"))
    if not t then note = "no answer, tap to try again"; return end
    temp, note = t, t >= 28 and "so hot" or t >= 15 and "nice out" or t >= 3 and "a bit cool" or "brr, cold"
    if t >= 28 then face.emote("sweat") elseif t >= 15 then face.event("greet", 0.3) elseif t < 3 then face.emote("dizzy") end
  end)
  if not id then busy = false; note = why or "no network" end
end

function init() load() end

function update(dt)
  waited = waited + dt
  if waited > 1200 then load(); return true end
  if busy then return true end
end

function touch(kind)
  if kind == "down" then load(); return true end
end
function button(name, down)
  if down and name == "a" then load(); return true end
end

function draw()
  gfx.clear(0x100d14)
  local cx = (gfx.X0 + gfx.X1) // 2
  gfx.face(cx, 70, 130)
  gfx.text(temp and string.format("%+d C", temp) or "--", cx, 160, 0xf4ecf1, 2, "c")
  gfx.text(busy and "loading..." or note, cx, 200, 0xa99dab, 1, "c")
end
