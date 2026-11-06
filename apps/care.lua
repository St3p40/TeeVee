-- Care: how the robot is doing, and two ways to look after it. The bars are its slow needs (face.needs): attention, rest, play.
-- Pet it (attention), or play catch: a ball swings across, catch it in the middle (each catch is a little win: play).
-- Rest only comes from sleep: leave it alone and it naps by itself.
-- Touch: the PET and PLAY buttons, tap to catch. Buttons: A = pet, B = play / catch.

local BTN = sys.buttons()
local CX = (gfx.X0 + gfx.X1) // 2
local L, R = gfx.X0 + 14, gfx.X1 - 14
local att, rest, play = 0, 0, 0
local known = false
local petAt, mode, ballX, ballV, throws, caught, flash = -10000, "idle", 0, 0, 0, 0, 0
local total = store.get("caught", 0)

local function readNeeds()
  local a, r, p = face.needs()
  if a then att, rest, play, known = a, r, p, true end
end

function init() math.randomseed(sys.ms()); readNeeds() end

local function pet()
  if sys.ms() - petAt < 1500 then return end -- one stroke at a time
  petAt = sys.ms()
  face.event("pet", 0.8)
  face.emote("heart")
  return true
end

local function throw()
  throws = throws + 1
  ballX, ballV = math.random() < 0.5 and L or R, 0
  ballV = (ballX == L and 1 or -1) * (110 + math.random(0, 90) + throws * 12)
end

local function playOrCatch()
  if mode ~= "play" then
    mode, throws, caught = "play", 0, 0
    throw()
    return true
  end
  if math.abs(ballX - CX) < 22 then -- in the middle: caught
    caught, total, flash = caught + 1, total + 1, 0.4
    store.set("caught", total)
    face.event("win", 0.5)
    if caught % 3 == 0 then face.emote("star") end
  else
    face.event("poke", 0.2) -- a miss: a little poke of disappointment
  end
  if throws >= 6 then mode = "done"; face.emote(caught >= 4 and "laugh" or "sweat") else throw() end
  return true
end

local t = 0
function update(dt)
  t = t + dt
  if flash > 0 then flash = flash - dt end
  if t > 1 then t = 0; readNeeds() end
  if mode == "play" then
    ballX = ballX + ballV * dt
    if ballX < L or ballX > R then -- it flew past: a miss, the next one
      face.event("poke", 0.1)
      if throws >= 6 then mode = "done"; face.emote(caught >= 4 and "laugh" or "sweat") else throw() end
    end
  end
  return true
end

function touch(kind, x, y)
  if kind ~= "down" then return end
  if mode == "play" then return playOrCatch() end
  if y >= 196 then
    if x < CX then return pet() else return playOrCatch() end
  end
end

function button(name, down)
  if not down then return end
  if name == "a" then return pet() end
  if name == "b" then return playOrCatch() end
end

local function bar(label, v, y, color)
  gfx.text(label, L, y, 0x9aa3c0, 1, "l")
  local x0, w = L + 78, R - L - 78
  gfx.rect(x0, y - 5, w, 10, 0x1e2434, 5)
  local f = math.floor(w * v + 0.5)
  if f > 4 then gfx.rect(x0, y - 5, f, 10, v > 0.7 and 0xf25259 or v > 0.4 and 0xf0c43a or color, 5) end
end

function draw()
  gfx.clear(0x0e1119)
  gfx.face(CX, 64, 128)
  if mode == "play" or (mode == "done" and flash > 0) then
    gfx.rect(CX - 22, 118, 44, 70, flash > 0 and 0x2a5a3a or 0x1a2030, 10) -- the catch zone
    if mode == "play" then gfx.circle(math.floor(ballX), 152, 11, 0xffd166) end
    gfx.text("caught " .. caught .. " / " .. throws, CX, 206, 0xc8cbe0, 1, "c")
    gfx.text(BTN and "B: catch" or "tap: catch", CX, 226, 0x6a6f88, 1, "c")
    return
  end
  if known then
    bar("attention", att, 128, 0x5ad6a0)
    bar("rest", rest, 150, 0x4da3ff)
    bar("play", play, 172, 0xb98cff)
  else
    gfx.text("(its needs are not known here)", CX, 150, 0x6a6f88, 1, "c")
  end
  if mode == "done" then gfx.text("caught " .. caught .. " of 6", CX, 189, 0xffd166, 1, "c") end
  if BTN then
    gfx.text("A: pet    B: play catch", CX, 212, 0xc8cbe0, 1, "c")
  else
    gfx.rect(L, 198, CX - L - 6, 34, 0x3a2a40, 12)
    gfx.text("PET", (L + CX) // 2, 215, 0xffffff, 1, "c")
    gfx.rect(CX + 6, 198, R - CX - 6, 34, 0x2a3a5a, 12)
    gfx.text("PLAY", (CX + R) // 2, 215, 0xffffff, 1, "c")
  end
  gfx.text("catches " .. total, R, 110, 0x5a6080, 1, "r")
end
