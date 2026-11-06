-- Pet the robot: a soft spot moves around its face. Stroke the spot before it moves on; the more you find in 30 seconds, the happier it is.
-- Touch: tap or stroke the spot. Buttons: arrows move a hand, A pets.
local TIME = 30
local spot, hand, score, left, state, gap
local best = store.get("best", 0)

local function place()
  spot = {x = math.random(gfx.X0 + 40, gfx.X1 - 40), y = math.random(50, 200)}
  gap = 2.2
end

local function start()
  score, left, state, hand = 0, TIME, "play", {x = 160, y = 120}
  place()
end

function init() math.randomseed(sys.ms()); state = "ready"; hand = {x = 160, y = 120}; score, left = 0, TIME end

local function pet(x, y)
  if state ~= "play" then start(); return true end
  face.look((x - 160) / 160, (y - 120) / 120)
  if math.abs(x - spot.x) < 34 and math.abs(y - spot.y) < 34 then
    score = score + 1
    face.event("pet", 0.6)
    if score % 5 == 0 then face.emote("heart") end
    place()
  end
  return true
end

function update(dt)
  if state ~= "play" then return end
  left, gap = left - dt, gap - dt
  if gap <= 0 then place() end
  if left <= 0 then
    state = "end"
    if score > best then best = score; store.set("best", best) end
    face.event(score >= 10 and "love" or "bore", 0.7)
  end
  return true
end

function touch(kind, x, y)
  if kind == "down" then return pet(x, y) end
end
function button(name, down)
  if not down then return end
  if name == "a" then return pet(hand.x, hand.y) end
  local dx = name == "left" and -24 or name == "right" and 24 or 0
  local dy = name == "up" and -24 or name == "down" and 24 or 0
  hand.x, hand.y = math.max(gfx.X0, math.min(gfx.X1, hand.x + dx)), math.max(0, math.min(239, hand.y + dy))
  return dx ~= 0 or dy ~= 0
end

function draw()
  gfx.clear(0x100d14)
  local cx = (gfx.X0 + gfx.X1) // 2
  gfx.face(cx, 120, 200)
  if state == "play" then
    gfx.circle(spot.x, spot.y, 16, 0xf25259)
    if sys.buttons() then gfx.circle(hand.x, hand.y, 6, 0xf4ecf1) end
    gfx.text(string.format("%d", score), gfx.X0 + 10, 14, 0xf4ecf1, 1, "l")
    gfx.text(string.format("%d s", math.ceil(left)), gfx.X1 - 10, 14, 0xa99dab, 1, "r")
  else
    gfx.rect(gfx.X0 + 20, 186, gfx.X1 - gfx.X0 - 40, 44, 0x241c2b, 10)
    local text = state == "end" and string.format("%d pets (best %d). Tap to play again", score, best) or "Pet the red spot! Tap to start"
    gfx.text(text, cx, 208, 0xf4ecf1, 1, "c")
  end
end
