-- Rock paper scissors against the robot, and the robot really feels the result: when it wins it is happy, when it loses it is sad, a draw annoys it (or confuses it).
-- Tap ROCK, PAPER or SCISSORS (buttons: A, B or C). It plays a bit cleverly: now and then it beats what you played last time. Scores are remembered. Hold a finger still 2.5 s to leave.

local NAMES = { "ROCK", "PAPER", "SCISSORS" }
local state, shakeUntil = "pick", 0
local yours, mine, last = 1, 1, nil
local result = ""
local wins, losses, draws = store.get("w", 0), store.get("l", 0), store.get("d", 0) -- from the robot's point of view

function init() math.randomseed(sys.ms()) end

local function beats(a, b) return (a - b) % 3 == 1 end -- paper(2) beats rock(1), scissors(3) beats paper, rock beats scissors

local function choose()
  if last and math.random() < 0.35 then return (last % 3) + 1 end -- the move that beats what you played last time
  return math.random(1, 3)
end

local function judge()
  if mine == yours then
    draws = draws + 1
    result = "Draw!"
    if math.random() < 0.5 then face.event("annoy", 0.6); face.emote("sweat") else face.event("confuse", 0.7); face.emote("question") end
  elseif beats(mine, yours) then
    wins = wins + 1
    result = "I win!"
    face.event("win", 0.9)
    face.emote(math.random() < 0.5 and "laugh" or "star")
  else
    losses = losses + 1
    result = "You win..."
    face.event("fail", 0.8)
    face.emote("cry")
  end
  store.set("w", wins); store.set("l", losses); store.set("d", draws)
  state = "result"
end

function update(dt)
  if state == "shake" then
    if sys.ms() >= shakeUntil then judge() end
    return true
  end
end

local function play(pick)
  yours = pick
  mine = choose()
  last = yours
  state = "shake"
  shakeUntil = sys.ms() + 1400
  face.emote("think")
  return true
end

function touch(kind, x, y)
  if kind ~= "down" then return end
  if state == "pick" and y > 120 then
    local third = (gfx.X1 - gfx.X0) / 3
    return play(x < gfx.X0 + third and 1 or x < gfx.X0 + 2 * third and 2 or 3)
  elseif state == "result" then
    state = "pick"
    return true
  end
end

local KEYS = { a = 1, b = 2, c = 3 }
function button(name, down)
  if not down then return end
  if state == "pick" and KEYS[name] then return play(KEYS[name]) end
  if state == "result" then state = "pick"; return true end
end

local function icon(n, cx, cy, color)
  if n == 1 then gfx.circle(cx, cy, 14, color)
  elseif n == 2 then gfx.rect(cx - 14, cy - 12, 28, 24, color, 4)
  else gfx.line(cx - 12, cy - 12, cx + 12, cy + 12, color, 4); gfx.line(cx + 12, cy - 12, cx - 12, cy + 12, color, 4) end
end

function draw()
  gfx.clear(0x120f18)
  gfx.face(160, 70, 150) -- the live face: it reacts to the result by itself
  if state == "pick" then
    gfx.text("Pick one!", 160, 128, 0xe8dde4, 2, "c")
    for i = 1, 3 do
      local third = (gfx.X1 - gfx.X0) / 3 -- three boxes across what the panel shows
      local cx = math.floor(gfx.X0 + (i - 0.5) * third)
      local half = math.min(48, math.floor(third / 2) - 3)
      gfx.rect(cx - half, 150, 2 * half, 62, 0x241c2b, 12)
      icon(i, cx, 172, 0xf25259)
      gfx.text(sys.buttons() and (("ABC"):sub(i, i) .. ": " .. NAMES[i]) or NAMES[i], cx, 200, 0xcfc2cb, 1, "c")
    end
  elseif state == "shake" then
    local t = (shakeUntil - sys.ms()) / 1400
    gfx.text(t > 0.66 and "rock..." or t > 0.33 and "paper..." or "scissors!", 160, 150, 0xe8dde4, 2, "c")
  else
    gfx.text("me: " .. NAMES[mine] .. "   you: " .. NAMES[yours], 160, 140, 0xcfc2cb, 1, "c")
    gfx.text(result, 160, 172, result == "I win!" and 0x5fd38d or result == "Draw!" and 0xf0b429 or 0xe07a5f, 2, "c")
    gfx.text(sys.buttons() and "any button: again" or "tap to play again", 160, 200, 0x8b7f88, 1, "c")
  end
  gfx.text(string.format("me %d   you %d   draws %d", wins, losses, draws), 160, 226, 0x8b7f88, 1, "c")
end
