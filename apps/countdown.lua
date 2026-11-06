-- Countdown: days until a date you pick. The robot is pleased as it gets near, and celebrates on the day.
-- Touch: the - and + buttons change the day, the bottom bar changes what they change. Buttons: left / right change, A swaps.
local year, month, day = store.get("y", 0), store.get("m", 1), store.get("d", 1)
local field = 1 -- 1 day, 2 month, 3 year
local NAMES = {"day", "month", "year"}
local cheered = false

local function civil(y, m, d) -- days since 1970 (a count that needs no calendar table)
  if m <= 2 then y = y - 1 end
  local era = y // 400
  local yoe = y - era * 400
  local doy = (153 * (m + (m > 2 and -3 or 9)) + 2) // 5 + d - 1
  local doe = yoe * 365 + yoe // 4 - yoe // 100 + doy
  return era * 146097 + doe - 719468
end

local function left()
  local _, _, _, d, m, y = sys.time()
  if not d or year == 0 then return nil end
  return civil(year, month, day) - civil(y, m, d)
end

function init()
  local _, _, _, _, _, y = sys.time()
  if year == 0 and y then year = y + 1 end
end

local function change(by)
  if field == 1 then day = (day - 1 + by) % 31 + 1
  elseif field == 2 then month = (month - 1 + by) % 12 + 1
  else year = year + by end
  store.set("y", year); store.set("m", month); store.set("d", day)
  cheered = false
  return true
end

local t = 0
function update(dt)
  t = t + dt
  if t < 1 then return end
  t = 0
  local n = left()
  if n == 0 and not cheered then
    cheered = true
    face.event("win", 0.9)
    face.emote("star")
  end
  return true
end

function touch(kind, x, y)
  if kind ~= "down" then return end
  if y >= 210 then field = field % 3 + 1; return true end
  if y >= 165 then return change(x < (gfx.X0 + gfx.X1) // 2 and -1 or 1) end
end
function button(name, down)
  if not down then return end
  if name == "a" then field = field % 3 + 1; return true end
  if name == "left" then return change(-1) end
  if name == "right" then return change(1) end
end

function draw()
  gfx.clear(0x100d14)
  local cx = (gfx.X0 + gfx.X1) // 2
  gfx.face(cx, 44, 90)
  local n = left()
  local big = not n and "--" or n == 0 and "Today!" or n < 0 and string.format("%d ago", -n) or tostring(n)
  gfx.text(big, cx, 112, 0xf4ecf1, 2, "c")
  gfx.text(n == 0 and "" or n == 1 and "day" or "days", cx, 136, 0xa99dab, 1, "c")
  gfx.text(string.format("%02d.%02d.%d", day, month, year), cx, 154, 0xa99dab, 1, "c")
  gfx.rect(gfx.X0 + 10, 168, cx - gfx.X0 - 14, 36, 0x241c2b, 8)
  gfx.rect(cx + 4, 168, gfx.X1 - cx - 14, 36, 0x241c2b, 8)
  gfx.text("-", (gfx.X0 + 10 + cx) // 2, 186, 0xf25259, 2, "c")
  gfx.text("+", (cx + gfx.X1 - 10) // 2, 186, 0xf25259, 2, "c")
  gfx.text("changing the " .. NAMES[field] .. " (tap here)", cx, 224, 0xa99dab, 1, "c")
end
