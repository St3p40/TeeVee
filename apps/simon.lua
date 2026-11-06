-- Simon: the robot lights the pads around its face in a sequence; repeat it. Every round adds one more.
-- Touch: tap the pads. Buttons: the D-pad (up, right, down, left are the four pads), A starts.
-- The robot cheers every 5 rounds and is sorry when you slip. Best round is remembered.

local BTN = sys.buttons()
local CX = (gfx.X0 + gfx.X1) // 2
local COLORS = { 0x3ccf6e, 0xf25259, 0xf0c43a, 0x4da3ff } -- top green, right red, bottom yellow, left blue
local DIM = { 0x15402a, 0x4a1c20, 0x4a3c14, 0x18324d }
local PADS = { -- x, y, w, h: around the face in the middle
  { CX - 45, 6, 90, 52 },
  { gfx.X1 - 84, 88, 76, 64 },
  { CX - 45, 182, 90, 52 },
  { gfx.X0 + 8, 88, 76, 64 },
}
local KEYS = { up = 1, right = 2, down = 3, left = 4 }

local best = store.get("best", 0)
local seq, pos, state, lit, timer = {}, 0, "idle", 0, 0
local step = 0.45 -- seconds a pad stays lit while it shows the sequence (faster as it grows)

function init() math.randomseed(sys.ms()) end

local function newRound()
  seq[#seq + 1] = math.random(1, 4)
  pos, state, timer, lit = 0, "show", 0.6, 0
  step = math.max(0.2, 0.45 - #seq * 0.015)
end

local function start()
  seq = {}
  newRound()
  face.emote("think")
  return true
end

local function press(i)
  if state == "idle" or state == "over" then return start() end
  if state ~= "input" then return end
  lit, timer = i, 0.18
  pos = pos + 1
  if seq[pos] ~= i then
    state, lit, timer = "over", seq[pos], 1.2 -- show the one it wanted
    local round = #seq - 1
    if round > best then best = round; store.set("best", best) end
    face.event("fail", 0.6)
    face.emote(round >= 5 and "sweat" or "cry")
  elseif pos == #seq then
    state, timer = "wait", 0.7
    if #seq % 5 == 0 then face.event("win", 0.8); face.emote("star") else face.event("praise", 0.3) end
  end
  return true
end

function update(dt)
  if timer <= 0 then return end
  timer = timer - dt
  if timer > 0 then return true end -- redraw while something is lit or waiting
  if state == "show" then
    if lit ~= 0 then lit, timer = 0, 0.12 -- a gap between two pads
    elseif pos < #seq then pos = pos + 1; lit, timer = seq[pos], step
    else state, pos, lit = "input", 0, 0 end
  elseif state == "wait" then newRound()
  else lit = 0 end
  return true
end

function touch(kind, x, y)
  if kind ~= "down" then return end
  for i, p in ipairs(PADS) do
    if x >= p[1] and x < p[1] + p[3] and y >= p[2] and y < p[2] + p[4] then return press(i) end
  end
  if state == "idle" or state == "over" then return start() end
end

function button(name, down)
  if not down then return end
  if KEYS[name] then return press(KEYS[name]) end
  if name == "a" and (state == "idle" or state == "over") then return start() end
end

function draw()
  gfx.clear(0x0c0e16)
  for i, p in ipairs(PADS) do gfx.rect(p[1], p[2], p[3], p[4], lit == i and COLORS[i] or DIM[i], 14) end
  gfx.face(CX, 112, 104)
  local msg
  if state == "idle" then msg = BTN and "A: start" or "tap to start"
  elseif state == "over" then msg = "round " .. (#seq - 1) .. "  -  " .. (BTN and "A: again" or "tap: again")
  elseif state == "input" then msg = "your turn " .. pos .. " / " .. #seq
  else msg = "round " .. #seq end
  gfx.text(msg, CX, 166, 0xc8cbe0, 1, "c")
  gfx.text("best " .. best, gfx.X0 + 8, 228, 0x6a6f88, 1, "l")
end
