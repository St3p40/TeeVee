-- Bounce: a ball with gravity and a fading trail. Tap to kick it toward the finger; with buttons the D-pad kicks it that way, A throws it up.

local x, y, vx, vy = 160, 60, 90, 0
local R, G = 14, 600
local trail = {}
local hue = 0

local function color(i)
  local r = math.floor(128 + 127 * math.sin(hue + i * 0.3))
  local g = math.floor(128 + 127 * math.sin(hue + 2 + i * 0.3))
  local b = math.floor(128 + 127 * math.sin(hue + 4 + i * 0.3))
  return gfx.rgb(r, g, b)
end

function update(dt)
  vy = vy + G * dt
  x = x + vx * dt
  y = y + vy * dt
  if x < gfx.X0 + R then x = gfx.X0 + R; vx = -vx end -- the walls are the panel's edges
  if x > gfx.X1 - R then x = gfx.X1 - R; vx = -vx end
  if y > gfx.H - R then y = gfx.H - R; vy = -vy * 0.88; if math.abs(vy) < 40 then vy = -420 end end
  if y < R then y = R; vy = -vy end
  hue = hue + dt
  table.insert(trail, 1, {x, y})
  if #trail > 14 then trail[#trail] = nil end
  return true
end

function touch(kind, tx, ty)
  if kind == "down" then
    vx = (tx - x) * 3
    vy = (ty - y) * 3 - 200
  end
end

function button(name, down)
  if not down then return end
  if name == "left" then vx = vx - 250 elseif name == "right" then vx = vx + 250
  elseif name == "up" or name == "a" then vy = -520 elseif name == "down" then vy = vy + 400 end
end

function draw()
  gfx.clear(0x0b0e14)
  for i = #trail, 1, -1 do
    local p = trail[i]
    gfx.circle(p[1], p[2], R * (1 - i / 18), color(i))
  end
  gfx.circle(x, y, R, 0xffffff)
end
