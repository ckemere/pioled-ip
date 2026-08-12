#!/bin/bash
#
# inject-pioled.sh — bake pioled-ip into a freshly flashed Raspberry Pi OS
# SD card or .img file, so the display works from the very first boot.
#
#   sudo ./inject-pioled.sh /dev/sdX  ./pioled-ip     # a flashed SD card
#   sudo ./inject-pioled.sh raspios.img ./pioled-ip   # an image file (flash after)
#
# What it does to the target:
#   rootfs: /usr/local/bin/pioled-ip                     (the ARM binary)
#           /etc/systemd/system/pioled-ip.service        (the unit)
#           /etc/systemd/system/multi-user.target.wants/ (enable = symlink)
#           /etc/modules                                 (+ i2c-dev)
#   boot:   config.txt                                   (dtparam=i2c_arm=on)
#
# The unit file is expected next to this script as pioled-ip.service.

set -euo pipefail

die() { echo "error: $*" >&2; exit 1; }

[ "$(id -u)" -eq 0 ] || die "run with sudo (mounting partitions needs root)"
[ $# -eq 2 ] || die "usage: $0 <device-or-image> <pioled-ip-binary>"

TARGET=$1
BINARY=$2
UNIT="$(dirname "$0")/pioled-ip.service"

[ -e "$TARGET" ] || die "$TARGET not found"
[ -f "$BINARY" ] || die "binary $BINARY not found"
[ -f "$UNIT" ]   || die "unit file $UNIT not found (keep it next to this script)"

# sanity: the binary must be an ARM executable, not one from your desktop
case "$(file -b "$BINARY")" in
    *ARM*|*aarch64*) ;;
    *) die "$BINARY is not an ARM binary — cross-compile it first, e.g.
       sudo apt install gcc-aarch64-linux-gnu
       aarch64-linux-gnu-gcc -O2 -Wall -static -o pioled-ip pioled-ip.c" ;;
esac

# --- attach image files to a loop device; use block devices as-is ----------
LOOPDEV=""
cleanup() {
    set +e
    [ -n "${ROOT_MNT:-}" ] && mountpoint -q "$ROOT_MNT" && umount "$ROOT_MNT"
    [ -n "${BOOT_MNT:-}" ] && mountpoint -q "$BOOT_MNT" && umount "$BOOT_MNT"
    [ -n "$LOOPDEV" ] && losetup -d "$LOOPDEV"
    [ -n "${WORK:-}" ] && rmdir "$WORK/boot" "$WORK/root" "$WORK" 2>/dev/null
}
trap cleanup EXIT

if [ -b "$TARGET" ]; then
    DEV=$TARGET
else
    LOOPDEV=$(losetup -fP --show "$TARGET")   # -P scans the partition table
    DEV=$LOOPDEV
    # some environments (containers, WSL) don't create the p1/p2 nodes; partx does
    [ -b "${DEV}p1" ] || partx -a "$DEV" 2>/dev/null || true
fi

# partition names differ: /dev/sdb -> sdb1/sdb2, /dev/loop0|mmcblk0 -> p1/p2
if [ -b "${DEV}p1" ]; then
    BOOT_PART=${DEV}p1 ROOT_PART=${DEV}p2
elif [ -b "${DEV}1" ]; then
    BOOT_PART=${DEV}1 ROOT_PART=${DEV}2
else
    die "cannot find partitions on $DEV — is this a flashed Raspberry Pi OS card?"
fi

WORK=$(mktemp -d)
BOOT_MNT=$WORK/boot ROOT_MNT=$WORK/root
mkdir -p "$BOOT_MNT" "$ROOT_MNT"
mount "$BOOT_PART" "$BOOT_MNT"
mount "$ROOT_PART" "$ROOT_MNT"

[ -d "$ROOT_MNT/etc/systemd/system" ] || die "$ROOT_PART doesn't look like a Linux rootfs"

# --- rootfs: binary + unit + enable symlink --------------------------------
install -m 755 "$BINARY" "$ROOT_MNT/usr/local/bin/pioled-ip"
install -m 644 "$UNIT"   "$ROOT_MNT/etc/systemd/system/pioled-ip.service"
mkdir -p "$ROOT_MNT/etc/systemd/system/multi-user.target.wants"
ln -sf /etc/systemd/system/pioled-ip.service \
       "$ROOT_MNT/etc/systemd/system/multi-user.target.wants/pioled-ip.service"

# --- rootfs: make sure the i2c-dev module loads at boot --------------------
if ! grep -q '^i2c-dev' "$ROOT_MNT/etc/modules" 2>/dev/null; then
    echo i2c-dev >> "$ROOT_MNT/etc/modules"
fi

# --- boot partition: enable the I2C bus in config.txt ----------------------
CONFIG=$BOOT_MNT/config.txt
[ -f "$CONFIG" ] || die "no config.txt on the boot partition"
if grep -q '^#dtparam=i2c_arm=on' "$CONFIG"; then
    sed -i 's/^#dtparam=i2c_arm=on/dtparam=i2c_arm=on/' "$CONFIG"
elif ! grep -q '^dtparam=i2c_arm=on' "$CONFIG"; then
    printf '\ndtparam=i2c_arm=on\n' >> "$CONFIG"
fi

sync
echo "done: pioled-ip installed and enabled, I2C switched on."
echo "The display will come up on first boot (NO IP YET until DHCP finishes)."
