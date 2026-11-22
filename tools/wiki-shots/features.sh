#!/usr/bin/env bash
# Regenerates the pictures of the wiki's feature pages.
#
#   tools/wiki-shots/features.sh <output dir> [shot ...]
#
# Starts its own three-rig virtual bench (scripts/testrig.sh) with empty
# scratch logbooks, builds the Standalone bundle into a temp dir and serves it
# on port 8765, runs features.mjs, shrinks the pictures (ImageMagick), then
# stops the bench and puts the previous scratch logbooks back.  Refuses to
# run over a bench that is already up.
# Never touches a real radio.  Rebuild ./wfweb first so the page is current.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
out=${1:?usage: features.sh <output dir> [shot ...]}; shift
mkdir -p "$out"; out=$(cd "$out" && pwd)
cd "$REPO"

if pgrep -x virtualrig >/dev/null; then
    echo "features.sh: a virtual bench is running; stop it first (scripts/testrig.sh down)" >&2
    exit 1
fi

tmp=$(mktemp -d)
mkdir -p .testrig "$tmp/logbooks"
mv .testrig/logbook_*.adi "$tmp/logbooks/" 2>/dev/null || true
http=
cleanup() {
    [[ -n "$http" ]] && kill "$http" 2>/dev/null || true
    scripts/testrig.sh down >/dev/null 2>&1 || true
    rm -f .testrig/logbook_*.adi
    mv "$tmp"/logbooks/* .testrig/ 2>/dev/null || true
    rm -rf "$tmp"
}
trap cleanup EXIT

tools/build-static.sh "$tmp/dist" >/dev/null
python3 -m http.server 8765 --bind 127.0.0.1 -d "$tmp/dist" >/dev/null 2>&1 & http=$!

scripts/testrig.sh up 3 >/dev/null
sleep 5
node "$HERE/features.mjs" "$out" "$@"

# Wiki-sized: at most 1600 px a side, 256 colours (a full screen goes from
# ~600 KB to ~150 KB and looks the same).
if command -v magick >/dev/null; then
    for f in "$out"/*.png; do magick "$f" -resize '1600x1600>' -strip -colors 256 "PNG8:$f"; done
fi
