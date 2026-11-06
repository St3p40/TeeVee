import http.server, json, re, sys, os
D = os.path.dirname(os.path.abspath(__file__))  # the JSON files come from mock_dump (see README.md)
SRC = os.path.expanduser('~/teevee-00c/src/portal_page.h')
def page():
    s = open(SRC).read()
    return s[s.index('R"HTML(') + 7 : s.rindex(')HTML"')]
def j(n): return open(os.path.join(D, n)).read()
FACES = ["neutral","happy","sad","angry","surprised","sleepy","excited","content","scared","bored","skeptical","confused","smug"]
CUSTOM = {"builtin_faces": FACES, "faces": [{"name":"grin","mood":True}], "eyes": ["none","heart","star","x","dizzy","coin"], "decos": ["none","tears","sweat","bubble","vein","text"],
  "builtin_emotes": ["heart","star","dizzy","cry","sweat","bubble","angry","question","exclaim","music","laugh","love","think","shy","rage","money","glitch","sparkle","dead"],
  "emotes": [{"name":"wow","face":"surprised","eyes":"star","deco":"none","text":""}],
  "fields": [{"name":n,"min":-1,"max":1,"value":v} for n,v in [("eye_open_l",0.8),("eye_open_r",0.8),("pupil_x",0),("pupil_y",0),("brow_angle",0),("brow_height",0),("smile",0.3),("mouth_open",0)]],
  "map": {"pet":"auto","poke":"auto","praise":"sparkle","scold":"auto","startle":"auto","win":"auto","fail":"auto"}}
class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def send(self, body, ctype='application/json'):
        b = body.encode(); self.send_response(200); self.send_header('Content-Type', ctype + '; charset=utf-8'); self.send_header('Content-Length', str(len(b))); self.end_headers(); self.wfile.write(b)
    def do_GET(self):
        if self.path == '/': return self.send(page(), 'text/html')
        if self.path == '/api/info': return self.send(json.dumps({"build":"a1b2c3d","slot":"app0","ip":"192.168.0.50","ssid":"HomeWiFi"}))
        if self.path == '/api/scan': return self.send(json.dumps({"networks":[{"ssid":"HomeWiFi"},{"ssid":"Neighbour 2.4"},{"ssid":"Cafe"}]}))
        self.send('{}')
    def do_POST(self):
        body = self.rfile.read(int(self.headers.get('Content-Length', 0))).decode()
        try: m = json.loads(body)
        except Exception: m = {}
        g = m.get('get') if isinstance(m, dict) else None
        if g == 'state': return self.send(json.dumps({"state":{"face":"content","idle":"awake"}}))
        if g == 'settings': return self.send(j('settings.json'))
        if g == 'style': return self.send(j('style.json'))
        if g == 'custom': return self.send(json.dumps(CUSTOM))
        if g == 'apps': return self.send(json.dumps({"apps":["clock","snake","dice","timer"],"running":"","free":596000,"total":655360}))
        if g == 'brain': return self.send(j('brain.json'))
        if g == 'mood': return self.send(json.dumps({"mood":{"temper":{"v":0.043,"a":-0.012,"drift":1.0},"needs":{"attention":0.42,"rest":0.18,"play":0.33},"wardrobe":{"on":True,"look":"sweet","mouth":"cat","scores":[["cute",0.31],["sweet",0.22],["monster",-0.18]],"mouths":[["cat",0.4],["zigzag",-0.2]]}}}))
        if g == 'diary': return self.send(j('diary.json'))
        self.send('{"ok":true}')
http.server.ThreadingHTTPServer(('127.0.0.1', int(sys.argv[1])), H).serve_forever()
