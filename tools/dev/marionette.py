import socket, json, subprocess, time, base64, sys, os, tempfile
class M:
    def __init__(self, port=2828):
        self.s = socket.create_connection(('127.0.0.1', port), timeout=60); self.id = 0; self._read()
    def _read(self):
        buf = b''
        while b':' not in buf: buf += self.s.recv(1)
        n = int(buf.split(b':')[0]); data = b''
        while len(data) < n: data += self.s.recv(n - len(data))
        return json.loads(data)
    def call(self, name, params=None):
        self.id += 1; msg = json.dumps([0, self.id, name, params or {}]).encode()
        self.s.sendall(str(len(msg)).encode() + b':' + msg)
        while True:
            r = self._read()
            if r[0] == 1 and r[1] == self.id:
                if r[2]: raise RuntimeError(r[2])
                return r[3]
def run(url, steps, out):
    prof = tempfile.mkdtemp()
    p = subprocess.Popen(['firefox', '--headless', '--marionette', '--profile', prof, '--no-remote'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        for _ in range(60):
            try: m = M(); break
            except OSError: time.sleep(0.5)
        m.call('WebDriver:NewSession', {'capabilities': {}})
        m.call('WebDriver:SetWindowRect', {'width': 900, 'height': 1300})
        m.call('WebDriver:Navigate', {'url': url})
        for kind, arg in steps:
            if kind == 'wait': time.sleep(arg)
            elif kind == 'js':
                r = m.call('WebDriver:ExecuteScript', {'script': arg, 'args': []}); print('js ->', r.get('value'))
            elif kind == 'shot':
                r = m.call('WebDriver:TakeScreenshot', {'full': True}); open(arg, 'wb').write(base64.b64decode(r['value'])); print('shot', arg)
    finally:
        p.kill()
if __name__ == '__main__':
    pass
