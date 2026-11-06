-- Memory pairs: find the matching colours. The robot watches: it looks at the card you turn, is pleased with a pair and sighs at a miss.
-- Touch: tap a card. Buttons: arrows move, A turns.
local COLS, ROWS = 4, 3
local COLORS = {0xf25259, 0x5ac88c, 0x5a8cf2, 0xf2c85a, 0xc85af2, 0x5ad8e6}
local cards, open, found, first, second, wait, tries, cur
local best = store.get("best", 0)

local function deal()
  cards, open, found, first, second, wait, tries, cur = {}, {}, 0, nil, nil, 0, 0, 1
  for i = 1, #COLORS do cards[#cards + 1] = i; cards[#cards + 1] = i end
  for i = #cards, 2, -1 do
    local j = math.random(i)
    cards[i], cards[j] = cards[j], cards[i]
  end
end

function init() math.randomseed(sys.ms()); deal() end

local function box(i)
  local w = (gfx.X1 - gfx.X0 - 10) // COLS
  local cx, cy = (i - 1) % COLS, (i - 1) // COLS
  return gfx.X0 + 5 + cx * w, 30 + cy * 68, w - 6, 62
end

local function turn(i)
  if wait > 0 or open[i] or not cards[i] then return end
  open[i] = true
  local x, y, w, h = box(i)
  face.look((x + w / 2 - 160) / 160, (y + h / 2 - 120) / 120)
  if not first then first = i
  else
    second, tries = i, tries + 1
    if cards[first] == cards[second] then
      found = found + 1
      first, second = nil, nil
      face.event("win", 0.5)
      if found == #COLORS then
        face.emote("star")
        if best == 0 or tries < best then best = tries; store.set("best", best) end
      end
    else
      wait = 0.9
      face.event("fail", 0.25)
    end
  end
  return true
end

function update(dt)
  if wait > 0 then
    wait = wait - dt
    if wait <= 0 then
      open[first], open[second] = nil, nil
      first, second = nil, nil
      return true
    end
  end
end

function touch(kind, x, y)
  if kind ~= "down" then return end
  if found == #COLORS then deal(); return true end
  for i = 1, #cards do
    local bx, by, w, h = box(i)
    if x >= bx and x < bx + w and y >= by and y < by + h then return turn(i) end
  end
end

function button(name, down)
  if not down then return end
  if name == "a" then
    if found == #COLORS then deal(); return true end
    return turn(cur)
  end
  local move = {left = -1, right = 1, up = -COLS, down = COLS}
  if move[name] then cur = (cur - 1 + move[name]) % #cards + 1; return true end
end

function draw()
  gfx.clear(0x100d14)
  local cx = (gfx.X0 + gfx.X1) // 2
  gfx.text(string.format("tries %d", tries), gfx.X0 + 8, 14, 0xa99dab, 1, "l")
  if best > 0 then gfx.text(string.format("best %d", best), gfx.X1 - 8, 14, 0xa99dab, 1, "r") end
  for i = 1, #cards do
    local x, y, w, h = box(i)
    gfx.rect(x, y, w, h, open[i] and COLORS[cards[i]] or 0x241c2b, 8)
    if sys.buttons() and i == cur then
      gfx.rect(x, y, w, 3, 0xf4ecf1)
    end
  end
  if found == #COLORS then
    gfx.rect(gfx.X0 + 20, 90, gfx.X1 - gfx.X0 - 40, 60, 0x1a1520, 12)
    gfx.text("All found! Tap to play again", cx, 120, 0xf4ecf1, 1, "c")
  end
end
