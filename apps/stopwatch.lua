-- Stopwatch: Tap the top half = start / stop. Tap the bottom half to take a lap (while running) or to clear (when stopped).
-- Buttons: A = start / stop, B = lap / clear.

local running, elapsed, since, laps = false, 0, 0, {}

local function now_s() return elapsed + (running and (sys.ms() - since) / 1000 or 0) end
local function fmt(t)
  local m, s = math.floor(t / 60), t % 60
  return string.format("%02d:%05.2f", m, s)
end

local function toggle()
  if running then elapsed = now_s(); running = false else since = sys.ms(); running = true end
  return true
end

function update() if running then return true end end

local function lapOrClear()
  if running then
    table.insert(laps, 1, now_s())
    if #laps > 5 then table.remove(laps) end
  else
    elapsed, laps = 0, {}
  end
  return true
end

function touch(kind, x, y)
  if kind ~= "down" then return end
  if y < 120 then return toggle() end
  return lapOrClear()
end

function button(name, down)
  if not down then return end
  if name == "a" then return toggle() end
  if name == "b" then return lapOrClear() end
end

function draw()
  gfx.clear(0x0e141c)
  gfx.text(fmt(now_s()), 160, 50, running and 0xffffff or 0x9fb4c8, 2, "c")
  local tap = sys.buttons() and "A" or "tap"
  gfx.text(running and ("running - " .. tap .. " to stop") or (elapsed > 0 and ("stopped - " .. tap .. " to resume") or (tap .. " to start")), 160, 90, 0x607488, 1, "c")
  for i, t in ipairs(laps) do
    gfx.text(string.format("lap %d   %s", #laps - i + 1, fmt(t)), 160, 110 + i * 20, 0xc8d6e4, 1, "c")
  end
  if sys.buttons() then gfx.text(running and "B: lap" or "B: clear", 160, 228, 0x4a5d70, 1, "c")
  else gfx.text(running and "tap here for a lap" or "tap here to clear", 160, 228, 0x4a5d70, 1, "c") end
end
