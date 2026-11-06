-- Dice: tap (or press A) to roll two dice. Remembers nothing, decides everything.

local a, b, rolling, until_ = 3, 5, false, 0
local PIPS = {
  [1] = { { 0, 0 } },
  [2] = { { -1, -1 }, { 1, 1 } },
  [3] = { { -1, -1 }, { 0, 0 }, { 1, 1 } },
  [4] = { { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 } },
  [5] = { { -1, -1 }, { 1, -1 }, { 0, 0 }, { -1, 1 }, { 1, 1 } },
  [6] = { { -1, -1 }, { 1, -1 }, { -1, 0 }, { 1, 0 }, { -1, 1 }, { 1, 1 } },
}

function init() math.randomseed(sys.ms()) end

local function roll()
  if not rolling then rolling, until_ = true, sys.ms() + 900 end
  return true
end

function update(dt)
  if not rolling then return end
  a, b = math.random(1, 6), math.random(1, 6)
  if sys.ms() >= until_ then rolling = false end
  return true
end

function touch(kind) if kind == "down" then return roll() end end
function button(name, down) if down and name == "a" then return roll() end end

local function die(cx, cy, n)
  gfx.rect(cx - 50, cy - 50, 100, 100, rolling and 0xdad4c8 or 0xf5f0e6, 18)
  for _, p in ipairs(PIPS[n]) do gfx.circle(cx + p[1] * 27, cy + p[2] * 27, 9, 0x1b1b22) end
end

function draw()
  gfx.clear(0x142018)
  die(90, 105, a)
  die(230, 105, b)
  gfx.text(rolling and "..." or ("total " .. (a + b)), 160, 195, 0xcfe8d8, 2, "c")
  gfx.text(sys.buttons() and "A: roll" or "tap to roll", 160, 225, 0x6f8f7c, 1, "c")
end
