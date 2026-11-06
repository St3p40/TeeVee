import sys, time, base64, subprocess, tempfile
sys.path.insert(0, sys.argv[1])
import marionette as mn
def shoot(shots, wait=3.5):
    prof = tempfile.mkdtemp()
    p = subprocess.Popen(['firefox', '--headless', '--marionette', '--profile', prof, '--no-remote'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        for _ in range(60):
            try: m = mn.M(); break
            except OSError: time.sleep(0.5)
        m.call('WebDriver:NewSession', {'capabilities': {}})
        for url, w, h, out, js in shots:
            m.call('WebDriver:SetWindowRect', {'width': w, 'height': h})
            m.call('WebDriver:Navigate', {'url': url}); time.sleep(wait)
            if js: m.call('WebDriver:ExecuteScript', {'script': js, 'args': []}); time.sleep(1)
            r = m.call('WebDriver:TakeScreenshot', {'full': True}); open(out, 'wb').write(base64.b64decode(r['value'])); print('shot', out)
    finally: p.kill()
if __name__ == '__main__':
    sp = sys.argv[1] + '/'
    shoot([(sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), sp + sys.argv[5], sys.argv[6] if len(sys.argv) > 6 else '')])
