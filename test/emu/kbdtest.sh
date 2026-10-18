#!/bin/bash
# cast/test/emu/kbdtest.sh - Type on phone in the emulator, end to end: the
# keys pressed on the 5mx reach fakeatom.py as the KEYS events in
# kbd.expect. Needs the card (node --experimental-strip-types
# core/tools/emu/mkcard.ts psicast) and the emulator's harness
# (core/tools/emu/README.md).
#
#   test/emu/kbdtest.sh                 # -> build/emu/castkbd/, PASS or FAIL
#
# PsiCast is not sharing, so Shift+Ctrl+B connects for the keyboard only
# (modem route: the serial bridge to fakeatom.py, which plays an Atom with a
# phone keyboard: KBD 2, then 3 a second later). Then the keys, then
# Shift+Ctrl+B again, which leaves and hangs up.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
OUT=$REPO/build/emu/castkbd
SOCK=/tmp/psicast-kbd/sock               # (a socket path must be short)
mkdir -p /tmp/psicast-kbd "$OUT.atom"
rm -f "$OUT.atom/atom.log"
python3 "$REPO/test/fakeatom.py" --unix "$SOCK" --out "$OUT.atom" --answer-status --sim-clock --ping 1 \
	--kbd --pair-after 1 --expect-keys "$HERE/kbd.expect" > /dev/null 2>&1 &
ATOM=$!
trap 'kill $ATOM 2>/dev/null || true' EXIT
sleep 1
C="22 40"; S="18 40"; F="24 30"; M="148 60"     # Ctrl, Shift, Fn, Menu held
EMU_SERIAL=$SOCK EMU_SECS=${EMU_SECS:-12} "$REPO/core/tools/emu/run.sh" castkbd \
	"50 $C" "50.05 $S" "50.2 66" \
	"66 72" \
	"67 $S" "67.1 73" \
	"68 $F" "68.1 52" \
	"69 $S" "69.1 51" \
	"70 $C" "70.1 67" \
	"71 $M" "71.15 2" "71.45 2" \
	"73 14" "73.5 16" \
	"74 $S" "74.1 15" \
	"75 $F" "75.1 16" \
	"76 $F" "76.1 14" \
	"77 3" "77.5 4" "78 1" \
	"78.5 $F" "78.6 1" "78.8 18 15" "78.85 1" \
	"79 $C" "79.1 49" \
	"80 5" "80.5 148" \
	"84 $C" "84.05 $S" "84.2 66" > /dev/null
sleep 2
kill $ATOM 2>/dev/null || true
wait $ATOM 2>/dev/null || true
trap - EXIT
grep -E "keys:|summary" "$OUT.atom/atom.log" | tail -2
echo "screenshots: $OUT/ (s-*.png, end.png)"
grep -q "keys: .*PASS" "$OUT.atom/atom.log"
