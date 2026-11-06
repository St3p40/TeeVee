-- Guess my number: the robot thinks of a number from 1 to 100. Pick yours and guess; it says higher or lower and
-- gets more excited the closer you are. Fewer guesses is better (the best is remembered).
-- Touch: the -10 / -1 / +1 / +10 buttons and GUESS. Buttons: left / right = -1 / +1, down / up = -10 / +10, A = guess.

local BTN = sys.buttons()
local CX = (gfx.X0 + gfx.X1) // 2
local best = store.get("best", 0) -- fewest guesses, 0 = none yet
local secret, pick, tries, hint, lo, hi, done

local W = math.min(60, (gfx.X1 - gfx.X0 - 20) // 4 - 6) -- on-screen buttons: four in a row inside what the panel shows
local PADS = { { "-10", -10 }, { "-1", -1 }, { "+1", 1 }, { "+10", 10 } }
local function padX(i) return CX - 2 * (W + 6) + (i - 1) * (W + 6) + 3 end

local function newGame()
  secret, pick, tries, hint, lo, hi, done = math.random(1, 100), 50, 0, "I'm thinking of a number...", 1, 100, false
end

function init()
  math.randomseed(sys.ms())
  newGame()
end

local function change(d)
  if done then return end
  pick = math.max(1, math.min(100, pick + d))
  return true
end

local function guess()
  if done then newGame(); return true end
  tries = tries + 1
  local off = math.abs(pick - secret)
  if off == 0 then
    done = true
    hint = "Yes! " .. secret .. " in " .. tries .. (tries == 1 and " guess" or " guesses")
    if best == 0 or tries < best then best = tries; store.set("best", best) end
    if tries <= 5 then face.event("love", 0.8); face.emote("heart") else face.event("praise", 0.7); face.emote("star") end
    return true
  end
  if pick < secret then lo = math.max(lo, pick + 1) else hi = math.min(hi, pick - 1) end
  local warmth = off <= 3 and "burning hot!" or off <= 8 and "hot" or off <= 20 and "warm" or off <= 40 and "cold" or "freezing"
  hint = (pick < secret and "higher" or "lower") .. " - " .. warmth
  if off <= 3 then face.event("startle", 0.4); face.emote("exclaim")
  elseif off <= 8 then face.event("greet", 0.3)
  elseif off > 40 then face.event("bore", 0.3) end
  return true
end

function touch(kind, x, y)
  if kind ~= "down" then return end
  if y >= 150 and y < 190 then
    for i, p in ipairs(PADS) do
      if x >= padX(i) and x < padX(i) + W then return change(p[2]) end
    end
  elseif y >= 196 then
    return guess()
  end
end

local STEP = { left = -1, right = 1, down = -10, up = 10 }
function button(name, down)
  if not down then return end
  if STEP[name] then return change(STEP[name]) end
  if name == "a" then return guess() end
end

function draw()
  gfx.clear(0x0f1420)
  gfx.face(CX, 44, 96)
  gfx.text(hint, CX, 98, done and 0x7dffa8 or 0xc8d2e8, 1, "c")
  gfx.text(tostring(pick), CX, 126, 0xffd166, 2, "c")
  if not done and tries > 0 then gfx.text(lo .. " .. " .. hi, CX, 141, 0x5a6480, 1, "c") end
  if BTN then
    gfx.text("left / right: 1   up / down: 10", CX, 168, 0x6a7490, 1, "c")
  else
    for i, p in ipairs(PADS) do
      gfx.rect(padX(i), 152, W, 34, 0x222a3c, 10)
      gfx.text(p[1], padX(i) + W // 2, 169, 0xffffff, 1, "c")
    end
  end
  gfx.rect(CX - 70, 198, 140, 34, done and 0x2a7a4b or 0x3a4a8a, 12)
  gfx.text(done and (BTN and "A: again" or "again") or (BTN and "A: guess" or "GUESS"), CX, 215, 0xffffff, 1, "c")
  gfx.text("tries " .. tries, gfx.X0 + 8, 14, 0x6a7490, 1, "l")
  if best > 0 then gfx.text("best " .. best, gfx.X1 - 8, 14, 0x6a7490, 1, "r") end
end
