-- Tic-tac-toe against the robot: you are X and go first, it is O. It wins when it can, blocks when it must,
-- and otherwise plays well, but a happy robot gets careless now and then. Its face shows how the game goes.
-- Touch: tap a square. Buttons: the D-pad moves the cursor, A places. Scores are remembered.

local BTN = sys.buttons()
local CELL = 58
local GX = (gfx.X0 + gfx.X1) // 2 - CELL * 3 // 2 + 30 -- the grid sits right of the little face
local GY = 52
local LINES = { { 1, 2, 3 }, { 4, 5, 6 }, { 7, 8, 9 }, { 1, 4, 7 }, { 2, 5, 8 }, { 3, 6, 9 }, { 1, 5, 9 }, { 3, 5, 7 } }

local board, cursor, state, winLine, thinkUntil
local inLine = {} -- squares of the winning line (kept, not rebuilt every frame: apps have 40 KB)
local CORNERS_FIRST = { 1, 3, 7, 9, 2, 4, 6, 8 }
local you, robot, draws = store.get("you", 0), store.get("robot", 0), store.get("draws", 0)

local function reset()
  board = { 0, 0, 0, 0, 0, 0, 0, 0, 0 } -- 0 empty, 1 you (X), 2 robot (O)
  cursor, state, winLine = 5, "you", nil
  for i = 1, 9 do inLine[i] = false end
end

function init()
  math.randomseed(sys.ms())
  reset()
end

local function winner()
  for _, l in ipairs(LINES) do
    local a = board[l[1]]
    if a ~= 0 and a == board[l[2]] and a == board[l[3]] then return a, l end
  end
  for i = 1, 9 do if board[i] == 0 then return nil end end
  return 0 -- full: a draw
end

local function finishing(who) -- a square that completes a line for `who`, or nil
  for _, l in ipairs(LINES) do
    local n, free = 0, nil
    for _, i in ipairs(l) do
      if board[i] == who then n = n + 1 elseif board[i] == 0 then free = i end
    end
    if n == 2 and free then return free end
  end
end

local function robotMove()
  local v = face.mood() -- a cheerful robot sometimes skips the block
  local careless = math.random() < 0.12 + 0.2 * math.max(0, v or 0)
  local m = finishing(2) or (not careless and finishing(1))
  if not m and board[5] == 0 then m = 5 end
  if not m then -- a random free corner, else a random free edge
    local nc, ne = 0, 0
    for _, i in ipairs(CORNERS_FIRST) do
      if board[i] == 0 then if i % 2 == 1 then nc = nc + 1 else ne = ne + 1 end end
    end
    local want, n = math.random(1, nc > 0 and nc or ne), 0
    for _, i in ipairs(CORNERS_FIRST) do
      if board[i] == 0 and ((nc > 0) == (i % 2 == 1)) then
        n = n + 1
        if n == want then m = i; break end
      end
    end
  end
  board[m] = 2
end

local function settle()
  local w, line = winner()
  if w == nil then return false end
  state, winLine = "over", line
  if line then for _, i in ipairs(line) do inLine[i] = true end end
  if w == 1 then
    you = you + 1; store.set("you", you)
    face.event("fail", 0.7); face.emote("cry")
  elseif w == 2 then
    robot = robot + 1; store.set("robot", robot)
    face.event("win", 0.9); face.emote("laugh")
  else
    draws = draws + 1; store.set("draws", draws)
    face.event("confuse", 0.5); face.emote("question")
  end
  return true
end

local function place(i)
  if state == "over" then reset(); return true end
  if state ~= "you" or board[i] ~= 0 then return end
  board[i] = 1
  if not settle() then
    state, thinkUntil = "robot", sys.ms() + 500 + math.random(0, 400)
    face.emote("think")
  end
  return true
end

function update(dt)
  if state == "robot" and sys.ms() >= thinkUntil then
    robotMove()
    if not settle() then state = "you" end
    return true
  end
end

function touch(kind, x, y)
  if kind ~= "down" then return end
  if state == "over" then reset(); return true end
  local c, r = (x - GX) // CELL, (y - GY) // CELL
  if c >= 0 and c < 3 and r >= 0 and r < 3 then return place(r * 3 + c + 1) end
end

local MOVE = { left = -1, right = 1, up = -3, down = 3 }
function button(name, down)
  if not down then return end
  if state == "over" then reset(); return true end
  local d = MOVE[name]
  if d then
    local c = cursor + d
    if (name == "left" or name == "right") and (c - 1) // 3 ~= (cursor - 1) // 3 then return end -- not round the row's end
    if c >= 1 and c <= 9 then cursor = c end
    return true
  end
  if name == "a" then return place(cursor) end
end

local function mark(i, color)
  local c, r = (i - 1) % 3, (i - 1) // 3
  local x, y = GX + c * CELL + CELL // 2, GY + r * CELL + CELL // 2
  if board[i] == 1 then
    gfx.line(x - 16, y - 16, x + 16, y + 16, color, 6)
    gfx.line(x + 16, y - 16, x - 16, y + 16, color, 6)
  elseif board[i] == 2 then
    gfx.circle(x, y, 19, color)
    gfx.circle(x, y, 13, 0x141019)
  end
end

function draw()
  gfx.clear(0x141019)
  gfx.face(gfx.X0 + 44, 70, 70)
  local head = state == "you" and "your move" or state == "robot" and "thinking..." or ""
  if state == "over" then
    local w = winner()
    head = w == 1 and "you win!" or w == 2 and "I win!" or "a draw"
  end
  gfx.text(head, GX + CELL * 3 // 2, 26, 0xe8dde4, 2, "c")
  for i = 1, 2 do
    gfx.rect(GX + i * CELL - 2, GY + 4, 4, CELL * 3 - 8, 0x3a3046, 2)
    gfx.rect(GX + 4, GY + i * CELL - 2, CELL * 3 - 8, 4, 0x3a3046, 2)
  end
  if BTN and state == "you" then
    local c, r = (cursor - 1) % 3, (cursor - 1) // 3
    gfx.rect(GX + c * CELL + 5, GY + r * CELL + 5, CELL - 10, CELL - 10, 0x2a2236, 8)
  end
  for i = 1, 9 do mark(i, inLine[i] and 0xffd166 or (board[i] == 1 and 0x4da3ff or 0xf25259)) end
  gfx.text("you " .. you, gfx.X0 + 44, 130, 0x4da3ff, 1, "c")
  gfx.text("me " .. robot, gfx.X0 + 44, 150, 0xf25259, 1, "c")
  gfx.text("draws " .. draws, gfx.X0 + 44, 170, 0x8b7f88, 1, "c")
  if state == "over" then gfx.text(BTN and "press: again" or "tap: again", gfx.X0 + 44, 210, 0x8b7f88, 1, "c") end
end
