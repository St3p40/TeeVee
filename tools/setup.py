#!/usr/bin/env python3
"""Configure a TeeVee board from the files in etc/ (run from anywhere).

  etc/wifi.conf    Wi-Fi name + password (private; asked for and saved on first run)
  etc/teevee.conf  timezone, face style, weather location, CI repo, serial port

Steps: [flash firmware] -> Wi-Fi -> timezone -> style -> wait for network -> WebSocket check
-> push weather / CI once. Settings are stored on the board, so this is only needed after a change.

  python3 tools/setup.py            apply settings
  python3 tools/setup.py --flash    build + flash the firmware first
Needs pyserial (installed with PlatformIO).
"""
import argparse
import configparser
import getpass
import glob
import os
import re
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import apps  # noqa: E402  (the PIN login)
import teevee  # noqa: E402  (WebSocket client, weather/CI fetchers)

try:
    import serial
except ImportError:
    sys.exit("pyserial missing: pip install pyserial (or run with PlatformIO's python)")


def step(msg):
    print(f"\n== {msg}")


def read_conf(path):
    c = configparser.ConfigParser(interpolation=None)  # passwords may contain %
    c.read(path)
    return c


def get(c, section, key):
    return c.get(section, key, fallback="").strip()


def wifi_creds(etc):
    path = os.path.join(etc, "wifi.conf")
    c = read_conf(path)
    ssid, pw = get(c, "wifi", "ssid"), c.get("wifi", "password", fallback="")
    if ssid:
        return ssid, pw
    if not sys.stdin.isatty():
        sys.exit(f"no Wi-Fi settings: copy etc/wifi.conf.example to {path} and fill it in")
    print(f"{path} not found - enter Wi-Fi details (2.4 GHz network).")
    ssid = input("  network name: ").strip()
    pw = getpass.getpass("  password: ")
    c = configparser.ConfigParser(interpolation=None)
    c["wifi"] = {"ssid": ssid, "password": pw}
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w") as f:
        c.write(f)
    print(f"  saved to {path} (readable only by you, ignored by git)")
    return ssid, pw


def posix_tz(value):
    """IANA name / empty (= this PC) -> POSIX TZ string from the zoneinfo file footer."""
    path = os.path.join("/usr/share/zoneinfo", value) if value else "/etc/localtime"
    if value and not os.path.isfile(path) and re.search(r"\d", value):
        return value  # already POSIX, e.g. CET-1CEST,M3.5.0,M10.5.0/3
    try:
        with open(path, "rb") as f:
            footer = f.read().rstrip(b"\n").rsplit(b"\n", 1)[-1].decode(errors="replace")
    except OSError:
        sys.exit(f"unknown timezone {value or '(system)'}")
    return footer if re.fullmatch(r"[A-Za-z<>+\-0-9,./:]+", footer or "") else "UTC0"


class Board:
    def __init__(self, port):
        # Set DTR/RTS before opening: toggling them after open pulses the board's reset/boot lines
        # (auto-reset circuit), which reboots it or can leave it in download mode.
        self.s = serial.Serial()
        self.s.port, self.s.baudrate, self.s.timeout = port, 115200, 0.2
        self.s.dtr = False
        self.s.rts = False
        self.s.open()
        time.sleep(1.5)
        self.s.reset_input_buffer()

    def cmd(self, line, wait=0.5, show=True):
        self.s.write((line + "\n").encode())
        end, out = time.time() + wait, b""
        while time.time() < end:
            out += self.s.read(1024)
        text = out.decode(errors="replace").strip()
        if show and text:
            print("  " + text.replace("\n", "\n  "))
        return text


def find_port(conf_port):
    if conf_port:
        return conf_port
    ports = sorted(glob.glob("/dev/ttyUSB*") + glob.glob("/dev/ttyACM*"))
    if not ports:
        sys.exit("no board found on /dev/ttyUSB* or /dev/ttyACM* (set [device] port in etc/teevee.conf)")
    return ports[0]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--etc", default=os.path.join(ROOT, "etc"), help="config folder (default: etc/)")
    ap.add_argument("--flash", action="store_true", help="build and flash the firmware first")
    ap.add_argument("--timeout", type=float, default=40, help="seconds to wait for Wi-Fi")
    a = ap.parse_args()

    conf = read_conf(os.path.join(a.etc, "teevee.conf"))
    ssid, pw = wifi_creds(a.etc)
    port = find_port(get(conf, "device", "port"))

    if a.flash:
        step("build + flash")
        r = subprocess.run(["pio", "run", "-t", "upload", "--upload-port", port], cwd=ROOT,
                           capture_output=True, text=True)
        if r.returncode:
            print("\n".join(l for l in r.stdout.splitlines() if "error" in l.lower())[-2000:])
            sys.exit("flash failed")
        print("  flashed")
        time.sleep(2)

    step(f"board on {port}")
    b = Board(port)

    step(f"Wi-Fi: {ssid}")
    quoted = f'"{ssid}"' if " " in ssid else ssid
    b.cmd(f"wifi {quoted} {pw}")  # the board echoes only the network name

    tz = posix_tz(get(conf, "time", "timezone"))
    step(f"timezone: {tz}")
    b.cmd(f"tz {tz}")

    step("style")
    preset = get(conf, "style", "preset")
    if preset:
        b.cmd(f"style {preset}", show=False)
    out = ""
    for key, val in (conf["style"].items() if conf.has_section("style") else []):
        if key != "preset" and val.strip():
            out = b.cmd(f"style {key} {val.strip()}", show=False)
    print("  " + (out or b.cmd("style", show=False)))

    if conf.has_section("settings"):
        step("settings")
        for key, val in conf["settings"].items():
            if val.strip():
                reply = b.cmd(f"cfg {key} {val.strip()}", show=False)
                bad = [l for l in reply.splitlines() if l.startswith("cfg: bad")]
                print("  " + (bad[0] if bad else f"{key} = {val.strip()}"))

    step("waiting for network")
    ip, end = None, time.time() + a.timeout
    while time.time() < end and not ip:
        m = re.search(r"net: (\d+\.\d+\.\d+\.\d+)", b.cmd("net", wait=1.0, show=False))
        ip = m.group(1) if m else None
    if not ip:
        sys.exit("  no connection - check etc/wifi.conf (2.4 GHz only) and run again")
    print(f"  connected: {ip}")

    host = get(conf, "device", "host") or ip
    step(f"WebSocket check: ws://{host}:81")
    try:
        ws = teevee.WS(host, token=apps.login(host))  # asks for the PIN on the board's screen the first time
        before = ws.request({"get": "state"})["state"]["face"]
        print("  event greet ->", ws.request({"event": "greet", "intensity": 0.8}))
        time.sleep(0.5)
        print(f"  face: {before} -> {ws.request({'get': 'state'})['state']['face']}")
    except (OSError, ConnectionError, KeyError, ValueError) as e:
        sys.exit(f"  WebSocket failed: {e} (is this PC on the same network?)")

    lat, lon, repo = get(conf, "weather", "latitude"), get(conf, "weather", "longitude"), get(conf, "ci", "repo")
    if lat and lon:
        step("weather")
        try:
            w = teevee.fetch_weather(lat, lon)
            print("  ", w, ws.request({"show": {"weather": w}}))
        except (OSError, KeyError, ValueError) as e:
            print("  weather failed:", e)
    if repo:
        step(f"CI: {repo}")
        try:
            status, name = teevee.fetch_ci(repo)
            print("  ", status, name, ws.request({"show": {"ci": {"status": status, "name": name}}}))
        except (OSError, subprocess.CalledProcessError, ValueError) as e:
            print("  CI failed:", e)

    step("done")
    print(f"  TeeVee is at ws://{host}:81 (also teevee.local)")
    if lat and lon:
        print(f"  keep weather fresh: python3 tools/teevee.py --host {host} weather {lat} {lon} --every 900")
    if repo:
        print(f"  keep CI fresh:      python3 tools/teevee.py --host {host} ci-watch {repo} --every 60")


if __name__ == "__main__":
    main()
