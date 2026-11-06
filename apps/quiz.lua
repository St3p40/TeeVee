-- Quiz: true or false. The robot cheers a right answer and frowns at a wrong one, and keeps the best score.
-- Touch: the TRUE and FALSE buttons. Buttons: A = true, B = false.
local Q = {
  {"The Sun is a star", true}, {"Spiders have six legs", false}, {"Sound travels in empty space", false},
  {"Water can boil at room temperature", true}, {"A byte has eight bits", true}, {"Bats are blind", false},
  {"Venus is hotter than Mercury", true}, {"Lightning never hits a place twice", false}, {"Honey can last for years", true},
  {"Humans have four lungs", false}, {"Octopuses have three hearts", true}, {"The Moon has its own light", false},
}
local order, at, score, state, right, wait
local best = store.get("best", 0)

local function start()
  order, at, score, state, wait = {}, 1, 0, "ask", 0
  for i = 1, #Q do order[i] = i end
  for i = #order, 2, -1 do
    local j = math.random(i)
    order[i], order[j] = order[j], order[i]
  end
end

function init() math.randomseed(sys.ms()); start() end

local LEN = 6 -- questions in a round

local function answer(said)
  if state ~= "ask" then return end
  right = Q[order[at]][2] == said
  if right then
    score = score + 1
    face.event("praise", 0.6)
    face.show("happy", 1.2)
  else
    face.event("fail", 0.4)
    face.show("sad", 1.2)
  end
  state, wait = "told", 1.4
  return true
end

function update(dt)
  if state == "told" then
    wait = wait - dt
    if wait <= 0 then
      at = at + 1
      if at > LEN then
        state = "end"
        if score > best then best = score; store.set("best", best) end
        face.emote(score >= LEN - 1 and "star" or score >= LEN // 2 and "music" or "sweat")
      else
        state = "ask"
      end
      return true
    end
  end
end

function touch(kind, x, y)
  if kind ~= "down" then return end
  if state == "end" then start(); return true end
  if y >= 175 then return answer(x < (gfx.X0 + gfx.X1) // 2) end
end
function button(name, down)
  if not down then return end
  if state == "end" then start(); return true end
  if name == "a" then return answer(true) end
  if name == "b" then return answer(false) end
end

function draw()
  gfx.clear(0x100d14)
  local cx = (gfx.X0 + gfx.X1) // 2
  gfx.face(cx, 44, 90)
  if state == "end" then
    gfx.text(string.format("%d of %d", score, LEN), cx, 124, 0xf4ecf1, 2, "c")
    gfx.text(string.format("best %d. Tap to play again", best), cx, 160, 0xa99dab, 1, "c")
    return
  end
  gfx.text(string.format("question %d of %d", at, LEN), cx, 104, 0xa99dab, 1, "c")
  gfx.text(Q[order[at]][1], cx, 132, 0xf4ecf1, 1, "c")
  if state == "told" then
    gfx.text(right and "Right!" or "Not quite", cx, 158, right and 0x5ac88c or 0xf25259, 1, "c")
  end
  gfx.rect(gfx.X0 + 10, 178, cx - gfx.X0 - 14, 50, 0x241c2b, 10)
  gfx.rect(cx + 4, 178, gfx.X1 - cx - 14, 50, 0x241c2b, 10)
  gfx.text("TRUE", (gfx.X0 + 10 + cx) // 2, 203, 0xf4ecf1, 1, "c")
  gfx.text("FALSE", (cx + gfx.X1 - 10) // 2, 203, 0xf4ecf1, 1, "c")
end
