-- Fetch: shows the text of a web page. Tap (or press A) to refresh. Only http:// works (no TLS on the board) and the answer is
-- cut to 2 KB, so point it at something small: a plain-text API, or a page served by your own PC, e.g.
--   python3 -m http.server 8000     then set url below to "http://<your pc>:8000/note.txt"

local url = store.get("url", "http://wttr.in/?format=3")
local status, body, at = nil, "loading...", nil
local busy = false

local function load()
  if busy then return end
  busy = true
  status, body = nil, "loading..."
  local id, why = http.get(url, function(code, text)
    busy = false
    status, body, at = code, text, sys.ms()
  end)
  if not id then busy = false; status, body = 0, why end
end

function init() load() end

function touch(kind)
  if kind == "down" then load(); return true end
end
function button(name, down)
  if down and name == "a" then load(); return true end
end

local function wrap(text, width)
  local lines = {}
  for para in (text .. "\n"):gmatch("(.-)\n") do
    while #para > width do
      local cut = para:sub(1, width):match("^.*()%s") or width
      lines[#lines + 1] = para:sub(1, cut)
      para = para:sub(cut + 1):gsub("^%s+", "")
    end
    lines[#lines + 1] = para
  end
  return lines
end

function draw()
  gfx.clear(0x0d1117)
  local L, R = gfx.X0 + 8, gfx.X1 - 8 -- inside what the panel shows
  gfx.text(url, L, 14, 0x607080, 1, "l")
  local head = status == nil and "..." or status > 0 and ("HTTP " .. status) or "request failed"
  gfx.text(head, R, 14, status and status > 0 and status < 300 and 0x56d364 or 0xf85149, 1, "r")
  gfx.rect(0, 28, 320, 1, 0x222a35)
  for i, line in ipairs(wrap(body or "", (R - L) // 7)) do
    if i > 11 then break end
    gfx.text(line, L, 44 + (i - 1) * 18, 0xe6edf3, 1, "l")
  end
  gfx.text(sys.buttons() and "A: refresh" or "tap to refresh", 160, 228, 0x4a5563, 1, "c")
end
