#!/bin/bash
# Copies this repository's committed tree (HEAD) into DIR for the public
# repository, leaving out the local files (*_local.*, *.local.*: pictures
# for programs that are not published) and checking that no words from
# tools/public-check.local (git-ignored, one extended regex a line), if it
# is there, are left in what is copied.
#   tools/export-public.sh DIR
set -e
DIR=${1:?usage: tools/export-public.sh DIR}
HERE=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$DIR"
git -C "$HERE" archive HEAD | tar -x -C "$DIR"
find "$DIR" \( -name '*_local.*' -o -name '*.local.*' -o -name '*.local' \) -print -delete
[ -f "$DIR/tools/public-check.local" ] && rm "$DIR/tools/public-check.local"
# (the iOS app and the M5Paper viewer are not published yet)
rm -rf "$DIR/ios" "$DIR/paper"
if [ -f "$HERE/tools/public-check.local" ]; then
	if grep -r -I -n -i -E -f "$HERE/tools/public-check.local" "$DIR" --exclude-dir=.git; then
		echo "export-public: the lines above name something private"; exit 1
	fi
fi
echo "export-public: $DIR"
