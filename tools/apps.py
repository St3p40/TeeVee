#!/usr/bin/env python3
"""Manage Lua apps on a TeeVee over HTTP (stdlib only). The first use asks for the PIN shown on the board's screen
and remembers the login in etc/token (kept out of git).

  apps.py [--host teevee.local] list
  apps.py put apps/clock.lua [name]     upload (name defaults to the file name) and keep it
  apps.py run apps/clock.lua            upload and run it right away
  apps.py image logo.ba [name]          upload a picture for gfx.image (to the SD card if there is one)
  apps.py behaviour [file]              print the behaviour settings, or put a behaviour file on the board
  apps.py start clock                   run an app that is already on the board
  apps.py get clock                     print its source
  apps.py rm clock                      delete it
  apps.py stop                          back to the face
"""
import argparse
import json
import os
import re
import sys
import urllib.error
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOKEN_FILE = os.path.join(ROOT, "etc", "token")


def call(host, path, body=None, token="", ctype="application/json", method=None, timeout=15):
    data = json.dumps(body).encode() if isinstance(body, dict) else body
    req = urllib.request.Request(f"http://{host}{path}", data, {"Content-Type": ctype, "X-Teevee-Token": token}, method=method)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.status, r.read()
    except urllib.error.HTTPError as e:
        return e.code, e.read()


def login(host):
    try:
        with open(TOKEN_FILE) as f:
            tok = f.read().strip()
        if call(host, "/api/info", token=tok)[0] == 200:
            return tok
    except OSError:
        pass
    call(host, "/api/info")  # an unauthenticated request makes the board show its PIN
    while True:
        pin = input("PIN shown on TeeVee's screen: ").strip()
        status, raw = call(host, "/api/login", {"pin": pin})
        tok = json.loads(raw or b"{}").get("token")
        if tok:
            fd = os.open(TOKEN_FILE, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
            with os.fdopen(fd, "w") as f:
                f.write(tok)
            return tok
        print("  wrong PIN (5 misses lock it for a minute)")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default=os.environ.get("TEEVEE_HOST", "teevee.local"))
    ap.add_argument("cmd", choices=["list", "put", "run", "start", "get", "rm", "stop"])
    ap.add_argument("args", nargs="*")
    a = ap.parse_args()
    tok = login(a.host)

    def msg(obj):
        status, raw = call(a.host, "/api/msg", obj, tok)
        return json.loads(raw or b"{}")

    def put(path, name=None):
        name = name or re.sub(r"[^a-z0-9_-]", "_", os.path.basename(path).rsplit(".", 1)[0].lower())[:11]
        with open(path, "rb") as f:
            src = f.read()
        status, raw = call(a.host, f"/api/app/{name}", src, tok, "text/plain")
        j = json.loads(raw or b"{}")
        if not j.get("ok"):
            sys.exit(f"upload failed: {j.get('error', status)}")
        print(f"uploaded {name} ({len(src)} bytes)")
        return name

    if a.cmd == "list":
        j = msg({"get": "apps"})
        print(f"{len(j['apps'])} app(s), {j['free'] // 1024} KB free" + (f", running: {j['running']}" if j["running"] else ""))
        for n in j["apps"]:
            print(" ", n + ("  (running)" if n == j["running"] else ""))
    elif a.cmd in ("put", "run"):
        if not a.args:
            sys.exit("which file?")
        name = put(a.args[0], a.args[1] if len(a.args) > 1 else None)
        if a.cmd == "run":
            print("running" if msg({"app": name}).get("ok") else "could not run it")
    elif a.cmd == "image":
        if not a.args:
            sys.exit("which file?")
        name = a.args[1] if len(a.args) > 1 else re.sub(r"[^a-z0-9_-]", "_", os.path.basename(a.args[0]).rsplit(".", 1)[0].lower())[:11]
        with open(a.args[0], "rb") as f:
            data = f.read()
        if data[:2] != b"bA":
            sys.exit("not a Bitmap Array file (it starts with bA)")
        status, raw = call(a.host, f"/api/image/{name}", data, tok, "application/octet-stream")
        j = json.loads(raw or b"{}")
        print(f"uploaded {name} ({len(data)} bytes)" if j.get("ok") else f"upload failed: {j.get('error', status)}")
    elif a.cmd == "behaviour":
        if not a.args:  # the settings in force
            sys.stdout.write(msg({"get": "behaviour"}).get("behaviour", ""))
        else:
            with open(a.args[0], "rb") as f:
                data = f.read()
            status, raw = call(a.host, "/api/behaviour", data, tok, "text/plain")
            j = json.loads(raw or b"{}")
            print("behaviour file in force" if j.get("ok") else f"refused: {j.get('error', status)}")
    elif a.cmd == "start":
        print("running" if msg({"app": a.args[0]}).get("ok") else "no such app")
    elif a.cmd == "get":
        status, raw = call(a.host, f"/api/app/{a.args[0]}", None, tok)
        sys.stdout.write(raw.decode(errors="replace") if status == 200 else f"error {status}\n")
    elif a.cmd == "rm":
        print("deleted" if msg({"app_del": a.args[0]}).get("ok") else "no such app")
    elif a.cmd == "stop":
        msg({"app": "stop"})
        print("back to the face")


if __name__ == "__main__":
    main()
