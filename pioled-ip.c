/*
 * pioled-ip.c — show hostname + IP address on an Adafruit PiOLED
 * (SSD1306 128x32, I2C address 0x3C on /dev/i2c-1).
 *
 * No libraries beyond libc and the kernel's i2c-dev interface.
 * On Raspberry Pi OS:   sudo apt install build-essential
 * Build:                gcc -O2 -Wall -o pioled-ip pioled-ip.c
 * Run:                  ./pioled-ip          (user must be in the 'i2c' group)
 *
 * The font is a 5x7 bitmap embedded below: uppercase letters, digits,
 * and . : - / (lowercase input is upcased before drawing).
 */

#include <arpa/inet.h>
#include <ctype.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <linux/i2c-dev.h>
#include <net/if.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define I2C_DEV   "/dev/i2c-1"
#define I2C_ADDR  0x3C
#define OLED_W    128
#define OLED_H    32
#define PAGES     (OLED_H / 8)          /* 4 pages of 8 vertical pixels */
#define CHARS_PER_LINE (OLED_W / 6)     /* 5px glyph + 1px gap = 21 chars */

static uint8_t fb[OLED_W * PAGES];      /* framebuffer, column-major pages */
static volatile sig_atomic_t running = 1;

/* ---- 5x7 font: one glyph = 5 column bytes, bit0 = top pixel ---- */
static const char font_index[] = " .:-/0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ?";
static const uint8_t font[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00},  /* space */
    {0x00, 0x60, 0x60, 0x00, 0x00},  /* . */
    {0x00, 0x36, 0x36, 0x00, 0x00},  /* : */
    {0x08, 0x08, 0x08, 0x08, 0x08},  /* - */
    {0x60, 0x10, 0x08, 0x04, 0x03},  /* / */
    {0x3E, 0x51, 0x49, 0x45, 0x3E},  /* 0 */
    {0x00, 0x42, 0x7F, 0x40, 0x00},  /* 1 */
    {0x42, 0x61, 0x51, 0x49, 0x46},  /* 2 */
    {0x21, 0x41, 0x45, 0x4B, 0x31},  /* 3 */
    {0x18, 0x14, 0x12, 0x7F, 0x10},  /* 4 */
    {0x27, 0x45, 0x45, 0x45, 0x39},  /* 5 */
    {0x3C, 0x4A, 0x49, 0x49, 0x30},  /* 6 */
    {0x01, 0x71, 0x09, 0x05, 0x03},  /* 7 */
    {0x36, 0x49, 0x49, 0x49, 0x36},  /* 8 */
    {0x06, 0x49, 0x49, 0x29, 0x1E},  /* 9 */
    {0x7E, 0x09, 0x09, 0x09, 0x7E},  /* A */
    {0x7F, 0x49, 0x49, 0x49, 0x36},  /* B */
    {0x3E, 0x41, 0x41, 0x41, 0x22},  /* C */
    {0x7F, 0x41, 0x41, 0x41, 0x3E},  /* D */
    {0x7F, 0x49, 0x49, 0x49, 0x41},  /* E */
    {0x7F, 0x09, 0x09, 0x09, 0x01},  /* F */
    {0x3E, 0x41, 0x49, 0x49, 0x3A},  /* G */
    {0x7F, 0x08, 0x08, 0x08, 0x7F},  /* H */
    {0x00, 0x41, 0x7F, 0x41, 0x00},  /* I */
    {0x20, 0x40, 0x41, 0x3F, 0x01},  /* J */
    {0x7F, 0x08, 0x14, 0x22, 0x41},  /* K */
    {0x7F, 0x40, 0x40, 0x40, 0x40},  /* L */
    {0x7F, 0x02, 0x0C, 0x02, 0x7F},  /* M */
    {0x7F, 0x02, 0x04, 0x08, 0x7F},  /* N */
    {0x3E, 0x41, 0x41, 0x41, 0x3E},  /* O */
    {0x7F, 0x09, 0x09, 0x09, 0x06},  /* P */
    {0x3E, 0x41, 0x51, 0x21, 0x5E},  /* Q */
    {0x7F, 0x09, 0x19, 0x29, 0x46},  /* R */
    {0x46, 0x49, 0x49, 0x49, 0x31},  /* S */
    {0x01, 0x01, 0x7F, 0x01, 0x01},  /* T */
    {0x3F, 0x40, 0x40, 0x40, 0x3F},  /* U */
    {0x1F, 0x20, 0x40, 0x20, 0x1F},  /* V */
    {0x7F, 0x20, 0x18, 0x20, 0x7F},  /* W */
    {0x63, 0x14, 0x08, 0x14, 0x63},  /* X */
    {0x03, 0x04, 0x78, 0x04, 0x03},  /* Y */
    {0x61, 0x51, 0x49, 0x45, 0x43},  /* Z */
    {0x02, 0x01, 0x51, 0x09, 0x06},  /* ? */
};

static const uint8_t *glyph(char c)
{
    const char *p = strchr(font_index, toupper((unsigned char)c));
    if (!p)
        p = strchr(font_index, '?');
    return font[p - font_index];
}

/* ---- I2C transport: control byte 0x00 = commands, 0x40 = data ---- */

static int oled_cmds(int fd, const uint8_t *cmds, size_t n)
{
    uint8_t buf[32];
    buf[0] = 0x00;
    memcpy(buf + 1, cmds, n);
    if (write(fd, buf, n + 1) != (ssize_t)(n + 1)) {
        perror("i2c command write");
        return -1;
    }
    return 0;
}

static int oled_init(int fd)
{
    static const uint8_t init[] = {
        0xAE,        /* display off                    */
        0xD5, 0x80,  /* clock divide ratio             */
        0xA8, 0x1F,  /* multiplex ratio: 32 rows       */
        0xD3, 0x00,  /* display offset 0               */
        0x40,        /* start line 0                   */
        0x8D, 0x14,  /* charge pump on (internal Vcc)  */
        0x20, 0x00,  /* horizontal addressing mode     */
        0xA1,        /* segment remap (flip X)         */
        0xC8,        /* COM scan reversed (flip Y)     */
        0xDA, 0x02,  /* COM pins: sequential (128x32!) */
        0x81, 0x8F,  /* contrast                       */
        0xD9, 0xF1,  /* precharge                      */
        0xDB, 0x40,  /* VCOMH deselect                 */
        0xA4,        /* resume from RAM                */
        0xA6,        /* normal (not inverted)          */
        0xAF,        /* display on                     */
    };
    return oled_cmds(fd, init, sizeof(init));
}

static int oled_flush(int fd)
{
    static const uint8_t window[] = {
        0x21, 0x00, OLED_W - 1,  /* column range  */
        0x22, 0x00, PAGES - 1,   /* page range    */
    };
    if (oled_cmds(fd, window, sizeof(window)) < 0)
        return -1;

    uint8_t buf[1 + 64];
    for (size_t off = 0; off < sizeof(fb); off += 64) {
        size_t n = sizeof(fb) - off < 64 ? sizeof(fb) - off : 64;
        buf[0] = 0x40;
        memcpy(buf + 1, fb + off, n);
        if (write(fd, buf, n + 1) != (ssize_t)(n + 1)) {
            perror("i2c data write");
            return -1;
        }
    }
    return 0;
}

/* ---- drawing ---- */

static void draw_string(int page, const char *s)
{
    int col = 0;
    for (; *s && col + 6 <= OLED_W; s++, col += 6)
        memcpy(&fb[page * OLED_W + col], glyph(*s), 5);
}

/* ---- find the first usable IPv4 address ----
 * Prefers a routable address; a 169.254.x.x link-local (what dhcpcd
 * self-assigns after DHCP timeout) is kept only as a fallback. */

static void primary_ip(char *ip, size_t iplen, char *ifname, size_t iflen)
{
    struct ifaddrs *addrs, *a;
    int have_linklocal = 0;

    snprintf(ip, iplen, "NO IP YET");
    snprintf(ifname, iflen, "-");

    if (getifaddrs(&addrs) != 0)
        return;

    for (a = addrs; a; a = a->ifa_next) {
        if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET)
            continue;
        if ((a->ifa_flags & IFF_LOOPBACK) || !(a->ifa_flags & IFF_UP))
            continue;
        struct sockaddr_in *sin = (struct sockaddr_in *)a->ifa_addr;
        uint32_t addr = ntohl(sin->sin_addr.s_addr);
        int linklocal = (addr & 0xFFFF0000u) == 0xA9FE0000u;  /* 169.254/16 */
        if (linklocal && have_linklocal)
            continue;
        inet_ntop(AF_INET, &sin->sin_addr, ip, iplen);
        snprintf(ifname, iflen, "%s", a->ifa_name);
        if (!linklocal)
            break;                    /* routable address wins outright */
        have_linklocal = 1;           /* else keep looking for better  */
    }
    freeifaddrs(addrs);
}

static void handle_signal(int sig)
{
    (void)sig;
    running = 0;
}

int main(void)
{
    int fd = open(I2C_DEV, O_RDWR);
    if (fd < 0) {
        perror("open " I2C_DEV);
        fprintf(stderr, "Is I2C enabled? (sudo raspi-config -> Interface Options)\n");
        return 1;
    }
    if (ioctl(fd, I2C_SLAVE, I2C_ADDR) < 0) {
        perror("ioctl I2C_SLAVE");
        return 1;
    }
    if (oled_init(fd) < 0)
        return 1;

    struct sigaction sa = { .sa_handler = handle_signal };
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    char hostname[64] = "?";
    gethostname(hostname, sizeof(hostname) - 1);

    int blink = 0;
    while (running) {
        char ip[INET_ADDRSTRLEN + 8], ifname[IF_NAMESIZE], ifline[IF_NAMESIZE + 2];

        primary_ip(ip, sizeof(ip), ifname, sizeof(ifname));
        snprintf(ifline, sizeof(ifline), "%s:", ifname);

        memset(fb, 0, sizeof(fb));                  /* draw_string clips at 21 chars */
        draw_string(0, hostname);                   /* page 0: hostname   */
        draw_string(2, ifline);                     /* page 2: interface  */
        draw_string(3, ip);                         /* page 3: IP address */

        /* heartbeat: dot in the last cell of line 0, on/off once per second */
        memset(&fb[0 * OLED_W + 120], 0, 6);        /* reserve cell 21 for the dot */
        if (blink)
            memcpy(&fb[0 * OLED_W + 120], glyph('.'), 5);
        blink ^= 1;

        if (oled_flush(fd) < 0)
            break;

        struct timespec half_sec = { .tv_sec = 0, .tv_nsec = 500 * 1000 * 1000 };
        nanosleep(&half_sec, NULL);                 /* 2 Hz; a signal wakes it early */
    }

    /* blank + power the panel down on exit so pixels don't burn in */
    uint8_t off = 0xAE;
    oled_cmds(fd, &off, 1);
    close(fd);
    return 0;
}
