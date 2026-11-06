-- Reflex: a reaction duel with the robot. Wait for GO, then tap (or press any button) as fast as you can.
-- The robot is quicker when it is lively and slower when it is sleepy. Too soon and it laughs at you. Your best time is remembered.

local BTN = sys.buttons()
local CX = (gfx.X0 + gfx.X1) // 2
local best = store.get("best", 0) -- ms, 0 = none yet
local wins, losses = store.get("w", 0), store.get("l", 0) -- yours
local state, goAt, yours, mine, result = "idle", 0, 0, 0, ""

function init() math.randomseed(sys.ms()) end

local function robotTime()
  local _, a = face.mood() -- arousal -1..1: a lively robot reacts faster
  return math.floor(260 - 70 * (a or 0) + math.random(-40, 80))
end

local function arm()
  state, result = "wait", ""
  goAt = sys.ms() + math.random(1500, 4000)
  mine = robotTime()
  return true
end

local function hit()
  if state == "idle" or state == "done" then return arm() end
  if state == "wait" then -- too soon
    state, result = "done", "too soon!"
    losses = losses + 1; store.set("l", losses)
    face.event("win", 0.5); face.emote("laugh")
    return true
  end
  if state == "go" then
    yours = sys.ms() - goAt
    if best == 0 or yours < best then best = yours; store.set("best", best) end
    if yours < mine then
      result = "you win!"
      wins = wins + 1; store.set("w", wins)
      face.event("fail", 0.6); face.emote("sweat")
    else
      result = "I win!"
      losses = losses + 1; store.set("l", losses)
      face.event("win", 0.7); face.emote("star")
    end
    state = "done"
    return true
  end
end

function update(dt)
  if state == "wait" and sys.ms() >= goAt then state = "go"; return true end
  if state == "go" and sys.ms() - goAt > mine then -- the robot pressed first (you can still tap to see your time)
    if not result:find("robot") then result = "robot: " .. mine .. " ms" end
    return true
  end
end

function touch(kind) if kind == "down" then return hit() end end
function button(name, down) if down then return hit() end end

function draw()
  local go = state == "go"
  gfx.clear(go and 0x123d22 or state == "wait" and 0x3a1418 or 0x101420)
  gfx.face(CX, 74, 140)
  if state == "idle" then
    gfx.text("Reaction duel", CX, 150, 0xffffff, 2, "c")
    gfx.text(BTN and "press any button to start" or "tap to start", CX, 180, 0x9aa3c0, 1, "c")
  elseif state == "wait" then
    gfx.text("wait for it...", CX, 160, 0xffd0d4, 2, "c")
  elseif go then
    gfx.text("GO!", CX, 160, 0x7dffa8, 2, "c")
    if result ~= "" then gfx.text(result, CX, 190, 0xcfe8d8, 1, "c") end
  else
    gfx.text(result, CX, 150, result == "you win!" and 0x7dffa8 or 0xffb4bb, 2, "c")
    if result ~= "too soon!" then gfx.text("you " .. yours .. " ms   robot " .. mine .. " ms", CX, 180, 0xc8cbe0, 1, "c") end
    gfx.text(BTN and "press again" or "tap again", CX, 202, 0x6a6f88, 1, "c")
  end
  gfx.text(string.format("you %d   robot %d", wins, losses), gfx.X0 + 8, 228, 0x6a6f88, 1, "l")
  if best > 0 then gfx.text("best " .. best .. " ms", gfx.X1 - 8, 228, 0x6a6f88, 1, "r") end
end
