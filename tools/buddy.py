#!/usr/bin/env python3
"""PC buddy: the robot on the desk reacts to what this computer is doing. Standard library only.

  buddy.py [--host teevee.local]      runs until stopped
  buddy.py --dry-run                  prints what it would send, needs no board

Busy computer for a minute: it sweats. Back after a break: it greets you. Low battery: it gets tired.
It connects only to send one message, and carries on if the board is off. The host can also come from TEEVEE_HOST.
"""
import argparse
import ctypes
import glob
import json
import os
import subprocess
import sys
import time

from teevee import NotLoggedIn, WS

BUSY_CPU = 0.85       # share of all cores
BUSY_AFTER = 60       # s above it before it shows
BUSY_AGAIN = 300      # s between sweats, however long the work goes on
AWAY_AFTER = 600      # s away that count as a break
LOW_BATTERY = 15      # percent
TIRED_AGAIN = 900     # s between tired reactions


class Buddy:
    """What the computer's state means for the robot. No I/O: step() takes the readings and returns the messages to send."""

    def __init__(self):
        self.busy_since = None
        self.last_sweat = None
        self.away_since = None
        self.last_tired = None

    def step(self, now, cpu=None, battery=None, charging=True, away=False):
        out = []
        if cpu is not None and cpu >= BUSY_CPU:
            if self.busy_since is None:
                self.busy_since = now
            if now - self.busy_since >= BUSY_AFTER and (self.last_sweat is None or now - self.last_sweat >= BUSY_AGAIN):
                self.last_sweat = now
                out.append({"emote": "sweat"})
        else:
            self.busy_since = None
        if away:
            if self.away_since is None:
                self.away_since = now
        else:
            if self.away_since is not None and now - self.away_since >= AWAY_AFTER:
                out.append({"event": "greet", "intensity": 0.8})
            self.away_since = None
        if battery is not None and battery <= LOW_BATTERY and not charging:
            if self.last_tired is None or now - self.last_tired >= TIRED_AGAIN:
                self.last_tired = now
                out.append({"event": "tired", "intensity": 0.7})
        else:
            self.last_tired = None
        return out


class Cpu:
    """Share of the processor in use since the last read; None until it can tell or where it cannot."""

    def __init__(self):
        self.last = None

    def read(self):
        now = self.times()
        if now is None:
            return self.load_average()
        last, self.last = self.last, now
        if last is None:
            return None
        busy, total = now[0] - last[0], now[1] - last[1]
        return busy / total if total > 0 else None

    @staticmethod
    def load_average():
        try:
            return min(1.0, os.getloadavg()[0] / (os.cpu_count() or 1))  # macOS
        except (OSError, AttributeError):
            return None

    @staticmethod
    def times():
        if sys.platform == "win32":
            idle, kernel, user = (ctypes.c_ulonglong() for _ in range(3))
            if not ctypes.windll.kernel32.GetSystemTimes(ctypes.byref(idle), ctypes.byref(kernel), ctypes.byref(user)):
                return None
            total = kernel.value + user.value  # (kernel time includes idle time)
            return total - idle.value, total
        try:
            with open("/proc/stat") as f:
                v = [int(x) for x in f.readline().split()[1:]]
        except (OSError, ValueError):
            return None
        idle = v[3] + (v[4] if len(v) > 4 else 0)  # idle + waiting for disks
        total = sum(v[:8])  # (guest time is already counted in user)
        return total - idle, total


def read_battery():
    """(percent or None, charging). A desktop has no battery: None."""
    if sys.platform == "win32":
        class Status(ctypes.Structure):
            _fields_ = [("ac", ctypes.c_ubyte), ("flag", ctypes.c_ubyte), ("percent", ctypes.c_ubyte), ("saver", ctypes.c_ubyte),
                        ("life", ctypes.c_ulong), ("full", ctypes.c_ulong)]
        s = Status()
        if not ctypes.windll.kernel32.GetSystemPowerStatus(ctypes.byref(s)) or s.percent > 100:
            return None, True
        return s.percent, s.ac == 1
    for d in glob.glob("/sys/class/power_supply/BAT*"):
        try:
            with open(d + "/capacity") as f:
                pct = int(f.read())
            with open(d + "/status") as f:
                return pct, f.read().strip().lower() != "discharging"
        except (OSError, ValueError):
            continue
    return None, True


def read_idle_seconds():
    """Seconds since the last key or mouse movement, where the system can say; None where it cannot."""
    if sys.platform == "win32":
        class Info(ctypes.Structure):
            _fields_ = [("size", ctypes.c_uint), ("time", ctypes.c_uint)]
        i = Info(ctypes.sizeof(Info), 0)
        if not ctypes.windll.user32.GetLastInputInfo(ctypes.byref(i)):
            return None
        return ((ctypes.windll.kernel32.GetTickCount() - i.time) & 0xFFFFFFFF) / 1000
    session = os.environ.get("XDG_SESSION_ID")
    if not session:
        return None
    try:
        out = subprocess.run(["loginctl", "show-session", session, "-p", "IdleHint", "-p", "IdleSinceHint"], capture_output=True, text=True, timeout=5).stdout
    except (OSError, subprocess.SubprocessError):
        return None
    v = dict(line.split("=", 1) for line in out.splitlines() if "=" in line)
    if v.get("IdleHint") != "yes":
        return 0 if "IdleHint" in v else None  # (not idle, or as far as the desktop tells it)
    try:
        return max(0, time.time() - int(v["IdleSinceHint"]) / 1e6)
    except (KeyError, ValueError):
        return None


def send(host, msg):
    """One message on its own connection (the board has few WebSocket slots: none is held). False if the board cannot be reached."""
    ws = None
    try:
        ws = WS(host)
        ws.request(msg)
        return True
    except NotLoggedIn as e:
        print(e, file=sys.stderr, flush=True)
        return False
    except (OSError, ConnectionError, ValueError):
        return False
    finally:
        if ws:
            ws.sock.close()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default=os.environ.get("TEEVEE_HOST", "teevee.local"))
    ap.add_argument("--every", type=float, default=5, help="seconds between looks at the computer")
    ap.add_argument("--dry-run", action="store_true", help="print what it would send, send nothing")
    a = ap.parse_args()
    buddy, cpu = Buddy(), Cpu()
    last_wall = time.time()
    resumed = False
    while True:
        time.sleep(a.every)
        wall = time.time()
        if wall - last_wall > a.every + AWAY_AFTER:  # the computer was asleep all that time: a break
            buddy.away_since = last_wall
            resumed = True
        last_wall = wall
        idle = read_idle_seconds()
        away = idle is not None and idle >= 60 and not resumed
        resumed = False
        pct, charging = read_battery()
        for msg in buddy.step(wall, cpu.read(), pct, charging, away):
            if a.dry_run:
                print(json.dumps(msg), flush=True)
            elif not send(a.host, msg):
                print("board not reachable, skipped:", json.dumps(msg), file=sys.stderr, flush=True)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
