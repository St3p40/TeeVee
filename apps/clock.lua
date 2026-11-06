-- Clock: big seven-segment time, the date, and a ring of dots that fills as the seconds pass.
-- Tap (or press A) to switch between 24-hour and 12-hour (remembered).

local h24 = store.get("h24", true)
local lastSecond = -1
local DAYS = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"}
local MONTHS = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"}

-- which of the seven segments (a top, b top right, c bottom right, d bottom, e bottom left, f top left, g middle) are on
local SEGS = {[0] = "abcdef", [1] = "bc", [2] = "abged", [3] = "abgcd", [4] = "fgbc",
              [5] = "afgcd", [6] = "afgedc", [7] = "abc", [8] = "abcdefg", [9] = "abcdfg"}
local ON, OFF = 0xe8f0ff, 0x151d29

local function digit(x, y, w, h, t, n)
  local lit = n and SEGS[n] or ""
  local vl = (h - 3 * t) // 2 -- length of the vertical segments
  local function seg(name, sx, sy, sw, sh)
    gfx.rect(sx, sy, sw, sh, lit:find(name, 1, true) and ON or OFF, t // 2)
  end
  seg("a", x + t, y, w - 2 * t, t)
  seg("g", x + t, y + t + vl, w - 2 * t, t)
  seg("d", x + t, y + 2 * t + 2 * vl, w - 2 * t, t)
  seg("f", x, y + t, t, vl)
  seg("b", x + w - t, y + t, t, vl)
  seg("e", x, y + 2 * t + vl, t, vl)
  seg("c", x + w - t, y + 2 * t + vl, t, vl)
end

local function ring(s)
  for i = 0, 59 do
    local a = i / 60 * 2 * math.pi - math.pi / 2
    local on = i <= s
    gfx.circle(160 + math.cos(a) * 112, 120 + math.sin(a) * 112, on and 3 or 2, on and 0x4da3ff or 0x24303f)
  end
end

function update(dt)
  local _, _, s = sys.time()
  if s ~= lastSecond then
    lastSecond = s
    return true
  end
end

local function switch()
  h24 = not h24
  store.set("h24", h24)
  return true
end

function touch(kind)
  if kind == "down" then return switch() end
end

function button(name, down)
  if down and name == "a" then return switch() end
end

function draw()
  gfx.clear(0x0a0f18)
  local h, m, s, d, mo, y, wd = sys.time()
  if not h then
    gfx.text("waiting for the time...", 160, 120, 0x8090a0, 1, "c")
    return
  end
  ring(s)
  local pm = h >= 12
  if not h24 then h = h % 12; if h == 0 then h = 12 end end
  local w, ht, t, gap = 40, 76, 9, 6
  local x0 = 160 - (4 * w + 3 * gap + 16) // 2
  local tens = h // 10
  digit(x0, 70, w, ht, t, (h24 or tens > 0) and tens or nil)
  digit(x0 + w + gap, 70, w, ht, t, h % 10)
  local cx = x0 + 2 * w + 2 * gap + 8
  gfx.circle(cx, 96, 4, ON)
  gfx.circle(cx, 120, 4, ON)
  digit(cx + 8 + gap, 70, w, ht, t, m // 10)
  digit(cx + 8 + gap + w + gap, 70, w, ht, t, m % 10)
  gfx.text(DAYS[wd + 1] .. " " .. d .. " " .. MONTHS[mo] .. " " .. y, 160, 172, 0x8aa0b8, 1, "c")
  if not h24 then gfx.text(pm and "PM" or "AM", 160, 195, 0x6a7f95, 1, "c") end
end
