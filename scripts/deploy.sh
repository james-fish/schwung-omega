#!/usr/bin/env bash
# deploy.sh — atomic deploy of build/dsp.so to the Move device (D-14).
#
# Uploads to dsp.so.new then renames over the live dsp.so on-device, so a
# partially-transferred file is never loaded. Override the device host/path via
# env vars before running:
#   OMEGA_DEVICE_HOST=ableton@move.local \
#   OMEGA_DEVICE_DIR=/data/UserData/schwung/modules/omega \
#   ./scripts/deploy.sh
set -euo pipefail

DEVICE_HOST="${OMEGA_DEVICE_HOST:-ableton@move.local}"
DEVICE_DIR="${OMEGA_DEVICE_DIR:-/data/UserData/schwung/modules/omega}"
SO="build/dsp.so"

if [ ! -f "$SO" ]; then
    echo "deploy: $SO not found — run 'make dsp.so' first" >&2
    exit 1
fi

echo "deploy: uploading $SO -> $DEVICE_HOST:$DEVICE_DIR/dsp.so.new"
scp "$SO" "$DEVICE_HOST:$DEVICE_DIR/dsp.so.new"

echo "deploy: atomic rename dsp.so.new -> dsp.so on device"
ssh "$DEVICE_HOST" "mv '$DEVICE_DIR/dsp.so.new' '$DEVICE_DIR/dsp.so'"

echo "deploy: done"
