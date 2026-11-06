#!/usr/bin/env python3
"""Regression check for a running TeeVee: exercises the Serial console, the WebSocket protocol and the HTTP API,
then (optionally) measures frame rate. Run after flashing or updating the firmware.

  python3 tools/regress.py [--host <board-ip>] [--port /dev/ttyUSB0] [--fps]

Needs the board on USB (for the web PIN and console) and on the same network as this PC. Leaves the board as it
found it (test faces/emotes are deleted, mappings restored).
"""
import argparse
import json
import re
import sys
import time
import urllib.error
import urllib.request

import serial

sys.path.insert(0, __file__.rsplit("/", 1)[0])
import teevee  # noqa: E402  (WebSocket client)

results = []


def check(name, cond, detail=""):
    results.append(bool(cond))
    print(("PASS  " if cond else "FAIL  ") + name + (f"   {detail}" if detail and not cond else ""))


class Console:
    def __init__(self, port):
        # Opening the port can reset the board (the USB adapter's auto-reset circuit pulses EN, depending on timing),
        # so wait until it answers before sending anything that matters.
        self.s = serial.Serial()
        self.s.port, self.s.baudrate, self.s.timeout = port, 115200, 0.2
        self.s.dtr = False
        self.s.rts = False
        self.s.open()
        deadline = time.time() + 25
        while time.time() < deadline:
            if "net:" in self.cmd("net", 0.8):
                break
        else:
            sys.exit("the board does not answer on " + port)
        self.s.reset_input_buffer()

    def toggle(self, line, word, want):
        """Commands like `s` and `b` flip a flag: send until the board says it is `want` (e.g. 'stats on')."""
        for _ in range(3):
            if f"{word} {'on' if want else 'off'}" in self.cmd(line, 0.4):
                return True
        return False

    def cmd(self, line, wait=0.6):
        self.s.write((line + "\n").encode())
        end, out = time.time() + wait, b""
        while time.time() < end:
            out += self.s.read(16384)
        return out.decode(errors="replace").strip()


def http(host, path, body=None, token=""):
    req = urllib.request.Request(f"http://{host}{path}", json.dumps(body).encode() if body is not None else None,
                                 {"Content-Type": "application/json", "X-Teevee-Token": token})
    try:
        with urllib.request.urlopen(req, timeout=10) as r:
            return r.status, json.loads(r.read() or b"{}")
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read() or b"{}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="teevee.local")
    ap.add_argument("--port", default="/dev/ttyUSB0")
    ap.add_argument("--fps", action="store_true", help="also measure frame rate (idle and forced full redraws)")
    a = ap.parse_args()
    c = Console(a.port)

    print("== console")
    cfg = c.cmd("cfg", 1.5)
    check("cfg lists every setting", len([l for l in cfg.splitlines() if l.strip()]) >= 51)
    check("style lists options", "eye_gap" in c.cmd("style") and "glint" in c.cmd("style"))
    check("event moves the emotion", "event: pet" in c.cmd("e pet", 1.0))
    check("emote list", "emotes:" in c.cmd("emote"))
    check("manual face and auto", "face: happy" in c.cmd("happy") and "face: auto" in c.cmd("auto"))
    check("mix", "face: happy->sad" in c.cmd("mix happy sad 0.5"))
    c.cmd("auto", 0.3)
    check("face editor", "fields:" in c.cmd("face") and "face smile = 0.50" in c.cmd("face smile 0.5"))
    c.cmd("auto", 0.3)
    check("event map lists events", "-> " in c.cmd("map", 0.8))
    check("pad and brain", "emotion v=" in c.cmd("pad") and "mind net:" in c.cmd("brain"))
    check("pomodoro", "started" in c.cmd("pomo 1", 0.5) and "stopped" in c.cmd("pomo 0", 0.5))
    c.cmd("ci pass main", 0.4)
    check("ci fail fires an event", "event: fail" in c.cmd("ci fail main", 0.8))
    c.cmd("ci none", 0.3)
    c.cmd("weather 12 rain", 0.3)
    c.cmd("weather off", 0.3)
    check("unknown command prints help", "Emotion:" in c.cmd("nonsense"))
    pin = re.search(r"web PIN: (\d{4})", c.cmd("pin", 0.6))
    check("web PIN shown", pin is not None)

    print("== WebSocket")
    login = http(a.host, "/api/login", {"pin": pin.group(1)})[1].get("token", "") if pin else ""
    try:
        teevee.WS(a.host, token="")
        check("WebSocket refused without login", False)
    except teevee.NotLoggedIn:
        check("WebSocket refused without login", True)
    try:
        teevee.WS(a.host, token="x" * 32)
        check("WebSocket refuses a bad token", False)
    except teevee.NotLoggedIn:
        check("WebSocket refuses a bad token", True)
    ws = teevee.WS(a.host, token=login)
    check("state", "state" in ws.request({"get": "state"}))
    check("settings with metadata", len(ws.request({"get": "settings"})["meta"]) >= 51)
    check("style with metadata", len(ws.request({"get": "style"})["meta"]) >= 25)
    check("custom store", len(ws.request({"get": "custom"})["fields"]) == 15)
    check("event, emote, show", ws.request({"event": "praise"}) == {"ok": True} and ws.request({"emote": "heart"}) == {"ok": True}
          and ws.request({"show": "happy", "ttl": 2}) == {"ok": True})
    check("custom face save and delete", ws.request({"face_save": {"name": "regress", "mood": False, "pad": [0, 0, 0]}}) == {"ok": True}
          and ws.request({"face_del": "regress"}) == {"ok": True})
    check("custom emote save, play, delete",
          ws.request({"emote_save": {"name": "regress", "face": "happy", "eyes": "star", "deco": "text", "text": "OK", "ms": 800}}) == {"ok": True}
          and ws.request({"emote": "regress"}) == {"ok": True} and ws.request({"emote_del": "regress"}) == {"ok": True})
    check("event map set and restore", ws.request({"map": {"pet": "love"}}) == {"ok": True} and ws.request({"map": {"pet": "auto"}}) == {"ok": True})
    check("unknown message and bad json refused", "error" in ws.request({"nope": 1}))

    print("== HTTP")
    page = urllib.request.urlopen(f"http://{a.host}/", timeout=5).read()
    check("page served", len(page) > 10000)
    check("API refused without login", http(a.host, "/api/info")[0] == 401)
    if pin:
        st, j = http(a.host, "/api/login", {"pin": pin.group(1)})
        tok = j.get("token", "")
        check("login with the PIN", st == 200 and len(tok) == 32)
        check("API works with the token", http(a.host, "/api/msg", {"get": "state"}, tok)[1].get("state", {}).get("idle") == "awake")
        check("bad token refused", http(a.host, "/api/info", token="x" * 32)[0] == 401)

    if a.fps:
        print("== frame rate")
        c.cmd("auto", 0.3)
        check("stats on", c.toggle("s", "stats", True))
        out = c.cmd("", 4.2)
        c.toggle("s", "stats", False)
        fps = [int(m) for m in re.findall(r"fps=(\d+)", out)]
        check("idle frame rate", fps and min(fps) >= 30, f"fps {fps}")
        print("      idle:", fps)
        check("bench on", c.toggle("b", "bench", True))
        out = c.cmd("", 4.2)
        c.toggle("b", "bench", False)
        fps = [int(m) for m in re.findall(r"fps=(\d+)", out)][1:]
        check("worst-case frame rate (every region redrawn each frame)", fps and min(fps) >= 30, f"fps {fps}")
        print("      worst case:", fps)

    c.cmd("auto", 0.3)
    print(f"\n{sum(results)}/{len(results)} checks passed")
    sys.exit(0 if all(results) else 1)


if __name__ == "__main__":
    main()
