-- Level: a bubble level from the motion sensor. Stand the device up (or lay it flat) and watch the bubble;
-- the numbers are the lean left/right and forward/back in degrees. Says so if there is no sensor.

local function lean(a)
  if a > 1 then a = 1 elseif a < -1 then a = -1 end
  return math.deg(math.asin(a))
end

function update(dt) return true end

function draw()
  gfx.clear(0x0b1210)
  local ax, ay, az = sys.motion()
  if not ax then
    gfx.text("no motion sensor", 160, 110, 0xa0b0a8, 2, "c")
    gfx.text(sys.buttons() and "this board has none" or "wire one to IO27 / IO22 and turn it on", 160, 145, 0x607068, 1, "c")
    return
  end
  gfx.circle(160, 120, 70, 0x1a2a24)
  gfx.circle(160, 120, 66, 0x0b1210)
  gfx.line(60, 120, 260, 120, 0x1f332b, 1)
  gfx.line(160, 20, 160, 220, 0x1f332b, 1)
  gfx.circle(160, 120, 14, 0x16251f)
  local x = 160 + math.max(-1, math.min(1, ax / 0.5)) * 56
  local y = 120 - math.max(-1, math.min(1, az / 0.5)) * 56
  local level = math.abs(ax) < 0.03 and math.abs(az) < 0.03
  gfx.circle(x, y, 12, level and 0x56d364 or 0xe3b341)
  gfx.text(string.format("left/right %+.1f", lean(ax)), gfx.X0 + 12, 224, 0x8aa098, 1, "l")
  gfx.text(string.format("fwd/back %+.1f", lean(az)), gfx.X1 - 12, 224, 0x8aa098, 1, "r")
end
