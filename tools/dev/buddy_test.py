#!/usr/bin/env python3
"""Tests tools/buddy.py with no board: its decisions, and one message through a small local WebSocket server. Run: python3 tools/dev/buddy_test.py"""
import base64
import hashlib
import json
import os
import socket
import struct
import sys
import threading

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import buddy as B

fails = 0


def check(ok, what):
    global fails
    if not ok:
        fails += 1
        print("FAIL", what)


def run(b, start, secs, **state):
    out = []
    for t in range(start, start + secs, 5):
        out += b.step(t, **state)
    return out


b = B.Buddy()
check(run(b, 0, 55, cpu=0.95) == [], "a short burst is not sweat")
check(run(b, 55, 10, cpu=0.95) == [{"emote": "sweat"}], "a busy minute sweats")
check(run(b, 65, 200, cpu=0.95) == [], "not again straight away")
check(run(b, 265, 110, cpu=0.95) == [{"emote": "sweat"}], "again after five minutes")
b = B.Buddy()
run(b, 0, 40, cpu=0.95)
run(b, 40, 5, cpu=0.1)
check(run(b, 45, 40, cpu=0.95) == [], "a quiet moment starts the count again")
check(run(B.Buddy(), 0, 600, cpu=None) == [], "no reading, no reaction")

b = B.Buddy()
check(run(b, 0, 500, away=True) == [] and run(b, 500, 5, away=False) == [], "a short absence: no greeting")
b = B.Buddy()
run(b, 0, 700, away=True)
check(b.step(700, away=False) == [{"event": "greet", "intensity": 0.8}], "back after a break: greets")
check(b.step(705, away=False) == [], "once")

b = B.Buddy()
check(run(b, 0, 30, battery=50, charging=False) == [], "battery fine")
tired = {"event": "tired", "intensity": 0.7}
check(run(b, 30, 5, battery=14, charging=False) == [tired], "low battery: tired")
check(run(b, 35, 600, battery=14, charging=False) == [], "not every few seconds")
check(run(b, 635, 300, battery=13, charging=False) == [tired], "again after a while")
check(run(B.Buddy(), 0, 30, battery=10, charging=True) == [], "charging: fine")
check(run(B.Buddy(), 0, 30, battery=None, charging=True) == [], "no battery")

cpu = B.Cpu()
if cpu.times():
    check(cpu.read() is None, "first read has nothing to compare")
    for _ in range(2000000):
        pass
    v = cpu.read()
    check(v is None or 0 <= v <= 1, f"cpu share in range: {v}")
pct, charging = B.read_battery()
check(pct is None or 0 <= pct <= 100, "battery in range")
idle = B.read_idle_seconds()
check(idle is None or idle >= 0, "idle time in range")

# A real round trip: the handshake and frames the board's server would do.
got = []
srv = socket.socket()
srv.bind(("127.0.0.1", 0))
srv.listen(1)
port = srv.getsockname()[1]


def serve():
    c, _ = srv.accept()
    req = b""
    while b"\r\n\r\n" not in req:
        req += c.recv(1024)
    key = [l.split(b": ")[1] for l in req.split(b"\r\n") if l.lower().startswith(b"sec-websocket-key")][0]
    acc = base64.b64encode(hashlib.sha1(key + b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11").digest())
    c.sendall(b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + acc + b"\r\n\r\n")
    head = c.recv(2)
    n = head[1] & 127
    mask = c.recv(4)
    data = bytes(x ^ mask[i % 4] for i, x in enumerate(c.recv(n)))
    got.append(json.loads(data))
    reply = b'{"ok":true}'
    c.sendall(struct.pack("BB", 0x81, len(reply)) + reply)
    c.close()


t = threading.Thread(target=serve, daemon=True)
t.start()
real_ws = B.WS
B.WS = lambda host: real_ws(host, port)
check(B.send("127.0.0.1", {"emote": "sweat"}), "sends")
t.join(3)
check(got == [{"emote": "sweat"}], f"the server got it: {got}")
B.WS = lambda host: real_ws(host, 1)
check(not B.send("127.0.0.1", {"emote": "sweat"}), "no board: False, no crash")

print(f"buddy_test: {'ok' if not fails else str(fails) + ' FAILED'}")
sys.exit(1 if fails else 0)
