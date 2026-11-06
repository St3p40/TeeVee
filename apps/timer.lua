-- Timer: set the minutes with - and +, tap START. Tap the clock face while running to pause.
-- When it reaches zero the screen flashes; tap to dismiss. Remembers the last length.
-- Buttons: up / down = +5 / -5 minutes (left / right = -1 / +1), A = start / pause, B = stop, any button dismisses.

local minutes = store.get("min", 25)
local running, paused, left, total, finished = false, false, 0, 0, false

local function fmt(sec)
  return string.format("%02d:%02d", sec // 60, sec % 60)
end

local BTN = sys.buttons()

local function pad(x, y, w, h, label, color)
  gfx.rect(x, y, w, h, color, 14)
  gfx.text(label, x + w // 2, y + h // 2, 0xffffff, 2, "c")
end

function update(dt)
  if running and not paused then
    left = left - dt
    if left <= 0 then
      running, finished, left = false, true, 0
      sys.log("timer finished")
    end
    return true
  end
  if finished then return true end -- keep flashing
end

function touch(kind, x, y)
  if kind ~= "down" then return end
  if finished then finished = false; return true end
  if running then
    if y < 160 then paused = not paused end
    if y >= 160 and x > 220 then running, paused = false, false end -- stop
    return true
  end
  if y >= 170 then
    if x < 100 then minutes = math.max(1, minutes - 5)
    elseif x > 220 then minutes = math.min(180, minutes + 5)
    else
      running, paused, total, left = true, false, minutes * 60, minutes * 60
      store.set("min", minutes)
    end
  end
  return true
end

local STEP = { up = 5, down = -5, right = 1, left = -1 }
function button(name, down)
  if not down then return end
  if finished then finished = false; return true end
  if running then
    if name == "a" then paused = not paused end
    if name == "b" then running, paused = false, false end
    return true
  end
  if STEP[name] then minutes = math.max(1, math.min(180, minutes + STEP[name])); return true end
  if name == "a" then
    running, paused, total, left = true, false, minutes * 60, minutes * 60
    store.set("min", minutes)
    return true
  end
end

function draw()
  local flash = finished and (sys.ms() // 400) % 2 == 0
  gfx.clear(flash and 0x7a1f2b or 0x0e141c)
  if finished then
    gfx.text("DONE", 160, 100, 0xffffff, 2, "c")
    gfx.text(BTN and "press any button" or "tap to dismiss", 160, 150, 0xe0c0c8, 1, "c")
    return
  end
  local shown = running and math.ceil(left) or minutes * 60
  gfx.text(fmt(shown), 160, 80, running and (paused and 0xffd166 or 0xffffff) or 0x9fb4c8, 2, "c")
  if running then
    gfx.rect(30, 120, 260, 12, 0x1e2a38, 6)
    local w = math.floor(260 * (1 - left / total))
    if w > 4 then gfx.rect(30, 120, w, 12, 0x4da3ff, 6) end
    local tap = BTN and "A" or "tap"
    gfx.text(paused and ("paused - " .. tap .. " to resume") or (tap .. " to pause"), 160, 150, 0x7f93a8, 1, "c")
    if BTN then gfx.text("B: stop", 160, 200, 0x7f93a8, 1, "c") else pad(220, 175, 90, 50, "stop", 0x8a2a3a) end
  else
    gfx.text("minutes", 160, 120, 0x607488, 1, "c")
    if BTN then
      gfx.text("up / down: 5 min", 160, 172, 0x607488, 1, "c")
      gfx.text("left / right: 1 min", 160, 190, 0x607488, 1, "c")
      gfx.text("A: START", 160, 216, 0x5fd38d, 2, "c")
      return
    end
    pad(10, 175, 90, 50, "-5", 0x24384e)
    pad(110, 175, 100, 50, "START", 0x2a7a4b)
    pad(220, 175, 90, 50, "+5", 0x24384e)
  end
end
