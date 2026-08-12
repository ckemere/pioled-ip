#!/bin/bash
#
# inject-pioled-mac.sh — macOS wrapper around inject-pioled.sh.
#
# macOS can't mount the ext4 root partition of a Raspberry Pi OS card, so
# on a Mac we modify the .img FILE inside a small Linux container instead,
# and you then flash the modified image with Raspberry Pi Imager ("Use
# custom") or dd. Requires Docker Desktop.
#
#   ./inject-pioled-mac.sh 2026-xx-xx-raspios-bookworm-arm64.img ./pioled-ip-arm64
#
# Keep this next to inject-pioled.sh and pioled-ip.service.

set -euo pipefail

die() { echo "error: $*" >&2; exit 1; }

[ $# -eq 2 ] || die "usage: $0 <raspios.img> <arm-binary>"
IMG=$1
BIN=$2

case "$IMG" in
    *.xz) die "image is compressed — decompress it first:  xz -dk '$IMG'" ;;
esac
[ -f "$IMG" ] || die "image $IMG not found"
[ -f "$BIN" ] || die "binary $BIN not found"
command -v docker >/dev/null 2>&1 || die "Docker Desktop is required on macOS"

IMG_DIR=$(cd "$(dirname "$IMG")" && pwd); IMG_NAME=$(basename "$IMG")
BIN_DIR=$(cd "$(dirname "$BIN")" && pwd); BIN_NAME=$(basename "$BIN")
TOOL_DIR=$(cd "$(dirname "$0")" && pwd)

[ -f "$TOOL_DIR/inject-pioled.sh" ]   || die "inject-pioled.sh must sit next to this script"
[ -f "$TOOL_DIR/pioled-ip.service" ]  || die "pioled-ip.service must sit next to this script"

# --privileged is needed for loop devices and mounting inside the container.
docker run --rm --privileged \
    -v "$IMG_DIR":/img \
    -v "$BIN_DIR":/bin-src:ro \
    -v "$TOOL_DIR":/tool:ro \
    debian:stable-slim \
    bash -c "apt-get update -qq >/dev/null && \
             apt-get install -y -qq file >/dev/null && \
             /tool/inject-pioled.sh /img/$IMG_NAME /bin-src/$BIN_NAME"

echo
echo "Image modified in place: $IMG"
echo "Flash it with Raspberry Pi Imager -> 'Use custom', or:"
echo "  diskutil unmountDisk /dev/diskN && sudo dd if=$IMG_NAME of=/dev/rdiskN bs=4m"
