-- Clock with the face: a big clock, and the live robot in the corner. Tap to swap between 24 and 12 hours.
local h24 = store.get("h24", true)
local last = -1

function update(dt)
  local h, m, s = sys.time()
  if s ~= last then last = s or -1; return true end
end

function touch(kind)
  if kind == "down" then
    h24 = not h24
    store.set("h24", h24)
    return true
  end
end
function button(name, down)
  if down and name == "a" then return touch("down") end
end

function draw()
  gfx.clear(0x100d14)
  local cx = (gfx.X0 + gfx.X1) // 2
  gfx.face(gfx.X1 - 56, 44, 100)
  local h, m, s, day, month, year = sys.time()
  if not h then
    gfx.text("the clock is not set yet", cx, 130, 0xa99dab, 1, "c")
    return
  end
  local shown = h24 and h or (h % 12 == 0 and 12 or h % 12)
  gfx.text(string.format("%02d:%02d", shown, m), cx, 130, 0xf4ecf1, 2, "c")
  gfx.text(string.format("%02d", s), cx, 164, 0xf25259, 1, "c")
  gfx.text(string.format("%02d.%02d.%d", day, month, year), cx, 200, 0xa99dab, 1, "c")
end
