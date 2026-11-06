-- Snake: tap the left half of the screen to turn left, the right half to turn right (turns are relative to where it is heading).
-- Eat the red dots; walls and your own tail end the game. Best score is remembered. Buttons: the D-pad steers (it cannot turn back on itself).

local ROWS, CELL, TOP = 11, 20, 20
local COLS = (gfx.X1 - gfx.X0) // CELL -- as many as the panel shows: 16, or 14 on the Lilka
local LEFT = gfx.X0 + (gfx.X1 - gfx.X0 - COLS * CELL) // 2
local MAXLEN = 150 -- stay under the draw-command limit; filling this much counts as a win

local best = store.get("best", 0)
local snake, dir, food, acc, dead, won, paused, pending

local function put_food()
  for _ = 1, 200 do
    local fx, fy = math.random(0, COLS - 1), math.random(0, ROWS - 1)
    local free = true
    for _, s in ipairs(snake) do if s[1] == fx and s[2] == fy then free = false; break end end
    if free then food = { fx, fy }; return end
  end
end

local function reset()
  local m = COLS // 2
  snake = { { m, 5 }, { m - 1, 5 }, { m - 2, 5 } } -- head first
  dir, pending, acc, dead, won, paused = 0, 0, 0, false, false, true
  put_food()
end

function init()
  math.randomseed(sys.ms())
  reset()
end

local function step()
  if pending ~= 0 then dir = (dir + pending) % 4; pending = 0 end -- 0 right, 1 down, 2 left, 3 up
  local dx = ({ 1, 0, -1, 0 })[dir + 1]
  local dy = ({ 0, 1, 0, -1 })[dir + 1]
  local nx, ny = snake[1][1] + dx, snake[1][2] + dy
  if nx < 0 or ny < 0 or nx >= COLS or ny >= ROWS then dead = true; return end
  for i = 1, #snake - 1 do -- the tail end moves away this step, so it is not a hit
    if snake[i][1] == nx and snake[i][2] == ny then dead = true; return end
  end
  table.insert(snake, 1, { nx, ny })
  if nx == food[1] and ny == food[2] then
    if #snake >= MAXLEN then won = true; return end
    put_food()
  else
    table.remove(snake)
  end
end

local function finish()
  local score = #snake - 3
  if score > best then best = score; store.set("best", best) end
end

function update(dt)
  if dead or won or paused then return end
  acc = acc + dt
  local every = math.max(0.09, 0.2 - (#snake - 3) * 0.003)
  if acc >= every then
    acc = acc - every
    step()
    if dead or won then finish() end
    return true
  end
end

function touch(kind, x)
  if kind ~= "down" then return end
  if dead or won then reset(); return true end
  if paused then paused = false; return true end
  pending = x < 160 and 3 or 1 -- left = counter-clockwise (+3), right = clockwise (+1)
  return true
end

local DIRS = { right = 0, down = 1, left = 2, up = 3 }
function button(name, down)
  if not down then return end
  if dead or won then reset(); return true end
  if paused then paused = false; return true end
  local d = DIRS[name]
  if d and d ~= dir and (d - dir) % 4 ~= 2 then pending = (d - dir) % 4 end -- as a turn from where it heads; never straight back
  return true
end

function draw()
  gfx.clear(0x0b1210)
  gfx.text("score " .. (#snake - 3), gfx.X0 + 8, 10, 0xcfe8d8, 1, "l")
  gfx.text("best " .. best, gfx.X1 - 8, 10, 0x6f8f7c, 1, "r")
  gfx.rect(0, TOP - 1, 320, 1, 0x24352c)
  for i = #snake, 1, -1 do
    local s = snake[i]
    gfx.rect(LEFT + s[1] * CELL + 1, TOP + s[2] * CELL + 1, CELL - 2, CELL - 2, i == 1 and 0x9fffb8 or 0x3fbf6a, 4)
  end
  gfx.circle(LEFT + food[1] * CELL + CELL // 2, TOP + food[2] * CELL + CELL // 2, 7, 0xff5a6a)
  if dead or won or paused then
    gfx.rect(40, 80, 240, 70, 0x0b1210, 12)
    local B = sys.buttons()
    local msg = won and "you win!" or dead and "game over" or (B and "press to start" or "tap to start")
    gfx.text(msg, 160, 105, 0xffffff, 2, "c")
    gfx.text(dead and (B and "a button: again" or "tap to play again") or (B and "the D-pad steers" or "tap left / right to steer"), 160, 135, 0x8fb09c, 1, "c")
  end
end
