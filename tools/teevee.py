#!/usr/bin/env python3
"""PC side of TeeVee: send events/emotions/features over WebSocket and feed weather + CI status.

Standard library only (tiny built-in WebSocket client).

  teevee.py [--host teevee.local] event pet [0.5]
  teevee.py emotion 0.6 0.3 0.1 [--ttl 10]
  teevee.py show happy [--ttl 5]
  teevee.py pomo 25              (0 = stop)
  teevee.py ci fail [name]       (pass | fail | running | none)
  teevee.py weather LAT LON [--every 900]   fetch Open-Meteo (no key) and push
  teevee.py ci-watch OWNER/REPO [--every 60] poll latest GitHub Actions run via `gh` and push
  teevee.py state                print the device state reports (Ctrl-C to stop)
"""
import argparse
import base64
import json
import os
import socket
import struct
import subprocess
import sys
import time
import urllib.request


TOKEN_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "etc", "token")


def saved_token():
    """The login: TEEVEE_TOKEN, or etc/token (`tools/apps.py list` asks for the PIN and saves it)."""
    tok = os.environ.get("TEEVEE_TOKEN", "")
    if not tok:
        try:
            with open(TOKEN_FILE) as f:
                tok = f.read().strip()
        except OSError:
            pass
    return tok


class NotLoggedIn(ConnectionError):
    def __str__(self):
        return "the board wants a login: run `python3 tools/apps.py list` and type the PIN shown on its screen"


class WS:
    """Minimal client: text frames only, masked, no extensions."""

    def __init__(self, host, port=81, timeout=5, token=None):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        token = saved_token() if token is None else token
        key = base64.b64encode(os.urandom(16)).decode()
        req = (f"GET / HTTP/1.1\r\nHost: {host}:{port}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
               f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\nX-Teevee-Token: {token}\r\n\r\n")
        self.sock.sendall(req.encode())
        resp = b""
        while b"\r\n\r\n" not in resp:
            chunk = self.sock.recv(1024)
            if not chunk:
                raise ConnectionError("handshake failed")
            resp += chunk
        if b" 401 " in resp.split(b"\r\n", 1)[0]:
            self.sock.close()
            raise NotLoggedIn()
        if b" 101 " not in resp.split(b"\r\n", 1)[0]:
            raise ConnectionError(resp.split(b"\r\n", 1)[0].decode(errors="replace"))
        self.buf = resp.split(b"\r\n\r\n", 1)[1]

    def send(self, obj):
        data = json.dumps(obj).encode()
        head = bytearray([0x81])
        n = len(data)
        if n < 126:
            head.append(0x80 | n)
        elif n < 65536:
            head += bytes([0x80 | 126]) + struct.pack(">H", n)
        else:
            head += bytes([0x80 | 127]) + struct.pack(">Q", n)
        mask = os.urandom(4)
        self.sock.sendall(bytes(head) + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(data)))

    def _read(self, n):
        while len(self.buf) < n:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError("closed")
            self.buf += chunk
        out, self.buf = self.buf[:n], self.buf[n:]
        return out

    def recv(self):
        """Next text message as parsed JSON (skips other frames)."""
        while True:
            b0, b1 = self._read(2)
            n = b1 & 0x7F
            if n == 126:
                n = struct.unpack(">H", self._read(2))[0]
            elif n == 127:
                n = struct.unpack(">Q", self._read(8))[0]
            payload = self._read(n)
            op = b0 & 0x0F
            if op == 0x8:
                raise ConnectionError("closed by device")
            if op == 0x1:
                return json.loads(payload)

    def request(self, obj):
        """Send and return the reply, skipping the periodic state broadcasts."""
        self.send(obj)
        while True:
            msg = self.recv()
            if "state" not in msg or obj.get("get") == "state":
                return msg


WMO = {0: "clear", 1: "fair", 2: "cloudy", 3: "overcast", 45: "fog", 48: "fog", 51: "drizzle", 53: "drizzle",
       55: "drizzle", 61: "rain", 63: "rain", 65: "heavy rain", 66: "ice rain", 67: "ice rain", 71: "snow",
       73: "snow", 75: "heavy snow", 77: "snow", 80: "showers", 81: "showers", 82: "storm", 85: "snow",
       86: "snow", 95: "thunder", 96: "thunder", 99: "thunder"}


def fetch_weather(lat, lon):
    url = (f"https://api.open-meteo.com/v1/forecast?latitude={lat}&longitude={lon}"
           "&current=temperature_2m,weather_code")
    with urllib.request.urlopen(url, timeout=10) as r:
        cur = json.load(r)["current"]
    return {"temp": cur["temperature_2m"], "text": WMO.get(cur["weather_code"], "?")}


def fetch_ci(repo):
    out = subprocess.run(["gh", "run", "list", "-R", repo, "-L", "1", "--json", "status,conclusion,workflowName"],
                         capture_output=True, text=True, check=True).stdout
    runs = json.loads(out)
    if not runs:
        return "none", ""
    run = runs[0]
    if run["status"] != "completed":
        return "running", run["workflowName"][:12]
    return ("pass" if run["conclusion"] == "success" else "fail"), run["workflowName"][:12]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default=os.environ.get("TEEVEE_HOST", "teevee.local"))
    ap.add_argument("--ttl", type=float)
    ap.add_argument("--every", type=float, help="repeat interval in seconds (weather / ci-watch)")
    ap.add_argument("cmd")
    ap.add_argument("args", nargs="*")
    a = ap.parse_args()
    try:
        ws = WS(a.host)
    except NotLoggedIn as e:
        sys.exit(str(e))

    def say(obj):
        print(json.dumps(ws.request(obj)))

    c, args = a.cmd, a.args
    if c == "event":
        say({"event": args[0], "intensity": float(args[1]) if len(args) > 1 else 1.0})
    elif c == "emotion":
        msg = {"emotion": dict(zip("vad", map(float, args[:3])))}
        if a.ttl:
            msg["ttl"] = a.ttl
        say(msg)
    elif c == "show":
        say({"show": args[0], "ttl": a.ttl or 5})
    elif c == "pomo":
        say({"show": {"pomodoro": float(args[0]) if args else 25}})
    elif c == "ci":
        say({"show": {"ci": {"status": args[0], "name": args[1] if len(args) > 1 else ""}}})
    elif c == "state":
        say({"get": "state"})
        while True:
            print(json.dumps(ws.recv()))
    elif c in ("weather", "ci-watch"):
        while True:
            try:
                if c == "weather":
                    w = fetch_weather(args[0], args[1])
                    print(w)
                    say({"show": {"weather": w}})
                else:
                    status, name = fetch_ci(args[0])
                    print(status, name)
                    say({"show": {"ci": {"status": status, "name": name}}})
            except (OSError, subprocess.CalledProcessError, KeyError, ValueError) as e:
                print("error:", e, file=sys.stderr)
            if not a.every:
                break
            time.sleep(a.every)
    else:
        ap.error(f"unknown command {c}")


if __name__ == "__main__":
    main()
