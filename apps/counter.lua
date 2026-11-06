-- Counter: tap anywhere to add one, tap the minus to subtract, hold the reset to clear. Remembered across restarts.
-- Buttons: A or up = +1, B or down = -1, hold D 1 s = reset.

local n = store.get("n", 0)
local best = store.get("best", 0)
local resetAt = nil
local BTN = sys.buttons()

local function save()
  store.set("n", n)
  if n > best then best = n; store.set("best", best) end
end

function update(dt)
  -- the reset button needs a 1 second hold
  if resetAt and sys.ms() - resetAt > 1000 then
    resetAt = nil
    n = 0
    save()
    return true
  end
end

function touch(kind, x, y)
  if kind == "down" then
    if x < 70 and y > 170 then
      n = n - 1
      save()
    elseif x > 250 and y > 170 then
      resetAt = sys.ms()
    else
      n = n + 1
      save()
    end
    return true
  elseif kind == "up" then
    resetAt = nil
  end
end

function button(name, down)
  if name == "d" then resetAt = down and sys.ms() or nil; return true end
  if not down then return end
  if name == "a" or name == "up" then n = n + 1; save(); return true end
  if name == "b" or name == "down" then n = n - 1; save(); return true end
end

function draw()
  gfx.clear(0x14101c)
  gfx.text(BTN and "A: +1   B: -1" or "tap to count", 160, 22, 0x6a6080, 1, "c")
  gfx.text(tostring(n), 160, 100, 0xffd166, 2, "c")
  gfx.rect(60, 124, 200, 4, 0x2a2338, 2)
  gfx.text("best " .. best, 160, 150, 0x9a90b8, 1, "c")
  if BTN then
    gfx.text("hold D: reset", 160, 205, resetAt and 0xff6a7a or 0x6a6080, 1, "c")
    return
  end
  gfx.rect(10, 180, 60, 50, 0x2a2338, 12)
  gfx.text("-", 40, 205, 0xffffff, 2, "c")
  gfx.rect(250, 180, 60, 50, resetAt and 0x8a2a3a or 0x2a2338, 12)
  gfx.text("reset", 280, 205, 0xffffff, 1, "c")
end
