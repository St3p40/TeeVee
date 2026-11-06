#!/usr/bin/env python3
"""Make and read TeeVee packs (one file with looks, a behaviour file, apps and pictures). Standard library only. See src/core/pack.h.

  pack.py make myfolder out.tvpack    from myfolder/looks/*.txt, apps/*.lua, images/*.ba, behaviour.txt, about.txt
  pack.py list some.tvpack            what is in it
  pack.py unpack some.tvpack folder   back into a folder (binary backup entries go under robot/)
  pack.py send some.tvpack [--all] [--host teevee.local]   install it on a board (--all: also what it has learned, replacing the board's)
"""
import argparse
import glob
import json
import os
import re
import sys

MAGIC = b"teevee-pack 1\n"
NAME = re.compile(r"^[a-z0-9_-]{1,11}$")
ROBOT = {"settings", "custom", "tastes", "diary", "mind", "wardrobe", "looknet", "cfg", "appdata"}


def name_of(path):
    return re.sub(r"[^a-z0-9_-]", "_", os.path.basename(path).rsplit(".", 1)[0].lower())[:11]


def build(entries):
    out = bytearray(MAGIC)
    for kind, name, data in entries:
        if not NAME.match(name):
            sys.exit(f"bad name: {name}")
        out += f"{kind} {name} {len(data)}\n".encode() + data + b"\n"
    return bytes(out) + f"end {len(entries)}\n".encode()


def parse(data):
    """The entries as (kind, name, bytes). ValueError if it is not a whole pack."""
    if not data.startswith(MAGIC):
        raise ValueError("not a TeeVee pack")
    at, entries = len(MAGIC), []
    while True:
        nl = data.find(b"\n", at)
        if nl < 0:
            raise ValueError("cut off")
        words = data[at:nl].decode(errors="replace").split(" ")
        at = nl + 1
        if words[0] == "end" and len(words) == 2:
            if int(words[1]) != len(entries) or at != len(data):
                raise ValueError("cut off, or something after the end")
            return entries
        if len(words) != 3 or not words[2].isdigit() or not NAME.match(words[1]):
            raise ValueError(f"bad line: {words}")
        size = int(words[2])
        if at + size >= len(data) or data[at + size] != 10:
            raise ValueError("cut off")
        entries.append((words[0], words[1], data[at:at + size]))
        at += size + 1


def make(folder):
    entries = []
    for kind, pattern in (("look", "looks/*.txt"), ("app", "apps/*.lua"), ("image", "images/*.ba")):
        for path in sorted(glob.glob(os.path.join(folder, pattern))):
            with open(path, "rb") as f:
                entries.append((kind, name_of(path), f.read().strip() if kind == "look" else f.read()))
    for kind, file_name, name in (("behaviour", "behaviour.txt", "main"), ("about", "about.txt", "pack")):
        path = os.path.join(folder, file_name)
        if os.path.exists(path):
            with open(path, "rb") as f:
                entries.append((kind, name, f.read()))
    if not entries:
        sys.exit("nothing to pack: expected looks/, apps/, images/ or behaviour.txt")
    return build(entries)


def unpack(entries, folder):
    where = {"look": ("looks", ".txt"), "app": ("apps", ".lua"), "image": ("images", ".ba")}
    for kind, name, data in entries:
        sub, ext = where.get(kind, (None, None))
        if kind == "behaviour":
            path = os.path.join(folder, "behaviour.txt")
        elif kind == "about":
            path = os.path.join(folder, "about.txt")
        elif sub:
            path = os.path.join(folder, sub, name + ext)
        else:
            path = os.path.join(folder, "robot", f"{kind}-{name}.bin")
        os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
        with open(path, "wb") as f:
            f.write(data)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["make", "list", "unpack", "send"])
    ap.add_argument("a")
    ap.add_argument("b", nargs="?")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--host", default=os.environ.get("TEEVEE_HOST", "teevee.local"))
    args = ap.parse_args()
    if args.cmd == "make":
        data = make(args.a)
        with open(args.b or "out.tvpack", "wb") as f:
            f.write(data)
        print(f"{args.b or 'out.tvpack'}: {len(parse(data))} entries, {len(data)} bytes")
        return
    with open(args.a, "rb") as f:
        data = f.read()
    try:
        entries = parse(data)
    except ValueError as e:
        sys.exit(f"{args.a}: {e}")
    if args.cmd == "list":
        for kind, name, body in entries:
            print(f"{kind:10} {name:12} {len(body):8} bytes" + ("   (what a robot has learned)" if kind in ROBOT else ""))
    elif args.cmd == "unpack":
        unpack(entries, args.b or ".")
        print(f"{len(entries)} entries written to {args.b or '.'}")
    else:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import apps
        token = apps.login(args.host)
        status, raw = apps.call(args.host, "/api/packall" if args.all else "/api/pack", data, token, "application/octet-stream", timeout=180)  # (it installs before it answers)
        j = json.loads(raw or b"{}")
        print(j.get("report") or f"refused: {j.get('error', status)}")


if __name__ == "__main__":
    main()
