#!/bin/bash
# Builds PsiCast for the Psion Series 5mx and packages dist/PsiCast.sis.
#   PSION_SDK=/path/to/psion_cpp_sdk_linux ./build.sh
# (or, on a machine without the SDK's tools: core/tools/docker/psibuild "bash build.sh")
#
#   PSICAST_SIS=path   where the .sis goes (default dist/PsiCast.sis, in this checkout)
#
# The shared code (psiglue.cpp, pwnet.c, zlib, psihold.mk, the tools) is
# psion-core, this checkout's core/ submodule (git submodule update --init).
# PsiCast.app is built by makmake in a project of its own (castproj), so
# its objects never mix with the other programs' in the SDK's shared
# ptproj build folder. psicast.exe is engine/engine.mk.
set -e
: "${PSION_SDK:?set PSION_SDK to your psion_cpp_sdk_linux directory}"
HERE=$(cd "$(dirname "$0")" && pwd)
TOP=$HERE
CORE=${PSION_CORE:-$HERE/core}
[ -f "$CORE/ssh/psiglue.cpp" ] || { echo "no psion-core in $CORE: git submodule update --init"; exit 1; }
export EPOCROOT=$PSION_SDK/epoc_cpp_sdk
REL=$EPOCROOT/epoc32/release/marm/rel
SIS=${PSICAST_SIS:-$HERE/dist/PsiCast.sis}
mkdir -p "$TOP/build"

echo "== psicast.exe"
make -C "$TOP" -f engine/engine.mk PSION_SDK="$PSION_SDK" > "$TOP/build/cast-engine.log" 2>&1 \
	|| { grep -E "error|undefined" "$TOP/build/cast-engine.log" | head -30; exit 1; }

echo "== pictures (PsiCast.mbm)"
python3 "$HERE/pkg/mktbar.py" "$TOP/build/cast-icons" "$HERE/app/csicons.h"
(cd "$TOP/build/cast-icons" && WINEDEBUG=-all wine "$EPOCROOT/epoc32/tools/bmconv.exe" /q psicast.mbm $(tr '\n' ' ' < files.txt) > /dev/null 2>&1)

echo "== PsiCast.app"
(
export PATH=/usr/bin:/bin:$PSION_SDK/gcc-3.0-psion-98r2-9/bin:$EPOCROOT/epoc32/tools
export TMP=$EPOCROOT/tmp WINEDEBUG=-all
mkdir -p "$TMP" "$EPOCROOT/castproj"
for l in psicast:$HERE/app psicastinc:$HERE/engine psitermssh:$CORE/ssh; do
	name=${l%%:*}; dir=${l#*:}
	if [ -e "$EPOCROOT/castproj/$name" ] && [ ! -L "$EPOCROOT/castproj/$name" ]; then
		echo "$EPOCROOT/castproj/$name exists and is not a link - move it away"; exit 1
	fi
	ln -sfn "$dir" "$EPOCROOT/castproj/$name"
done
cd "$EPOCROOT/castproj/psicast"
makmake psicast marm > "$TOP/build/psicast-makmake.log" 2>&1
make -f psicast.marm rel > "$TOP/build/psicast-app.log" 2>&1 \
	|| { grep -i -B1 -A4 "error\|undefined" "$TOP/build/psicast-app.log" | head -60; exit 1; }
)

# psihold.exe: holds the Psion Internet connection up after use (core/ssh/psihold.cpp)
echo "== psihold.exe"
make -f "$CORE/ssh/psihold.mk" PSION_SDK="$PSION_SDK" TOP="$TOP" > "$TOP/build/psihold.log" 2>&1 \
	|| { grep -E "error|undefined" "$TOP/build/psihold.log" | head -20; exit 1; }

echo "== package"
P=$TOP/build/cast-pkg
rm -rf "$P"; mkdir -p "$P" "$(dirname "$SIS")"
cp "$REL/psicast.app" "$REL/psicast.rsc" "$HERE/pkg/psicast.pkg" "$TOP/build/cast-icons/psicast.mbm" "$TOP/build/psihold/psihold.exe" "$P/"
cp "$TOP/build/cast-epoc/psicast.exe" "$P/psicast.exe"
install -m 644 "$REL/stdlib.sis" "$P/STDLIB.SIS"   # ESTLIB.DLL for psicast.exe (not in the 5mx ROM)
python3 "$HERE/pkg/mkicon.py" "$P/psicast.aif" > /dev/null
(cd "$P" && WINEDEBUG=-all wine "$EPOCROOT/epoc32/tools/makesis.exe" psicast.pkg psicast.sis > /dev/null)
mv "$P/psicast.sis" "$SIS"
ls -la "$P/psicast.exe" "$SIS"
