-- Pomodoro: 25 minutes of work, 5 of rest, a long rest every 4th round. Tap: start / pause.
-- Hold a finger on the screen for 1 second to skip to the next phase. Rounds done today are counted.
-- Buttons: A = start / pause, hold B 1 s = skip.

local WORK, REST, LONG = 25 * 60, 5 * 60, 15 * 60
local phase, left, running, held = "work", WORK, false, nil
local rounds, day = store.get("rounds", 0), store.get("day", 0)

local function today()
  local _, _, _, d, m, y = sys.time()
  return d and (y * 400 + m * 32 + d) or 0
end

function init()
  local t = today()
  if t ~= day then rounds, day = 0, t; store.set("rounds", 0); store.set("day", t) end
end

local function next_phase()
  if phase == "work" then
    rounds = rounds + 1
    store.set("rounds", rounds)
    phase = rounds % 4 == 0 and "long" or "rest"
  else
    phase = "work"
  end
  left = phase == "work" and WORK or phase == "rest" and REST or LONG
  running = false -- wait for you
  sys.log("pomodoro: " .. phase)
end

function update(dt)
  if held and sys.ms() - held > 1000 then held = nil; next_phase(); return true end
  if running then
    left = left - dt
    if left <= 0 then next_phase() end
    return true
  end
end

function touch(kind)
  if kind == "down" then held = sys.ms(); return end
  if kind == "up" and held then
    held = nil
    running = not running
    return true
  end
end

local BTN = sys.buttons()

function button(name, down)
  if name == "a" and down then running = not running; return true end
  if name == "b" then held = down and sys.ms() or nil end
end

function draw()
  local color = phase == "work" and 0xff6b5a or 0x5ad6a0
  gfx.clear(0x10131a)
  gfx.text(phase == "work" and "WORK" or phase == "rest" and "REST" or "LONG REST", 160, 28, color, 2, "c")
  local s = math.ceil(left)
  gfx.text(string.format("%02d:%02d", s // 60, s % 60), 160, 100, running and 0xffffff or 0x8a96a8, 2, "c")
  local total = phase == "work" and WORK or phase == "rest" and REST or LONG
  gfx.rect(30, 140, 260, 12, 0x1e2634, 6)
  local w = math.floor(260 * (1 - left / total))
  if w > 4 then gfx.rect(30, 140, w, 12, color, 6) end
  gfx.text(BTN and (running and "A: pause" or "A: start") or (running and "tap: pause" or "tap: start"), 160, 175, 0x6b7a90, 1, "c")
  gfx.text(BTN and "hold B 1 s: skip" or "hold 1 s: skip", 160, 195, 0x4a5568, 1, "c")
  gfx.text(rounds .. " done today", 160, 222, 0x8a96a8, 1, "c")
end
