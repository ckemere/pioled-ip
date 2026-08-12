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

Flash Raspberry Pi OS as usual, then before ejecting:

    sudo ./inject-pioled.sh /dev/sdX ./pioled-ip-arm64

Also works on .img files (loop-mounted automatically). Installs the binary,
enables the service, turns on I2C in config.txt, and adds the i2c-dev
module — the display works from the very first boot.

## Tests

test/render-test.c compiles the real drawing code and diffs the framebuffer
output against test/expected.txt. Run locally:

    gcc -O2 -Wall -o render-test test/render-test.c && ./render-test | diff - test/expected.txt
