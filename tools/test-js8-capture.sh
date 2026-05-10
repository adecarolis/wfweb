#!/bin/bash
# Phase 1 Loop C: capture JS8Call transmissions through the testrig
# PulseAudio bridge, save as 12 kHz mono Int16 WAVs + JSON metadata.
#
# Prereqs (run first, in this order):
#   ./scripts/testrig.sh up 0 1 --broadcast
#   Open JS8Call, configure radio + audio against external slot A
#     (Hamlib NET rigctl on 127.0.0.1:4532, output=virtualrig-A-output,
#      input=virtualrig-A-input). Set My Call to K1FM. Mode = JS8 Normal.
#
# Then run this and follow the prompts.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUTDIR="$REPO_ROOT/resources/js8-src/test/corpus/normal"
DEVICE="virtualrig-A-output.monitor"

# Sanity checks.
if ! pactl list short sources 2>/dev/null | awk '{print $2}' | grep -qx "$DEVICE"; then
    echo "ERROR: $DEVICE not present in PulseAudio." >&2
    echo "       Did you run: ./scripts/testrig.sh up 0 1 --broadcast ?" >&2
    exit 1
fi
if ! command -v ffmpeg >/dev/null 2>&1; then
    echo "ERROR: ffmpeg not on PATH (used to write 12 kHz mono WAVs)." >&2
    exit 1
fi

mkdir -p "$OUTDIR"

# Predetermined messages. Each is meant to fit a single JS8 frame
# (mostly ≤ ~12 chars from the alphabet "0-9A-Za-z-+ "). JS8Call will
# segment longer messages into multiple frames automatically; capture
# 16 s is enough for one frame either way.
MESSAGES=(
    "cq|CQ K1FM"
    "cq|CQ K1FM EM85"
    "cq|CQ DX K1FM"
    "directed|@KN4CRD HI"
    "directed|@W1AW 73"
    "directed|@VK3ACF GD"
    "heartbeat|@HB AUTO"
    "compound|K1FM/QRP"
)

count=${#MESSAGES[@]}
echo
echo "──────────────────────────────────────────────────────────────────"
echo " Phase 1 Loop C — JS8 corpus capture, $count transmissions"
echo "──────────────────────────────────────────────────────────────────"
echo
echo "For each message:"
echo "  1. Copy the EXACT text into JS8Call's text box."
echo "  2. Click Send (or press Enter inside JS8Call)."
echo "  3. WAIT until JS8Call's TX indicator lights up (red, top-left)."
echo "     JS8 starts on 15-s slot boundaries — the wait can be up to 15 s."
echo "  4. The MOMENT TX starts, press Enter HERE — we'll capture 16 s."
echo
echo "If you mistype or want to re-do an entry, Ctrl-C and rerun;"
echo "saved files will be overwritten on a re-run."
echo

for i in $(seq 0 $((count - 1))); do
    entry="${MESSAGES[$i]}"
    type="${entry%%|*}"
    msg="${entry#*|}"
    n=$(printf "%02d" "$i")
    out_base="$OUTDIR/${type}_${n}"

    echo "[$((i + 1))/$count]  type \"${msg}\"  (frame: ${type})"
    read -r -p "         Press Enter the moment JS8Call's TX lights up: "

    echo "         capturing 16 s ..."
    ffmpeg -nostdin -loglevel error \
        -f pulse -i "$DEVICE" -t 16 \
        -ar 12000 -ac 1 -c:a pcm_s16le \
        -y "${out_base}.wav"

    # Tiny JSON metadata. Quote the message in case it contains weird chars.
    python3 -c "
import json
json.dump({'text': '''${msg//\'/\\\'}''', 'frame_type': '${type}'},
          open('${out_base}.json','w'))
"

    sz=$(stat -c%s "${out_base}.wav")
    echo "         saved: ${out_base}.wav (${sz} bytes)"
    echo
done

echo "──────────────────────────────────────────────────────────────────"
echo " Capture done. ${count} WAVs + JSON pairs in:"
echo "   $OUTDIR/"
echo
echo " Verify the corpus against the wasm decoder:"
echo "   node tools/test-js8-corpus.mjs"
echo "──────────────────────────────────────────────────────────────────"
