# pioled-ip

Shows the Raspberry Pi's hostname, network interface, and IP address on an
[Adafruit PiOLED](https://www.adafruit.com/product/3527) 
(SSD1306 128x32, I2C). Single C file, no dependencies beyond
libc and the kernel's i2c-dev interface. A blinking dot in the top-right
corner shows the program is alive; before DHCP completes it reads NO IP YET.

## Build on the Pi

    sudo apt install build-essential
    gcc -O2 -Wall -o pioled-ip pioled-ip.c
    ./pioled-ip

## Cross-compile (or grab a release binary)

    sudo apt install gcc-aarch64-linux-gnu        # 64-bit Pi OS
    aarch64-linux-gnu-gcc -O2 -Wall -static -o pioled-ip-arm64 pioled-ip.c

    sudo apt install gcc-arm-linux-gnueabihf      # 32-bit Pi OS
    arm-linux-gnueabihf-gcc -O2 -Wall -static -o pioled-ip-armhf pioled-ip.c

CI builds both on every push; tagged releases (v*) have them attached.

## Run at boot

    sudo cp pioled-ip /usr/local/bin/
    sudo cp pioled-ip.service /etc/systemd/system/
    sudo systemctl enable --now pioled-ip

## Bake into an SD card at imaging time

Three options; all give a display that works from the very first boot
(binary installed, service enabled, I2C on, i2c-dev module loaded).

### Option A: modify locally (Linux)

Flash Raspberry Pi OS as usual, then before ejecting:

    sudo ./inject-pioled.sh /dev/sdX ./pioled-ip-arm64

Also works on .img files (loop-mounted automatically), so you can prepare
a golden image once and flash it repeatedly.

### Option B: modify locally (macOS, via Docker)

macOS cannot mount the ext4 root partition, so on a Mac the injection runs
inside a small Linux container against the .img file (needs Docker Desktop):

    xz -dk 2026-xx-xx-raspios-bookworm-arm64.img.xz
    ./inject-pioled-mac.sh 2026-xx-xx-raspios-bookworm-arm64.img ./pioled-ip-arm64

Then flash the modified .img with Raspberry Pi Imager ("Use custom"). Keep
inject-pioled-mac.sh next to inject-pioled.sh and pioled-ip.service — all
three are attached to every release.

### Option C: download a pre-built golden image

Every tagged release includes ready-to-flash images with pioled-ip already
injected into official Raspberry Pi OS Lite:

    pioled-raspios-lite-arm64.img.xz   (Pi 3/4/5, Zero 2)
    pioled-raspios-lite-armhf.img.xz   (Pi Zero/1, 32-bit)

Flash directly with Raspberry Pi Imager ("Use custom" accepts .img.xz) or
any other tool. A .sha256 file is published alongside each image.

Note: Imager does not offer its OS customisation step (hostname, Wi-Fi,
user, SSH) for images picked via "Use custom" — it needs repository
metadata to know how. Each release therefore also publishes imager.json.
Point Imager at it once (Options -> Content Repository -> custom URL):

    https://github.com/ckemere/pioled-ip/releases/latest/download/imager.json

and the pioled images appear in Imager's OS list as regular entries, with the
full customisation flow available.

## Tests

test/render-test.c compiles the real drawing code and diffs the framebuffer
output against test/expected.txt. Run locally:

    gcc -O2 -Wall -o render-test test/render-test.c && ./render-test | diff - test/expected.txt
