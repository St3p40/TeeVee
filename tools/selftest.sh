#!/bin/sh
# The nightly self-test: host tests, the renderer's golden check, the PC buddy's test and the alive probe; one line per run in the log, the detail of a failed run beside it.
# With TEEVEE_HOST set it also runs tools/regress.py on that board (it sends the board events and changes nothing saved for long).
# Run by hand: tools/selftest.sh    On a schedule: see tools/teevee-selftest.timer
cd "$(dirname "$0")/.." || exit 2
dir="${TEEVEE_LOG_DIR:-${XDG_STATE_HOME:-$HOME/.local/state}/teevee}"
mkdir -p "$dir" || exit 2
log="$dir/selftest.log"
detail="$dir/selftest-last-failure.txt"
tmp="$(mktemp -d)" || exit 2
trap 'rm -rf "$tmp"' EXIT
result=""
fail=0
step() {  # name, command...
  name="$1"; shift
  if "$@" >>"$tmp/out" 2>&1; then result="$result $name=ok"
  else result="$result $name=FAIL"; fail=1
  fi
}
step host make -C test
step golden make -C test golden-check
step buddy python3 tools/dev/buddy_test.py
step probe_build g++ -O2 -std=c++17 -DTEEVEE_MIND_N=128 -Isrc/core -Idesktop tools/dev/alive_probe.cpp desktop/engine.cpp -o "$tmp/alive_probe"
if [ -x "$tmp/alive_probe" ]; then
  # (a SCRIPTED row is a failure; WEAK rows are counted by the probe's last line, kept in the log)
  step alive "$tmp/alive_probe"
  result="$result [$(tail -n 1 "$tmp/out")]"
fi
if [ -n "$TEEVEE_HOST" ]; then step board python3 tools/regress.py --host "$TEEVEE_HOST"; fi
echo "$(date '+%Y-%m-%d %H:%M')$result" >>"$log"
if [ "$fail" != 0 ]; then cp "$tmp/out" "$detail"; fi
tail -n 1 "$log"
exit "$fail"
