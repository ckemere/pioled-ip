/*
 * render-test.c — compile the real program with its entry point renamed,
 * draw a known screen using the actual font + draw_string code, and print
 * the framebuffer as ASCII art. CI diffs this against expected.txt, so any
 * accidental change to the font table or drawing logic fails the build.
 */
#define main pioled_unused_main
#include "../pioled-ip.c"
#undef main

int main(void)
{
    memset(fb, 0, sizeof(fb));
    draw_string(0, "raspberrypi");
    draw_string(2, "wlan0:");
    draw_string(3, "192.168.100.254");
    memcpy(&fb[0 * OLED_W + 120], glyph('.'), 5);   /* heartbeat ON frame */

    for (int y = 0; y < OLED_H; y++) {
        for (int x = 0; x < OLED_W; x++)
            putchar((fb[(y / 8) * OLED_W + x] >> (y % 8)) & 1 ? '#' : '.');
        putchar('\n');
    }
    return 0;
}
