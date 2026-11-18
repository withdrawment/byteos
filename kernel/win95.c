#include "kernel.h"
#include "site_data.h"
#include "font8x16.h"
#include <stdarg.h>

#define GW 640
#define GH 480
#define TASKBAR_Y 450
#define MAX_WIN 4

#define G_BLACK   0
#define G_NAVY    1
#define G_GREEN   2
#define G_TEAL    3
#define G_MAROON  4
#define G_PURPLE  5
#define G_OLIVE   6
#define G_SILVER  7
#define G_GRAY    8
#define G_BLUE    9
#define G_LIME    10
#define G_CYAN    11
#define G_RED     12
#define G_MAGENTA 13
#define G_YELLOW  14
#define G_WHITE   15

enum {
    APP_FILES,
    APP_NOTEPAD,
    APP_CONSOLE,
    APP_SYSPROP,
    APP_ABOUT
};

enum {
    ACT_FILES,
    ACT_NOTEPAD,
    ACT_CONSOLE,
    ACT_SYSPROP,
    ACT_ABOUT,
    ACT_SEP,
    ACT_BROWSER,
    ACT_EDITOR,
    ACT_TERMINAL,
    ACT_FETCH,
    ACT_DONUT,
    ACT_EXIT
};

typedef struct {
    const char *label;
    int action;
} start_item_t;

static const start_item_t g_start_items[] = {
    { "My Computer", ACT_FILES },
    { "Notepad", ACT_NOTEPAD },
    { "Command Prompt", ACT_CONSOLE },
    { "System Properties", ACT_SYSPROP },
    { "About ByteWin 95", ACT_ABOUT },
    { "", ACT_SEP },
    { "Browser", ACT_BROWSER },
    { "HTML Editor", ACT_EDITOR },
    { "Terminal", ACT_TERMINAL },
    { "Fetch", ACT_FETCH },
    { "Donut", ACT_DONUT },
    { "", ACT_SEP },
    { "Shut Down...", ACT_EXIT }
};

#define START_COUNT ((int)(sizeof(g_start_items) / sizeof(g_start_items[0])))

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

typedef struct {
    uint8_t misc;
    uint8_t seq[5];
    uint8_t crtc[25];
    uint8_t gfx[9];
    uint8_t attr[21];
    uint8_t pixel_mask;
} vga_regs_t;

static vga_regs_t g_regs;
static uint8_t g_saved_palette[768];
static uint16_t g_saved_text[80 * 25];
static uint8_t *g_fb = (uint8_t *)0x200000;
static uint8_t *g_plane = (uint8_t *)0x300000;
static int g_gfx_active;

static void wseq(uint8_t index, uint8_t value)
{
    outb(0x3C4, index);
    outb(0x3C5, value);
}

static void wcrtc(uint8_t index, uint8_t value)
{
    outb(0x3D4, index);
    outb(0x3D5, value);
}

static void wgfx(uint8_t index, uint8_t value)
{
    outb(0x3CE, index);
    outb(0x3CF, value);
}

static void save_vga(void)
{
    g_regs.misc = inb(0x3CC);

    for (int i = 0; i < 5; i++) {
        outb(0x3C4, (uint8_t)i);
        g_regs.seq[i] = inb(0x3C5);
    }
    for (int i = 0; i < 25; i++) {
        outb(0x3D4, (uint8_t)i);
        g_regs.crtc[i] = inb(0x3D5);
    }
    for (int i = 0; i < 9; i++) {
        outb(0x3CE, (uint8_t)i);
        g_regs.gfx[i] = inb(0x3CF);
    }

    (void)inb(0x3DA);
    for (int i = 0; i < 21; i++) {
        outb(0x3C0, (uint8_t)i);
        g_regs.attr[i] = inb(0x3C1);
    }
    (void)inb(0x3DA);

    outb(0x3C7, 0);
    for (int i = 0; i < 768; i++)
        g_saved_palette[i] = inb(0x3C9);
    g_regs.pixel_mask = inb(0x3C6);

    memcpy(g_saved_text, (const void *)VGA_MEMORY, sizeof(g_saved_text));
}

static void gui_palette(void)
{
    static const uint8_t pal[16][3] = {
        { 0, 0, 0 }, { 0, 0, 32 }, { 0, 32, 0 }, { 0, 32, 32 },
        { 32, 0, 0 }, { 32, 0, 32 }, { 32, 32, 0 }, { 48, 48, 48 },
        { 32, 32, 32 }, { 0, 0, 63 }, { 0, 63, 0 }, { 0, 63, 63 },
        { 63, 0, 0 }, { 63, 0, 63 }, { 63, 63, 0 }, { 63, 63, 63 }
    };
    outb(0x3C8, 0);
    for (int i = 0; i < 16; i++) {
        for (int c = 0; c < 3; c++)
            outb(0x3C9, pal[i][c]);
    }
}

static void set_mode12(void)
{
    static const uint8_t crtc[25] = {
        0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0x0B, 0x3E,
        0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xEA, 0x0C, 0xDF, 0x28, 0x00, 0xE7, 0x04, 0xE3, 0x11
    };
    static const uint8_t attr[21] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
        0x01, 0x00, 0x0F, 0x00, 0x00
    };

    outb(0x3C2, 0xE3);
    wseq(0, 0x01);
    wseq(1, 0x01);
    wseq(2, 0x0F);
    wseq(3, 0x00);
    wseq(4, 0x06);

    wcrtc(0x11, (uint8_t)(g_regs.crtc[0x11] & 0x7F));
    for (int i = 0; i < 25; i++) {
        if (i != 0x11)
            wcrtc((uint8_t)i, crtc[i]);
    }
    wcrtc(0x11, crtc[0x11]);

    wgfx(0, 0x00);
    wgfx(1, 0x00);
    wgfx(2, 0x00);
    wgfx(3, 0x00);
    wgfx(4, 0x00);
    wgfx(5, 0x00);
    wgfx(6, 0x05);
    wgfx(7, 0x0F);
    wgfx(8, 0xFF);

    (void)inb(0x3DA);
    for (int i = 0; i < 21; i++) {
        outb(0x3C0, (uint8_t)i);
        outb(0x3C0, attr[i]);
    }
    outb(0x3C0, 0x20);
    wseq(0, 0x03);

    wseq(2, 0x0F);
    memset((void *)0xA0000, 0, 65536);
}

static void restore_vga(void)
{
    outb(0x3C2, g_regs.misc);
    wseq(0, 0x01);
    for (int i = 1; i < 5; i++)
        wseq((uint8_t)i, g_regs.seq[i]);

    wcrtc(0x11, (uint8_t)(g_regs.crtc[0x11] & 0x7F));
    for (int i = 0; i < 25; i++) {
        if (i != 0x11)
            wcrtc((uint8_t)i, g_regs.crtc[i]);
    }
    wcrtc(0x11, g_regs.crtc[0x11]);

    for (int i = 0; i < 9; i++)
        wgfx((uint8_t)i, g_regs.gfx[i]);

    uint8_t gfx5 = g_regs.gfx[5];
    uint8_t gfx6 = g_regs.gfx[6];
    uint8_t *font = (uint8_t *)0xA0000;
    wgfx(5, 0x00);
    wgfx(6, 0x05);
    wseq(2, 0x04);
    wseq(4, 0x06);
    for (int ch = 0; ch < 256; ch++) {
        memcpy(font + ch * 32, &g_font8x16[ch * 16], 16);
        memset(font + ch * 32 + 16, 0, 16);
    }
    wseq(2, g_regs.seq[2]);
    wseq(4, g_regs.seq[4]);
    wgfx(5, gfx5);
    wgfx(6, gfx6);

    memcpy((void *)VGA_MEMORY, g_saved_text, sizeof(g_saved_text));

    outb(0x3C8, 0);
    for (int i = 0; i < 768; i++)
        outb(0x3C9, g_saved_palette[i]);
    outb(0x3C6, g_regs.pixel_mask);

    (void)inb(0x3DA);
    for (int i = 0; i < 21; i++) {
        outb(0x3C0, (uint8_t)i);
        outb(0x3C0, g_regs.attr[i]);
    }
    outb(0x3C0, 0x20);

    wseq(0, 0x03);
}

static int vga_enter_graphics(void)
{
    if (g_gfx_active)
        return 0;

    save_vga();

    set_mode12();
    gui_palette();
    memset(g_fb, 0, GW * GH);
    g_gfx_active = 1;
    return 0;
}

static void vga_leave_graphics(void)
{
    if (!g_gfx_active)
        return;
    restore_vga();
    g_gfx_active = 0;
}

static void fb_px(int x, int y, uint8_t color)
{
    if (x < 0 || x >= GW || y < 0 || y >= GH)
        return;
    g_fb[y * GW + x] = color;
}

static void fb_fill(int x, int y, int w, int h, uint8_t color)
{
    if (w <= 0 || h <= 0)
        return;
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > GW)
        w = GW - x;
    if (y + h > GH)
        h = GH - y;
    if (w <= 0 || h <= 0)
        return;
    for (int row = 0; row < h; row++)
        memset(&g_fb[(y + row) * GW + x], color, (size_t)w);
}

static void fb_hline(int x, int y, int w, uint8_t color)
{
    fb_fill(x, y, w, 1, color);
}

static void fb_vline(int x, int y, int h, uint8_t color)
{
    fb_fill(x, y, 1, h, color);
}

static void fb_bevel(int x, int y, int w, int h, int raised)
{
    uint8_t tl = raised ? G_WHITE : G_GRAY;
    uint8_t br = raised ? G_GRAY : G_WHITE;
    fb_hline(x, y, w, tl);
    fb_vline(x, y, h, tl);
    fb_hline(x, y + h - 1, w, br);
    fb_vline(x + w - 1, y, h, br);
}

static void fb_char(int x, int y, unsigned char ch, uint8_t fg, int bg)
{
    if (x <= -8 || x >= GW || y <= -16 || y >= GH)
        return;
    const uint8_t *glyph = &g_font8x16[(unsigned)ch * 16];

    for (int row = 0; row < 16; row++) {
        int py = y + row;
        if (py < 0 || py >= GH)
            continue;
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            int px = x + col;
            if (px < 0)
                continue;
            if (px >= GW)
                break;
            if (bits & (0x80 >> col))
                g_fb[py * GW + px] = fg;
            else if (bg >= 0)
                g_fb[py * GW + px] = (uint8_t)bg;
        }
    }
}

static void fb_text(int x, int y, const char *s, uint8_t fg, int bg)
{
    while (*s) {
        if (x >= GW)
            break;
        fb_char(x, y, (unsigned char)*s, fg, bg);
        x += 8;
        s++;
    }
}

static void fb_text_center(int x, int y, int w, const char *s,
                           uint8_t fg, int bg)
{
    int len = (int)kstrlen(s);
    fb_text(x + (w - len * 8) / 2, y, s, fg, bg);
}

static void format_buf(char *out, size_t cap, const char *fmt, ...)
{
    va_list ap;
    size_t pos = 0;
    char tmp[32];

    va_start(ap, fmt);
    while (*fmt && pos + 1 < cap) {
        if (*fmt != '%') {
            out[pos++] = *fmt++;
            continue;
        }
        fmt++;
        if (*fmt == '%') {
            out[pos++] = '%';
            fmt++;
            continue;
        }

        char pad = ' ';
        int width = 0;
        int longs = 0;
        if (*fmt == '0') {
            pad = '0';
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }
        while (*fmt == 'l') {
            longs++;
            fmt++;
        }
        char spec = *fmt;
        if (spec)
            fmt++;

        int n = 0;
        if (spec == 's') {
            const char *s = va_arg(ap, const char *);
            if (!s)
                s = "(null)";
            while (*s && pos + 1 < cap)
                out[pos++] = *s++;
            continue;
        } else if (spec == 'c') {
            tmp[n++] = (char)va_arg(ap, int);
        } else {
            uint64_t value;
            if (spec == 'd' || spec == 'i') {
                int64_t sv;
                if (longs == 0)
                    sv = va_arg(ap, int);
                else if (longs == 1)
                    sv = va_arg(ap, long);
                else
                    sv = va_arg(ap, long long);
                if (sv < 0) {
                    tmp[n++] = '-';
                    value = (uint64_t)(-(sv + 1)) + 1u;
                } else {
                    value = (uint64_t)sv;
                }
            } else if (longs == 0) {
                value = va_arg(ap, unsigned int);
            } else if (longs == 1) {
                value = va_arg(ap, unsigned long);
            } else {
                value = va_arg(ap, unsigned long long);
            }

            unsigned base = (spec == 'x' || spec == 'X') ? 16u : 10u;
            char digits[20];
            int count = 0;
            if (value == 0)
                digits[count++] = '0';
            while (value) {
                unsigned d = (unsigned)(value % base);
                digits[count++] = (char)(d < 10 ? '0' + d
                                              : (spec == 'X' ? 'A' : 'a') + d - 10);
                value /= base;
            }
            int start = n;
            if (pad == ' ')
                while (n + count < width)
                    tmp[n++] = ' ';
            while (count > 0)
                tmp[n++] = digits[--count];
            if (pad == '0') {
                int digits_len = n - start;
                while (digits_len < width) {
                    for (int k = n; k > start; k--)
                        tmp[k] = tmp[k - 1];
                    tmp[start] = '0';
                    n++;
                    digits_len++;
                }
            }
        }

        for (int i = 0; i < n && pos + 1 < cap; i++)
            out[pos++] = tmp[i];
    }
    va_end(ap);
    out[pos] = 0;
}

static int iabs(int v)
{
    return v < 0 ? -v : v;
}

static void present(void)
{
    for (int p = 0; p < 4; p++) {
        int si = 0;
        for (int i = 0; i < GW * GH; i += 8) {
            uint8_t b = 0;
            for (int k = 0; k < 8; k++)
                b = (uint8_t)(b | ((g_fb[i + k] >> p) & 1) << (7 - k));
            g_plane[si++] = b;
        }
        wseq(2, (uint8_t)(1 << p));
        memcpy((void *)0xA0000, g_plane, (GW * GH) / 8);
    }
    wseq(2, 0x0F);
}

static void fb_circle(int cx, int cy, int radius, uint8_t color)
{
    for (int y = -radius; y <= radius; y++) {
        for (int x = -radius; x <= radius; x++) {
            if (x * x + y * y <= radius * radius)
                fb_px(cx + x, cy + y, color);
        }
    }
}

static void fb_ring(int cx, int cy, int outer, int inner, uint8_t color)
{
    for (int y = -outer; y <= outer; y++) {
        for (int x = -outer; x <= outer; x++) {
            int d = x * x + y * y;
            if (d <= outer * outer && d >= inner * inner)
                fb_px(cx + x, cy + y, color);
        }
    }
}

#define ICON_COMPUTER 0
#define ICON_PAGE     1
#define ICON_CONSOLE  2
#define ICON_GEAR     3
#define ICON_INFO     4
#define ICON_GLOBE    5
#define ICON_EDITOR   6
#define ICON_TERM     7
#define ICON_CHART    8
#define ICON_DONUT    9
#define ICON_POWER    10
#define ICON_FOLDER   11
#define ICON_FILE     12

static void draw_big_icon(int kind, int x, int y)
{
    switch (kind) {
    case ICON_COMPUTER:
        fb_fill(x + 2, y + 4, 28, 19, G_SILVER);
        fb_bevel(x + 2, y + 4, 28, 19, 1);
        fb_fill(x + 5, y + 7, 22, 13, G_TEAL);
        fb_fill(x + 8, y + 9, 6, 1, G_WHITE);
        fb_fill(x + 8, y + 12, 12, 1, G_CYAN);
        fb_fill(x + 13, y + 23, 6, 3, G_GRAY);
        fb_fill(x + 8, y + 26, 16, 3, G_SILVER);
        fb_fill(x + 26, y + 14, 5, 15, G_SILVER);
        fb_bevel(x + 26, y + 14, 5, 15, 1);
        fb_fill(x + 27, y + 17, 3, 2, G_GREEN);
        break;
    case ICON_PAGE:
    case ICON_FILE:
        fb_fill(x + 7, y + 2, 19, 28, G_WHITE);
        fb_bevel(x + 7, y + 2, 19, 28, 0);
        fb_fill(x + 10, y + 7, 13, 1, G_GRAY);
        fb_fill(x + 10, y + 11, 13, 1, G_GRAY);
        fb_fill(x + 10, y + 15, 9, 1, G_GRAY);
        fb_fill(x + 10, y + 19, 13, 1, G_GRAY);
        break;
    case ICON_CONSOLE:
    case ICON_TERM:
        fb_fill(x + 3, y + 4, 26, 23, G_BLACK);
        fb_bevel(x + 3, y + 4, 26, 23, 0);
        fb_fill(x + 4, y + 5, 24, 3, G_SILVER);
        fb_text(x + 6, y + 10, "C:\\>", G_WHITE, G_BLACK);
        break;
    case ICON_GEAR:
        fb_fill(x + 6, y + 6, 20, 20, G_SILVER);
        fb_bevel(x + 6, y + 6, 20, 20, 1);
        fb_fill(x + 2, y + 13, 4, 6, G_SILVER);
        fb_fill(x + 26, y + 13, 4, 6, G_SILVER);
        fb_fill(x + 13, y + 2, 6, 4, G_SILVER);
        fb_fill(x + 13, y + 26, 6, 4, G_SILVER);
        fb_circle(x + 16, y + 16, 5, G_NAVY);
        break;
    case ICON_INFO:
        fb_circle(x + 16, y + 16, 12, G_NAVY);
        fb_fill(x + 15, y + 9, 3, 3, G_WHITE);
        fb_fill(x + 15, y + 14, 3, 10, G_WHITE);
        break;
    case ICON_GLOBE:
        fb_circle(x + 16, y + 16, 12, G_BLUE);
        fb_fill(x + 5, y + 13, 22, 2, G_GREEN);
        fb_fill(x + 5, y + 19, 22, 2, G_GREEN);
        fb_fill(x + 11, y + 5, 2, 22, G_CYAN);
        fb_fill(x + 19, y + 5, 2, 22, G_CYAN);
        break;
    case ICON_EDITOR:
        fb_fill(x + 5, y + 2, 19, 28, G_WHITE);
        fb_bevel(x + 5, y + 2, 19, 28, 0);
        fb_fill(x + 8, y + 7, 12, 1, G_GRAY);
        fb_fill(x + 8, y + 11, 12, 1, G_GRAY);
        fb_fill(x + 8, y + 15, 8, 1, G_GRAY);
        fb_fill(x + 20, y + 16, 4, 12, G_YELLOW);
        fb_fill(x + 20, y + 28, 4, 3, G_RED);
        break;
    case ICON_CHART:
        fb_fill(x + 5, y + 3, 22, 26, G_WHITE);
        fb_bevel(x + 5, y + 3, 22, 26, 0);
        fb_fill(x + 9, y + 19, 4, 7, G_RED);
        fb_fill(x + 15, y + 14, 4, 12, G_GREEN);
        fb_fill(x + 21, y + 10, 4, 16, G_BLUE);
        break;
    case ICON_DONUT:
        fb_ring(x + 16, y + 16, 13, 7, G_MAGENTA);
        fb_ring(x + 16, y + 16, 9, 8, G_YELLOW);
        break;
    case ICON_POWER:
        fb_circle(x + 16, y + 16, 12, G_MAROON);
        fb_fill(x + 15, y + 7, 3, 11, G_WHITE);
        fb_fill(x + 10, y + 9, 2, 2, G_SILVER);
        fb_fill(x + 20, y + 9, 2, 2, G_SILVER);
        break;
    case ICON_FOLDER:
        fb_fill(x + 3, y + 7, 26, 20, G_YELLOW);
        fb_fill(x + 3, y + 4, 11, 5, G_YELLOW);
        fb_bevel(x + 3, y + 7, 26, 20, 0);
        break;
    }
}

static void draw_small_icon(int kind, int x, int y)
{
    switch (kind) {
    case ICON_COMPUTER:
        fb_fill(x + 1, y + 2, 14, 9, G_SILVER);
        fb_fill(x + 3, y + 4, 10, 5, G_TEAL);
        fb_fill(x + 6, y + 11, 4, 2, G_GRAY);
        fb_fill(x + 4, y + 13, 8, 1, G_SILVER);
        break;
    case ICON_FOLDER:
        fb_fill(x + 1, y + 4, 14, 10, G_YELLOW);
        fb_fill(x + 1, y + 2, 6, 3, G_YELLOW);
        break;
    case ICON_PAGE:
    case ICON_FILE:
    case ICON_EDITOR:
    case ICON_CHART:
        fb_fill(x + 3, y + 1, 10, 14, G_WHITE);
        fb_fill(x + 5, y + 4, 6, 1, G_GRAY);
        fb_fill(x + 5, y + 7, 6, 1, G_GRAY);
        fb_fill(x + 5, y + 10, 4, 1, G_GRAY);
        if (kind == ICON_EDITOR)
            fb_fill(x + 10, y + 8, 2, 7, G_YELLOW);
        if (kind == ICON_CHART) {
            fb_fill(x + 5, y + 10, 2, 4, G_RED);
            fb_fill(x + 9, y + 8, 2, 6, G_GREEN);
        }
        break;
    case ICON_CONSOLE:
    case ICON_TERM:
        fb_fill(x + 1, y + 2, 14, 11, G_BLACK);
        fb_fill(x + 1, y + 2, 14, 2, G_SILVER);
        fb_fill(x + 3, y + 6, 2, 1, G_LIME);
        fb_fill(x + 3, y + 8, 5, 1, G_LIME);
        fb_fill(x + 6, y + 6, 1, 3, G_LIME);
        break;
    case ICON_GEAR:
        fb_fill(x + 3, y + 3, 10, 10, G_SILVER);
        fb_fill(x + 1, y + 6, 2, 4, G_SILVER);
        fb_fill(x + 13, y + 6, 2, 4, G_SILVER);
        fb_fill(x + 6, y + 1, 4, 2, G_SILVER);
        fb_fill(x + 6, y + 13, 4, 2, G_SILVER);
        fb_circle(x + 8, y + 8, 3, G_NAVY);
        break;
    case ICON_INFO:
        fb_circle(x + 8, y + 8, 7, G_NAVY);
        fb_fill(x + 7, y + 4, 2, 2, G_WHITE);
        fb_fill(x + 7, y + 7, 2, 6, G_WHITE);
        break;
    case ICON_GLOBE:
        fb_circle(x + 8, y + 8, 7, G_BLUE);
        fb_fill(x + 2, y + 7, 12, 1, G_GREEN);
        fb_fill(x + 2, y + 10, 12, 1, G_GREEN);
        fb_fill(x + 7, y + 2, 1, 12, G_CYAN);
        break;
    case ICON_DONUT:
        fb_ring(x + 8, y + 8, 7, 4, G_MAGENTA);
        break;
    case ICON_POWER:
        fb_circle(x + 8, y + 8, 7, G_MAROON);
        fb_fill(x + 7, y + 3, 2, 6, G_WHITE);
        break;
    }
}

typedef struct {
    int open;
    int app;
    int x;
    int y;
    int w;
    int h;
    int scroll;
    int selected;
    char path[48];
    char title[40];
} win_t;

static win_t g_win[MAX_WIN];
static int g_order[MAX_WIN];
static int g_order_count;
static int g_focus = -1;
static int g_quit;
static int g_start_open;
static int g_start_sel;
static int g_drag_win = -1;
static int g_drag_dx;
static int g_drag_dy;
static int g_icon_sel = -1;
static uint64_t g_last_click_tsc;
static int g_last_click_x = -100;
static int g_last_click_y = -100;

static const int g_icon_actions[5] = {
    ACT_FILES, ACT_NOTEPAD, ACT_CONSOLE, ACT_SYSPROP, ACT_ABOUT
};
static const int g_icon_kinds[5] = {
    ICON_COMPUTER, ICON_PAGE, ICON_CONSOLE, ICON_GEAR, ICON_INFO
};
static const char *g_icon_labels[5] = {
    "My Computer", "Notepad", "Command Prompt", "System Properties", "About"
};

static char g_files_path[48] = "/";
static bfs_dir_entry_t g_files_entries[64];
static int g_files_count;
static int g_files_sel;
static int g_files_scroll;
static int g_files_live;

#define CON_LINES 220
#define CON_COLS 70

static char g_con[CON_LINES][CON_COLS + 2];
static int g_con_count;
static int g_con_head;
static char g_con_cwd[64] = "/";
static char g_con_input[56];
static int g_con_len;
static int g_con_scroll;

static void con_add_line(const char *line)
{
    int idx;
    if (g_con_count < CON_LINES) {
        idx = (g_con_head + g_con_count) % CON_LINES;
        g_con_count++;
    } else {
        idx = g_con_head;
        g_con_head = (g_con_head + 1) % CON_LINES;
    }
    str_copy(g_con[idx], line, CON_COLS + 1);
}

static void con_add(const char *s)
{
    char line[CON_COLS + 1];
    int len = 0;

    if (!s)
        return;
    while (*s) {
        if (*s == '\r') {
            s++;
            continue;
        }
        if (*s == '\n') {
            line[len] = 0;
            con_add_line(line);
            len = 0;
            s++;
            continue;
        }
        line[len++] = *s++;
        if (len == CON_COLS) {
            line[len] = 0;
            con_add_line(line);
            len = 0;
        }
    }
    if (len) {
        line[len] = 0;
        con_add_line(line);
    }
}

static const char *con_line(int index)
{
    return g_con[(g_con_head + index) % CON_LINES];
}

static void con_printf(const char *fmt, ...)
{
    char tmp[320];
    va_list ap;
    va_start(ap, fmt);
    int n = 0;
    while (*fmt && n < (int)sizeof(tmp) - 1) {
        if (*fmt != '%') {
            tmp[n++] = *fmt++;
            continue;
        }
        fmt++;
        if (*fmt == '%') {
            tmp[n++] = '%';
            fmt++;
            continue;
        }
        char pad = ' ';
        int width = 0;
        int longs = 0;
        if (*fmt == '0') {
            pad = '0';
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }
        while (*fmt == 'l') {
            longs++;
            fmt++;
        }
        char spec = *fmt;
        if (spec)
            fmt++;

        char num[32];
        int m = 0;
        if (spec == 's') {
            const char *s = va_arg(ap, const char *);
            if (!s)
                s = "(null)";
            while (*s && n < (int)sizeof(tmp) - 1)
                tmp[n++] = *s++;
            continue;
        }
        if (spec == 'c') {
            tmp[n++] = (char)va_arg(ap, int);
            continue;
        }

        uint64_t value;
        int negative = 0;
        if (spec == 'd' || spec == 'i') {
            int64_t sv;
            if (longs == 0)
                sv = va_arg(ap, int);
            else if (longs == 1)
                sv = va_arg(ap, long);
            else
                sv = va_arg(ap, long long);
            if (sv < 0) {
                negative = 1;
                value = (uint64_t)(-(sv + 1)) + 1u;
            } else {
                value = (uint64_t)sv;
            }
        } else if (longs == 0) {
            value = va_arg(ap, unsigned int);
        } else if (longs == 1) {
            value = va_arg(ap, unsigned long);
        } else {
            value = va_arg(ap, unsigned long long);
        }

        unsigned base = (spec == 'x' || spec == 'X') ? 16u : 10u;
        if (negative)
            num[m++] = '-';
        char digits[24];
        int count = 0;
        if (value == 0)
            digits[count++] = '0';
        while (value) {
            unsigned d = (unsigned)(value % base);
            digits[count++] = (char)(d < 10 ? '0' + d
                                          : (spec == 'X' ? 'A' : 'a') + d - 10);
            value /= base;
        }
        int prefix = m;
        if (pad == ' ')
            while (m + count < width)
                num[m++] = ' ';
        while (count > 0)
            num[m++] = digits[--count];
        if (pad == '0') {
            while (m - prefix < width) {
                for (int k = m; k > prefix; k--)
                    num[k] = num[k - 1];
                num[prefix] = '0';
                m++;
            }
        }
        for (int i = 0; i < m && n < (int)sizeof(tmp) - 1; i++)
            tmp[n++] = num[i];
    }
    va_end(ap);
    tmp[n] = 0;
    con_add(tmp);
}

static char g_note_text[32768];
static uint32_t g_note_len;
static char g_note_name[48];
static uint16_t g_note_start[700];
static uint8_t g_note_size[700];
static int g_note_lines;

static void note_reflow(void)
{
    uint32_t i = 0;
    g_note_lines = 0;
    while (i < g_note_len && g_note_lines < 700) {
        uint32_t start = i;
        uint32_t end = i;
        while (end < g_note_len && g_note_text[end] != '\n' &&
               end - start < 58)
            end++;
        if (end - start >= 58 && end < g_note_len &&
            g_note_text[end] != '\n') {
            uint32_t cut = end;
            while (cut > start && g_note_text[cut - 1] != ' ')
                cut--;
            if (cut > start)
                end = cut - 1;
        }
        g_note_start[g_note_lines] = (uint16_t)start;
        g_note_size[g_note_lines] = (uint8_t)(end - start);
        g_note_lines++;
        if (end < g_note_len &&
            (g_note_text[end] == '\n' || g_note_text[end] == ' '))
            end++;
        i = end;
    }
}

static void open_app(int app);
static void close_win(int idx);
static void bring_front(int idx);

static int cmd_match(const char *p, const char *name, const char **arg)
{
    size_t n = kstrlen(name);
    for (size_t i = 0; i < n; i++) {
        char a = p[i];
        char b = name[i];
        if (a >= 'A' && a <= 'Z')
            a = (char)(a + 32);
        if (b >= 'A' && b <= 'Z')
            b = (char)(b + 32);
        if (a != b)
            return 0;
    }
    if (p[n] != 0 && p[n] != ' ')
        return 0;
    const char *a = p + n;
    while (*a == ' ')
        a++;
    if (arg)
        *arg = a;
    return 1;
}

static void size_text(uint64_t bytes, char *out, size_t cap)
{
    const uint64_t mib = 1024ULL * 1024ULL;
    const uint64_t gib = 1024ULL * 1024ULL * 1024ULL;

    if (bytes >= gib) {
        uint64_t whole = bytes / gib;
        uint64_t frac = ((bytes % gib) * 10) / gib;
        format_buf(out, cap, "%llu.%llu GiB",
                   (unsigned long long)whole, (unsigned long long)frac);
    } else if (bytes >= mib) {
        format_buf(out, cap, "%llu MiB", (unsigned long long)(bytes / mib));
    } else if (bytes >= 1024) {
        format_buf(out, cap, "%llu KiB", (unsigned long long)(bytes / 1024));
    } else {
        format_buf(out, cap, "%llu B", (unsigned long long)bytes);
    }
}

static void gui_ls(const char *arg)
{
    if (!bfs_mounted()) {
        con_add("ls: no ByteFS disk mounted.");
        return;
    }
    char abs[96];
    path_normalize(g_con_cwd, arg, abs, sizeof(abs));
    if (!bfs_is_dir(abs)) {
        con_printf("ls: %s: Not a directory", abs);
        return;
    }
    static bfs_dir_entry_t entries[64];
    int n = bfs_list_dir(abs, entries, 64);
    con_printf("total %d", n);
    for (int i = 0; i < n; i++) {
        bfs_dir_entry_t *e = &entries[i];
        if (e->is_dir)
            con_printf("%s %s/", "drwx", e->name);
        else
            con_printf("%s %s  %u bytes", "-rw-", e->name, e->size);
    }
}

static void gui_cd(const char *arg)
{
    if (!bfs_mounted()) {
        con_add("cd: no ByteFS disk mounted.");
        return;
    }
    char abs[96];
    if (!arg || !*arg)
        path_normalize(g_con_cwd, "/home", abs, sizeof(abs));
    else
        path_normalize(g_con_cwd, arg, abs, sizeof(abs));
    if (!bfs_is_dir(abs)) {
        con_printf("cd: %s: Not a directory", abs);
        return;
    }
    str_copy(g_con_cwd, abs, sizeof(g_con_cwd));
}

static void gui_mkdir(const char *arg)
{
    if (!bfs_mounted()) {
        con_add("mkdir: no ByteFS disk mounted.");
        return;
    }
    if (!arg || !*arg) {
        con_add("usage: mkdir <directory>");
        return;
    }
    char abs[96];
    path_normalize(g_con_cwd, arg, abs, sizeof(abs));
    if (bfs_mkdir(abs[0] == '/' ? abs + 1 : abs) != 0)
        con_printf("mkdir: %s: cannot create", abs);
}

static void gui_rmdir(const char *arg)
{
    if (!bfs_mounted()) {
        con_add("rmdir: no ByteFS disk mounted.");
        return;
    }
    if (!arg || !*arg) {
        con_add("usage: rmdir <directory>");
        return;
    }
    char abs[96];
    path_normalize(g_con_cwd, arg, abs, sizeof(abs));
    int rc = bfs_rmdir(abs[0] == '/' ? abs + 1 : abs);
    if (rc == -2)
        con_printf("rmdir: %s: Not a directory", abs);
    else if (rc == -3)
        con_printf("rmdir: %s: Directory not empty", abs);
    else if (rc != 0)
        con_printf("rmdir: %s: cannot remove", abs);
}

static void gui_touch(const char *arg)
{
    if (!bfs_mounted()) {
        con_add("touch: no ByteFS disk mounted.");
        return;
    }
    if (!arg || !*arg) {
        con_add("usage: touch <file>");
        return;
    }
    char abs[96];
    path_normalize(g_con_cwd, arg, abs, sizeof(abs));
    if (bfs_is_dir(abs)) {
        con_printf("touch: %s: Is a directory", abs);
        return;
    }
    if (!bfs_exists(abs[0] == '/' ? abs + 1 : abs)) {
        if (bfs_add(abs[0] == '/' ? abs + 1 : abs, "", 0,
                    bfs_is_html(abs)) != 0)
            con_printf("touch: %s: cannot create", abs);
    }
}

static void gui_rm(const char *arg)
{
    if (!bfs_mounted()) {
        con_add("rm: no ByteFS disk mounted.");
        return;
    }
    if (!arg || !*arg) {
        con_add("usage: rm <file>");
        return;
    }
    char abs[96];
    path_normalize(g_con_cwd, arg, abs, sizeof(abs));
    if (!bfs_exists(abs[0] == '/' ? abs + 1 : abs)) {
        con_printf("rm: %s: No such file", abs);
        return;
    }
    int rc = bfs_unlink(abs[0] == '/' ? abs + 1 : abs);
    if (rc == -2)
        con_printf("rm: %s: Is a directory", abs);
    else if (rc != 0)
        con_printf("rm: %s: cannot remove", abs);
}

static void gui_cat(const char *arg)
{
    if (!bfs_mounted()) {
        con_add("cat: no ByteFS disk mounted.");
        return;
    }
    if (!arg || !*arg) {
        con_add("usage: cat <file>");
        return;
    }
    char abs[96];
    path_normalize(g_con_cwd, arg, abs, sizeof(abs));
    if (bfs_is_dir(abs)) {
        con_printf("cat: %s: Is a directory", abs);
        return;
    }
    static char buf[32768];
    uint32_t len = 0;
    if (bfs_read(abs, buf, sizeof(buf), &len) != 0) {
        con_printf("cat: %s: cannot read", abs);
        return;
    }
    con_add(buf);
}

static void gui_lsblk(void)
{
    con_add("NAME    SIZE      TYPE  MODEL");
    int any = 0;
    int mounted_slot = bfs_mounted() ? bfs_slot() : -1;
    for (int i = 0; i < ATA_MAX_DISKS; i++) {
        ata_disk_t *d = ata_slot(i);
        if (!d)
            continue;
        any = 1;
        char sz[24];
        size_text((uint64_t)d->sectors * 512ULL, sz, sizeof(sz));
        con_printf("sd%c     %s  disk  %s", (char)('a' + i), sz,
                   d->model[0] ? d->model : "ATA disk");
        if (i == mounted_slot) {
            char psz[24];
            size_text((uint64_t)(d->sectors - BFS_PART_LBA) * 512ULL,
                      psz, sizeof(psz));
            con_printf("|-sd%c1  %s  part  ByteFS", (char)('a' + i), psz);
        }
    }
    if (!any)
        con_add("no ATA disks detected");
}

static void gui_df(void)
{
    con_add("Filesystem      1K-blocks      Used Available Mounted on");
    if (bfs_mounted()) {
        ata_disk_t *d = ata_slot(bfs_slot());
        uint64_t total = d ? (uint64_t)(d->sectors - BFS_PART_LBA) * 512ULL : 0;
        uint64_t used = bfs_used_bytes();
        uint64_t avail = total > used ? total - used : 0;
        con_printf("/dev/sd%c1     %10llu %9llu %9llu /",
                   (char)('a' + bfs_slot()),
                   (unsigned long long)(total / 1024ULL),
                   (unsigned long long)(used / 1024ULL),
                   (unsigned long long)(avail / 1024ULL));
    } else {
        con_add("romfs                 0         0         0 / (live ISO)");
    }
}

static void gui_free(void)
{
    extern uint8_t __bss_end[];
    uint64_t total = g_mem_usable;
    uint64_t used = (uint64_t)((uintptr_t)__bss_end - 0x10000ULL);
    if (used > total)
        used = total;
    con_add("              total        used        free      shared  buff/cache");
    con_printf("Mem:      %9lluK %10lluK %10lluK %9lluK %10lluK",
               (unsigned long long)(total / 1024ULL),
               (unsigned long long)(used / 1024ULL),
               (unsigned long long)((total - used) / 1024ULL),
               0ULL,
               (unsigned long long)(used / 1024ULL));
    con_add("Swap:               0          0          0");
}

static void gui_uname(const char *arg)
{
    if (arg && str_eq(arg, "-a"))
        con_printf("ByteOS %s 0.2 %s #1 x86_64 GNU/ByteOS",
                   auth_hostname(), BYTEOS_CODENAME);
    else if (arg && str_eq(arg, "-r"))
        con_add("0.2");
    else if (arg && str_eq(arg, "-m"))
        con_add("x86_64");
    else if (arg && str_eq(arg, "-n"))
        con_add(auth_hostname());
    else if (arg && str_eq(arg, "-s"))
        con_add("ByteOS");
    else
        con_printf("ByteOS %s x86_64", auth_hostname());
}

static int weekday_of(int y, int m, int d)
{
    static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 3)
        y -= 1;
    int w = (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
    return w < 0 ? w + 7 : w;
}

static void gui_date(void)
{
    static const char *wd[7] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };
    static const char *mo[12] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    rtc_time_t tm;
    if (rtc_read(&tm) != 0) {
        con_add("date: RTC unavailable");
        return;
    }
    con_printf("%s %s %02d %02d:%02d:%02d %04d",
               wd[weekday_of(tm.year, tm.month, tm.day)],
               mo[(tm.month - 1) % 12], tm.day, tm.hour, tm.minute,
               tm.second, tm.year);
}

static uint64_t gui_uptime_seconds(void)
{
    if (!g_tsc_hz || read_tsc() <= g_boot_tsc)
        return 0;
    return (read_tsc() - g_boot_tsc) / g_tsc_hz;
}

static void gui_uptime(void)
{
    uint64_t up = gui_uptime_seconds();
    uint64_t days = up / 86400ULL;
    if (days)
        con_printf(" %llu day%s, %llu:%02llu up",
                   (unsigned long long)days, days == 1 ? "" : "s",
                   (unsigned long long)((up / 3600ULL) % 24ULL),
                   (unsigned long long)((up / 60ULL) % 60ULL));
    else
        con_printf(" %llu:%02llu up",
                   (unsigned long long)(up / 3600ULL),
                   (unsigned long long)((up / 60ULL) % 60ULL));
    con_add("1 user, load average: 0.00, 0.00, 0.00");
}

static void gui_echo(const char *arg)
{
    if (!arg) {
        con_add("");
        return;
    }
    char buf[160];
    str_copy(buf, arg, sizeof(buf));
    size_t len = kstrlen(buf);
    if (len >= 2 && ((buf[0] == '"' && buf[len - 1] == '"') ||
                     (buf[0] == '\'' && buf[len - 1] == '\''))) {
        buf[len - 1] = 0;
        con_add(buf + 1);
    } else {
        con_add(buf);
    }
}

static void gui_help(void)
{
    con_add("ByteWin 95 command prompt");
    con_add("  ls / dir    list directory      cd <dir>   change directory");
    con_add("  pwd         print directory    mkdir      create directory");
    con_add("  rmdir       remove directory   touch      create file");
    con_add("  rm          delete file        cat        print file");
    con_add("  lsblk       block devices      df         disk usage");
    con_add("  free        memory usage       uname      system name");
    con_add("  date        date and time      uptime     uptime");
    con_add("  hostname    hostname           echo       print text");
    con_add("  whoami      current user       clear      clear screen");
    con_add("  exit        close this window");
}

static int g_con_close;

static void gui_run_command(const char *raw)
{
    char line[96];
    str_copy(line, raw, sizeof(line));
    char *p = line;
    while (*p == ' ')
        p++;
    int end = (int)kstrlen(p);
    while (end > 0 && p[end - 1] == ' ')
        p[--end] = 0;
    if (*p == 0)
        return;

    const char *arg = 0;
    if (str_eq_ci(p, "help")) {
        gui_help();
    } else if (cmd_match(p, "ls", &arg) || cmd_match(p, "dir", &arg)) {
        gui_ls(arg);
    } else if (cmd_match(p, "cd", &arg)) {
        gui_cd(arg);
    } else if (str_eq_ci(p, "pwd")) {
        con_add(g_con_cwd);
    } else if (cmd_match(p, "mkdir", &arg)) {
        gui_mkdir(arg);
    } else if (cmd_match(p, "rmdir", &arg)) {
        gui_rmdir(arg);
    } else if (cmd_match(p, "touch", &arg)) {
        gui_touch(arg);
    } else if (cmd_match(p, "rm", &arg)) {
        gui_rm(arg);
    } else if (cmd_match(p, "cat", &arg)) {
        gui_cat(arg);
    } else if (str_eq_ci(p, "lsblk")) {
        gui_lsblk();
    } else if (str_eq_ci(p, "df")) {
        gui_df();
    } else if (str_eq_ci(p, "free")) {
        gui_free();
    } else if (cmd_match(p, "uname", &arg)) {
        gui_uname(arg);
    } else if (str_eq_ci(p, "date")) {
        gui_date();
    } else if (str_eq_ci(p, "uptime")) {
        gui_uptime();
    } else if (str_eq_ci(p, "hostname")) {
        con_add(auth_hostname());
    } else if (cmd_match(p, "echo", &arg)) {
        gui_echo(arg);
    } else if (str_eq_ci(p, "whoami")) {
        con_add(auth_is_root() ? "root" : auth_username());
    } else if (str_eq_ci(p, "clear") || str_eq_ci(p, "cls")) {
        g_con_count = 0;
        g_con_head = 0;
        g_con_scroll = 0;
    } else if (str_eq_ci(p, "exit")) {
        g_con_close = 1;
    } else if (str_eq_ci(p, "fetch")) {
        char sz[24];
        con_printf("ByteOS 0.2 \"Sprinkles\" x86_64");
        con_printf("CPU     %s", g_cpu_brand[0] ? g_cpu_brand : "unknown");
        size_text(g_mem_usable, sz, sizeof(sz));
        con_printf("Memory  %s usable / %u CPUs", sz, g_logical_cpus);
        con_printf("Uptime  %llu seconds", (unsigned long long)gui_uptime_seconds());
        con_printf("Shell   bytesh");
        con_printf("Desktop ByteWin 95 (VGA 640x480x16)");
    } else if (str_eq_ci(p, "version")) {
        con_add("ByteOS 0.2 \"Sprinkles\" / x86_64");
    } else if (str_eq_ci(p, "about")) {
        con_add("ByteWin 95, part of ByteOS 0.2.");
        con_add("A 640x480 VGA desktop built from scratch.");
    } else if (str_eq_ci(p, "start-desktop")) {
        con_add("ByteWin 95 is already running.");
    } else if (str_eq_ci(p, "su") || str_eq_ci(p, "passwd")) {
        con_add("Use the ByteOS shell for account changes.");
    } else {
        con_printf("'%s' is not recognized.", p);
        con_add("Type help for the command list.");
    }
}

static int note_open(const char *path)
{
    if (bfs_mounted()) {
        uint32_t len = 0;
        if (bfs_read(path, g_note_text, sizeof(g_note_text), &len) != 0)
            return -1;
        g_note_len = len;
    } else {
        int found = -1;
        for (unsigned int i = 0; i < g_site_file_count; i++) {
            if (str_eq_ci(g_site_files[i].path, path)) {
                found = (int)i;
                break;
            }
        }
        if (found < 0)
            return -1;
        uint32_t len = g_site_files[found].size;
        if (len > sizeof(g_note_text) - 1)
            len = sizeof(g_note_text) - 1;
        memcpy(g_note_text, g_site_files[found].data, len);
        g_note_len = len;
    }
    str_copy(g_note_name, path, sizeof(g_note_name));
    note_reflow();
    return 0;
}

static void files_load(void)
{
    g_files_live = !bfs_mounted();
    g_files_count = 0;
    g_files_sel = 0;
    g_files_scroll = 0;

    if (bfs_mounted()) {
        g_files_count = bfs_list_dir(g_files_path, g_files_entries, 64);
    } else if (str_eq(g_files_path, "/")) {
        for (unsigned int i = 0; i < g_site_file_count && g_files_count < 64; i++) {
            bfs_dir_entry_t *e = &g_files_entries[g_files_count++];
            memset(e, 0, sizeof(*e));
            str_copy(e->name, g_site_files[i].path, sizeof(e->name));
            e->size = g_site_files[i].size;
            e->html = (int)g_site_files[i].html;
            e->is_dir = 0;
        }
    }
}

static void files_up(void)
{
    if (g_files_live)
        return;
    if (str_eq(g_files_path, "/"))
        return;
    char tmp[48];
    path_normalize(g_files_path, "..", tmp, sizeof(tmp));
    str_copy(g_files_path, tmp, sizeof(g_files_path));
    files_load();
}

static void files_open_entry(int index)
{
    if (index < 0 || index >= g_files_count)
        return;
    bfs_dir_entry_t *e = &g_files_entries[index];
    if (e->is_dir) {
        char tmp[96];
        path_normalize(g_files_path, e->name, tmp, sizeof(tmp));
        str_copy(g_files_path, tmp, sizeof(g_files_path));
        files_load();
        return;
    }

    char path[96];
    if (g_files_live) {
        str_copy(path, e->name, sizeof(path));
    } else {
        path_normalize(g_files_path, e->name, path, sizeof(path));
    }
    if (note_open(path) == 0)
        open_app(APP_NOTEPAD);
}

static void tui_browser(void)
{
    browser_app("www/index.html");
}

static void tui_editor(void)
{
    editor_app("home/notes.html");
}

static void tui_terminal(void)
{
    shell_set_exit_mode(1);
    shell();
    shell_set_exit_mode(0);
}

static void run_tui(void (*fn)(void))
{
    vga_leave_graphics();
    kbd_drain();
    mouse_flush();
    fn();
    kbd_drain();
    mouse_flush();
    vga_enter_graphics();
}

static void client_rect(win_t *w, int *x, int *y, int *cw, int *ch)
{
    if (x)
        *x = w->x + 3;
    if (y)
        *y = w->y + 22;
    if (cw)
        *cw = w->w - 6;
    if (ch)
        *ch = w->h - 25;
}

static int find_app(int app)
{
    for (int i = 0; i < MAX_WIN; i++) {
        if (g_win[i].open && g_win[i].app == app)
            return i;
    }
    return -1;
}

static void remove_order(int idx)
{
    int pos = -1;
    for (int i = 0; i < g_order_count; i++) {
        if (g_order[i] == idx) {
            pos = i;
            break;
        }
    }
    if (pos < 0)
        return;
    for (int i = pos; i + 1 < g_order_count; i++)
        g_order[i] = g_order[i + 1];
    g_order_count--;
}

static void bring_front(int idx)
{
    if (idx < 0 || idx >= MAX_WIN || !g_win[idx].open)
        return;
    remove_order(idx);
    if (g_order_count < MAX_WIN)
        g_order[g_order_count++] = idx;
    g_focus = idx;
    g_start_open = 0;
}

static void close_win(int idx)
{
    if (idx < 0 || idx >= MAX_WIN || !g_win[idx].open)
        return;
    g_win[idx].open = 0;
    remove_order(idx);
    if (g_focus == idx)
        g_focus = g_order_count ? g_order[g_order_count - 1] : -1;
}

static void open_app(int app)
{
    int existing = find_app(app);
    if (existing >= 0) {
        bring_front(existing);
        if (app == APP_FILES)
            files_load();
        return;
    }

    if (g_order_count >= MAX_WIN)
        close_win(g_order[0]);

    int slot = -1;
    for (int i = 0; i < MAX_WIN; i++) {
        if (!g_win[i].open) {
            slot = i;
            break;
        }
    }
    if (slot < 0)
        return;

    win_t *w = &g_win[slot];
    memset(w, 0, sizeof(*w));
    w->open = 1;
    w->app = app;

    switch (app) {
    case APP_FILES:
        w->x = 64;
        w->y = 48;
        w->w = 500;
        w->h = 336;
        str_copy(w->title, "My Computer", sizeof(w->title));
        files_load();
        break;
    case APP_NOTEPAD:
        w->x = 96;
        w->y = 40;
        w->w = 480;
        w->h = 350;
        if (g_note_name[0])
            format_buf(w->title, sizeof(w->title), "Notepad - %s", g_note_name);
        else
            str_copy(w->title, "Notepad", sizeof(w->title));
        break;
    case APP_CONSOLE:
        w->x = 92;
        w->y = 76;
        w->w = 480;
        w->h = 320;
        str_copy(w->title, "Command Prompt", sizeof(w->title));
        break;
    case APP_SYSPROP:
        w->x = 118;
        w->y = 66;
        w->w = 404;
        w->h = 306;
        str_copy(w->title, "System Properties", sizeof(w->title));
        break;
    case APP_ABOUT:
        w->x = 85;
        w->y = 82;
        w->w = 470;
        w->h = 280;
        str_copy(w->title, "About ByteWin 95", sizeof(w->title));
        break;
    }

    g_order[g_order_count++] = slot;
    g_focus = slot;
    g_start_open = 0;
}

static void draw_small_file(int x, int y)
{
    fb_fill(x + 3, y + 1, 10, 14, G_WHITE);
    fb_hline(x + 5, y + 4, 6, G_GRAY);
    fb_hline(x + 5, y + 7, 6, G_GRAY);
    fb_hline(x + 5, y + 10, 4, G_GRAY);
}

static void list_scroll_fix(int visible)
{
    if (g_files_sel < g_files_scroll)
        g_files_scroll = g_files_sel;
    if (g_files_sel >= g_files_scroll + visible)
        g_files_scroll = g_files_sel - visible + 1;
    if (g_files_scroll < 0)
        g_files_scroll = 0;
}

static void draw_files(win_t *w, int cx, int cy, int cw, int ch)
{
    fb_fill(cx + 2, cy + 2, 34, 20, G_SILVER);
    fb_bevel(cx + 2, cy + 2, 34, 20, 1);
    fb_text_center(cx + 2, cy + 4, 34, "Up", G_BLACK, -1);

    fb_fill(cx + 40, cy + 2, cw - 44, 20, G_WHITE);
    fb_bevel(cx + 40, cy + 2, cw - 44, 20, 0);
    fb_text(cx + 44, cy + 4,
            g_files_live ? "Live ISO (read-only)" : g_files_path,
            G_BLACK, -1);

    int list_y = cy + 26;
    int list_h = ch - 46;
    fb_fill(cx + 2, list_y, cw - 4, list_h, G_WHITE);
    fb_bevel(cx + 2, list_y, cw - 4, list_h, 0);

    int visible = (list_h - 4) / 18;
    if (visible < 1)
        visible = 1;
    list_scroll_fix(visible);

    if (g_files_count == 0) {
        fb_text(cx + 8, list_y + 6,
                g_files_live ? "(no live files)" : "(empty directory)",
                G_GRAY, -1);
    }

    for (int row = 0; row < visible; row++) {
        int idx = g_files_scroll + row;
        if (idx >= g_files_count)
            break;
        int ry = list_y + 2 + row * 18;
        uint8_t fg = G_BLACK;
        int bg = -1;
        if (idx == g_files_sel) {
            fb_fill(cx + 4, ry, cw - 8, 18, G_NAVY);
            fg = G_WHITE;
            bg = G_NAVY;
        }
        bfs_dir_entry_t *e = &g_files_entries[idx];
        if (e->is_dir)
            draw_small_icon(ICON_FOLDER, cx + 6, ry + 1);
        else
            draw_small_file(cx + 6, ry + 1);
        fb_text(cx + 26, ry + 1, e->name, fg, bg);

        if (!e->is_dir) {
            char sz[24];
            format_buf(sz, sizeof(sz), "%u", e->size);
            fb_text(cx + cw - 16 - (int)kstrlen(sz) * 8, ry + 1,
                    sz, fg, bg);
        }
    }

    const char *status;
    if (g_files_live)
        status = "Live ISO files are read-only. Double-click to open.";
    else
        status = "Double-click a folder or file. Backspace goes up.";
    fb_text(cx + 4, cy + ch - 16, status, G_GRAY, -1);

    if (w->scroll != g_files_scroll)
        w->scroll = g_files_scroll;
}

static void draw_notepad(win_t *w, int cx, int cy, int cw, int ch)
{
    fb_fill(cx + 2, cy + 2, cw - 4, ch - 22, G_WHITE);
    fb_bevel(cx + 2, cy + 2, cw - 4, ch - 22, 0);

    int visible = (ch - 28) / 16;
    if (visible < 1)
        visible = 1;
    int max_scroll = g_note_lines > visible ? g_note_lines - visible : 0;
    if (w->scroll > max_scroll)
        w->scroll = max_scroll;
    if (w->scroll < 0)
        w->scroll = 0;

    for (int row = 0; row < visible; row++) {
        int idx = w->scroll + row;
        if (idx >= g_note_lines)
            break;
        char line[60];
        int len = g_note_size[idx];
        uint32_t start = g_note_start[idx];
        if (len > 58)
            len = 58;
        for (int k = 0; k < len; k++) {
            unsigned char c = (unsigned char)g_note_text[start + k];
            if (c == '\r' || c == '\t' || c < 32)
                c = ' ';
            line[k] = (char)c;
        }
        line[len] = 0;
        fb_text(cx + 6, cy + 4 + row * 16, line, G_BLACK, -1);
    }

    char st[80];
    format_buf(st, sizeof(st), "%d lines   %u bytes   PgUp/PgDn scroll",
               g_note_lines, g_note_len);
    fb_text(cx + 4, cy + ch - 16, st, G_GRAY, -1);
}

static void con_prompt(char *out, size_t cap)
{
    format_buf(out, cap, "%s@%s:%s%c ", auth_username(), auth_hostname(),
               str_eq(g_con_cwd, "/home") ? "~" : g_con_cwd,
               auth_is_root() ? '#' : '$');
}

static void draw_console(win_t *w, int cx, int cy, int cw, int ch)
{
    fb_fill(cx + 2, cy + 2, cw - 4, ch - 4, G_BLACK);
    fb_bevel(cx + 2, cy + 2, cw - 4, ch - 4, 0);

    int rows = (ch - 10) / 16;
    if (rows < 1)
        rows = 1;
    int total = g_con_count;
    int start = total > rows ? total - rows : 0;
    start -= g_con_scroll;
    if (start < 0)
        start = 0;

    for (int row = 0; row < rows; row++) {
        int idx = start + row;
        if (idx >= g_con_count)
            break;
        fb_text(cx + 6, cy + 4 + row * 16, con_line(idx), G_SILVER,
                -1);
    }

    char prompt[72];
    con_prompt(prompt, sizeof(prompt));
    int py = cy + ch - 18;
    fb_text(cx + 6, py, prompt, G_LIME, -1);
    int px = cx + 6 + (int)kstrlen(prompt) * 8;
    fb_text(px, py, g_con_input, G_WHITE, -1);
    int cursor_x = px + g_con_len * 8;
    if (cursor_x < cx + cw - 8)
        fb_fill(cursor_x, py + 14, 7, 2, G_WHITE);

    (void)w;
}

static void draw_sysprop(win_t *w, int cx, int cy, int cw, int ch)
{
    fb_fill(cx + 2, cy + 2, cw - 4, ch - 4, G_SILVER);

    int px = cx + 12;
    int py = cy + 10;
    char line[96];
    char sz[24];

    fb_text(px, py, "ByteOS 0.2 \"Sprinkles\"", G_NAVY, -1);
    format_buf(line, sizeof(line), "Registered to: %s",
               auth_is_root() ? "root" : auth_username());
    fb_text(px, py + 22, line, G_BLACK, -1);

    format_buf(line, sizeof(line), "Computer: x86_64, %u logical CPUs",
               g_logical_cpus);
    fb_text(px, py + 44, line, G_BLACK, -1);

    format_buf(line, sizeof(line), "CPU: %s", g_cpu_brand[0] ? g_cpu_brand : "unknown");
    fb_text(px, py + 60, line, G_BLACK, -1);

    size_text(g_mem_usable, sz, sizeof(sz));
    format_buf(line, sizeof(line), "Memory: %s usable", sz);
    fb_text(px, py + 76, line, G_BLACK, -1);

    if (bfs_mounted()) {
        ata_disk_t *d = ata_slot(bfs_slot());
        if (d) {
            size_text((uint64_t)d->sectors * 512ULL, sz, sizeof(sz));
            format_buf(line, sizeof(line), "Disk: %s, %s", d->model, sz);
        } else {
            str_copy(line, "Disk: ByteFS", sizeof(line));
        }
    } else {
        str_copy(line, "Disk: live ISO (read-only)", sizeof(line));
    }
    fb_text(px, py + 92, line, G_BLACK, -1);

    fb_text(px, py + 108, "Video: VGA mode 12h, 640x480, 16 colours",
            G_BLACK, -1);
    fb_text(px, py + 124, "Input: PS/2 keyboard and mouse", G_BLACK, -1);

    format_buf(line, sizeof(line), "Uptime: %llu seconds",
               (unsigned long long)gui_uptime_seconds());
    fb_text(px, py + 140, line, G_BLACK, -1);

    fb_text(px, py + 166, "ByteFS is a flat 32-file filesystem.", G_GRAY, -1);
    fb_text(px, py + 182, "Use the command prompt for df and lsblk.", G_GRAY, -1);

    (void)w;
    (void)cw;
    (void)ch;
}

static void draw_about(win_t *w, int cx, int cy, int cw, int ch)
{
    fb_fill(cx + 2, cy + 2, cw - 4, ch - 4, G_SILVER);

    fb_text_center(cx, cy + 8, cw, "ByteWin 95", G_NAVY, -1);

    int lx = cx + 16;
    int ly = cy + 40;
    fb_text(lx, ly, "ByteOS 0.2 \"Sprinkles\"", G_BLACK, -1);
    fb_text(lx, ly + 20, "A real 640x480 VGA desktop, not a text-mode skin.",
            G_BLACK, -1);
    fb_text(lx, ly + 36, "Mouse, windows, icons, menus, a taskbar and a",
            G_BLACK, -1);
    fb_text(lx, ly + 52, "small command prompt.", G_BLACK, -1);
    fb_text(lx, ly + 76, "The classic text-mode ByteDesktop is still",
            G_BLACK, -1);
    fb_text(lx, ly + 92, "available from start-desktop.", G_BLACK, -1);
    fb_text(lx, ly + 116, "F12 leaves ByteWin 95 for the ByteOS shell.",
            G_GRAY, -1);

    fb_text(lx, ly + 148, "github.com/withdrawment/byteos", G_NAVY, -1);

    (void)w;
    (void)ch;
}

static void draw_window(int idx)
{
    win_t *w = &g_win[idx];
    if (!w->open)
        return;

    int active = (idx == g_focus);

    fb_fill(w->x + 4, w->y + 4, w->w, w->h, G_BLACK);
    fb_fill(w->x, w->y, w->w, w->h, G_SILVER);
    fb_bevel(w->x, w->y, w->w, w->h, 1);
    fb_hline(w->x + 1, w->y + 1, w->w - 2, G_WHITE);

    fb_fill(w->x + 2, w->y + 2, w->w - 4, 18,
            active ? G_NAVY : G_GRAY);
    char title[36];
    str_copy(title, w->title, sizeof(title));
    fb_text(w->x + 6, w->y + 3, title, G_WHITE, active ? G_NAVY : G_GRAY);

    int bx = w->x + w->w - 18;
    int by = w->y + 4;
    fb_fill(bx, by, 14, 14, G_SILVER);
    fb_bevel(bx, by, 14, 14, 1);
    for (int i = 0; i < 7; i++) {
        fb_px(bx + 4 + i, by + 4 + i, G_BLACK);
        fb_px(bx + 10 - i, by + 4 + i, G_BLACK);
    }

    int cx, cy, cw, ch;
    client_rect(w, &cx, &cy, &cw, &ch);
    switch (w->app) {
    case APP_FILES:
        draw_files(w, cx, cy, cw, ch);
        break;
    case APP_NOTEPAD:
        draw_notepad(w, cx, cy, cw, ch);
        break;
    case APP_CONSOLE:
        draw_console(w, cx, cy, cw, ch);
        break;
    case APP_SYSPROP:
        draw_sysprop(w, cx, cy, cw, ch);
        break;
    case APP_ABOUT:
        draw_about(w, cx, cy, cw, ch);
        break;
    }
}

static void draw_icon_label(int x, int y, int w, const char *label, int selected)
{
    int len = (int)kstrlen(label);
    int split = -1;

    if (len > 13) {
        int best = 1000;
        for (int i = 1; i + 1 < len; i++) {
            if (label[i] != ' ')
                continue;
            int left = i;
            int right = len - i - 1;
            if (left > 13 || right > 13)
                continue;
            int score = left > right ? left : right;
            if (score < best) {
                best = score;
                split = i;
            }
        }
    }

    if (split < 0) {
        if (selected)
            fb_fill(x, y, w, 18, G_NAVY);
        fb_text_center(x, y + 2, w, label, G_WHITE, selected ? G_NAVY : -1);
        return;
    }

    char first[24];
    char second[24];
    int i = 0;
    for (; i < split && i < 23; i++)
        first[i] = label[i];
    first[i] = 0;
    int j = 0;
    for (i = split + 1; label[i] && j < 23; i++)
        second[j++] = label[i];
    second[j] = 0;

    if (selected)
        fb_fill(x, y, w, 34, G_NAVY);
    fb_text_center(x, y + 2, w, first, G_WHITE, selected ? G_NAVY : -1);
    fb_text_center(x, y + 18, w, second, G_WHITE, selected ? G_NAVY : -1);
}

static void draw_desktop(void)
{
    fb_fill(0, 0, GW, GH, G_TEAL);

    fb_text(GW - 8 * 10 - 10, 10, "ByteWin 95", G_CYAN, -1);
    fb_text(GW - 8 * 20 - 10, 28, "ByteOS 0.2 Sprinkles", G_CYAN, -1);

    for (int i = 0; i < 5; i++) {
        int x = 6;
        int y = 12 + i * 78;
        draw_big_icon(g_icon_kinds[i], x + 30, y);
        draw_icon_label(0, y + 34, 104, g_icon_labels[i], g_icon_sel == i);
    }

    for (int i = 0; i < g_order_count; i++)
        draw_window(g_order[i]);
}

static void draw_taskbar(void)
{
    fb_fill(0, TASKBAR_Y, GW, GH - TASKBAR_Y, G_SILVER);
    fb_hline(0, TASKBAR_Y, GW, G_WHITE);
    fb_hline(0, TASKBAR_Y + 1, GW, G_GRAY);

    fb_fill(2, TASKBAR_Y + 3, 58, 24, G_SILVER);
    fb_bevel(2, TASKBAR_Y + 3, 58, 24, g_start_open ? 0 : 1);
    fb_fill(6, TASKBAR_Y + 8, 13, 13, G_RED);
    fb_fill(9, TASKBAR_Y + 11, 7, 7, G_YELLOW);
    fb_fill(11, TASKBAR_Y + 13, 3, 3, G_BLUE);
    fb_text(23, TASKBAR_Y + 6, "Start", G_BLACK, -1);

    int bx = 66;
    for (int i = 0; i < g_order_count; i++) {
        int idx = g_order[i];
        win_t *w = &g_win[idx];
        fb_fill(bx, TASKBAR_Y + 3, 100, 24, G_SILVER);
        fb_bevel(bx, TASKBAR_Y + 3, 100, 24, idx == g_focus ? 0 : 1);
        char label[14];
        str_copy(label, w->title, sizeof(label));
        fb_text(bx + 5, TASKBAR_Y + 6, label, G_BLACK, -1);
        bx += 104;
    }

    fb_fill(GW - 70, TASKBAR_Y + 3, 66, 24, G_SILVER);
    fb_bevel(GW - 70, TASKBAR_Y + 3, 66, 24, 0);
    rtc_time_t tm;
    char clock[8];
    if (rtc_read(&tm) == 0)
        format_buf(clock, sizeof(clock), "%02d:%02d", tm.hour, tm.minute);
    else
        str_copy(clock, "--:--", sizeof(clock));
    fb_text_center(GW - 70, TASKBAR_Y + 6, 66, clock, G_BLACK, -1);
}

static int start_menu_height(void)
{
    int h = 8;
    for (int i = 0; i < START_COUNT; i++)
        h += g_start_items[i].action == ACT_SEP ? 8 : 22;
    return h;
}

static int start_menu_top(void)
{
    return TASKBAR_Y - 4 - start_menu_height();
}

static void draw_start_menu(void)
{
    int top = start_menu_top();
    int w = 210;
    int h = start_menu_height();

    fb_fill(5, top + 4, w, h, G_BLACK);
    fb_fill(2, top, w, h, G_SILVER);
    fb_bevel(2, top, w, h, 1);

    fb_fill(4, top + 2, 20, h - 4, G_NAVY);
    static const char *brand = "ByteWin";
    for (int i = 0; i < 7; i++)
        fb_char(6, top + 6 + i * 16, (unsigned char)brand[i], G_WHITE, G_NAVY);

    int y = top + 4;
    for (int i = 0; i < START_COUNT; i++) {
        if (g_start_items[i].action == ACT_SEP) {
            fb_hline(28, y + 3, w - 32, G_GRAY);
            y += 8;
            continue;
        }
        if (i == g_start_sel)
            fb_fill(26, y, w - 28, 22, G_NAVY);
        uint8_t fg = i == g_start_sel ? G_WHITE : G_BLACK;
        int bg = i == g_start_sel ? G_NAVY : -1;
        int icon_kind = ICON_PAGE;
        switch (g_start_items[i].action) {
        case ACT_FILES: icon_kind = ICON_COMPUTER; break;
        case ACT_NOTEPAD: icon_kind = ICON_PAGE; break;
        case ACT_CONSOLE:
        case ACT_TERMINAL: icon_kind = ICON_CONSOLE; break;
        case ACT_SYSPROP: icon_kind = ICON_GEAR; break;
        case ACT_ABOUT: icon_kind = ICON_INFO; break;
        case ACT_BROWSER: icon_kind = ICON_GLOBE; break;
        case ACT_EDITOR: icon_kind = ICON_EDITOR; break;
        case ACT_FETCH: icon_kind = ICON_CHART; break;
        case ACT_DONUT: icon_kind = ICON_DONUT; break;
        case ACT_EXIT: icon_kind = ICON_POWER; break;
        }
        draw_small_icon(icon_kind, 29, y + 3);
        fb_text(50, y + 3, g_start_items[i].label, fg, bg);
        y += 22;
    }
}

static void draw_cursor(void)
{
    static const char *shape[17] = {
        "##          ",
        "#.#         ",
        "#..#        ",
        "#...#       ",
        "#....#      ",
        "#.....#     ",
        "#......#    ",
        "#.......#   ",
        "#........#  ",
        "#.........# ",
        "#....###### ",
        "#..#..#     ",
        "#.# #..#    ",
        "##  #..#    ",
        "#    #..#   ",
        "     #..#   ",
        "      ##    "
    };
    int mx, my, buttons;
    mouse_get_state(&mx, &my, &buttons);
    (void)buttons;
    for (int row = 0; row < 17; row++) {
        for (int col = 0; col < 12; col++) {
            char c = shape[row][col];
            if (c == '#')
                fb_px(mx + col, my + row, G_BLACK);
            else if (c == '.')
                fb_px(mx + col, my + row, G_WHITE);
        }
    }
}

static int window_at(int mx, int my)
{
    for (int i = g_order_count - 1; i >= 0; i--) {
        int idx = g_order[i];
        win_t *w = &g_win[idx];
        if (mx >= w->x && mx < w->x + w->w &&
            my >= w->y && my < w->y + w->h)
            return idx;
    }
    return -1;
}

static int taskbar_button_at(int mx, int my)
{
    if (my < TASKBAR_Y + 3 || my >= TASKBAR_Y + 27)
        return -1;
    for (int i = 0; i < g_order_count; i++) {
        int bx = 66 + i * 104;
        if (mx >= bx && mx < bx + 100)
            return g_order[i];
    }
    return -1;
}

static int start_item_at(int mx, int my)
{
    int top = start_menu_top();
    int h = start_menu_height();
    if (mx < 2 || mx >= 212 || my < top || my >= top + h)
        return -1;
    int y = top + 4;
    for (int i = 0; i < START_COUNT; i++) {
        if (g_start_items[i].action == ACT_SEP) {
            y += 8;
            continue;
        }
        if (my >= y && my < y + 22)
            return i;
        y += 22;
    }
    return -1;
}

static void start_action(int action)
{
    switch (action) {
    case ACT_FILES:
        open_app(APP_FILES);
        break;
    case ACT_NOTEPAD:
        if (!g_note_name[0]) {
            if (bfs_mounted())
                note_open("home/notes.html");
            else
                note_open("README.TXT");
        }
        open_app(APP_NOTEPAD);
        break;
    case ACT_CONSOLE:
        open_app(APP_CONSOLE);
        break;
    case ACT_SYSPROP:
        open_app(APP_SYSPROP);
        break;
    case ACT_ABOUT:
        open_app(APP_ABOUT);
        break;
    case ACT_BROWSER:
        run_tui(tui_browser);
        break;
    case ACT_EDITOR:
        run_tui(tui_editor);
        break;
    case ACT_TERMINAL:
        run_tui(tui_terminal);
        break;
    case ACT_FETCH:
        run_tui(fetch_screen);
        break;
    case ACT_DONUT:
        run_tui(donut_mode);
        break;
    case ACT_EXIT:
        g_quit = 1;
        break;
    }
}

static void app_click(int idx, int mx, int my, int dbl)
{
    win_t *w = &g_win[idx];
    int cx, cy, cw, ch;
    client_rect(w, &cx, &cy, &cw, &ch);

    if (w->app == APP_FILES) {
        if (mx >= cx + 2 && mx < cx + 36 && my >= cy + 2 && my < cy + 22) {
            files_up();
            return;
        }
        int list_y = cy + 26;
        if (my >= list_y + 2) {
            int row = (my - list_y - 2) / 18 + g_files_scroll;
            if (row >= 0 && row < g_files_count) {
                g_files_sel = row;
                if (dbl)
                    files_open_entry(row);
            }
        }
        (void)cw;
        (void)ch;
    }
}

static void handle_click(int mx, int my, int dbl)
{
    if (g_start_open) {
        int item = start_item_at(mx, my);
        g_start_open = 0;
        if (item >= 0)
            start_action(g_start_items[item].action);
        return;
    }

    if (my >= TASKBAR_Y) {
        if (mx >= 2 && mx < 60) {
            g_start_open = 1;
            g_start_sel = 0;
            return;
        }
        int idx = taskbar_button_at(mx, my);
        if (idx >= 0)
            bring_front(idx);
        return;
    }

    int idx = window_at(mx, my);
    if (idx >= 0) {
        bring_front(idx);
        win_t *w = &g_win[idx];
        if (mx >= w->x + w->w - 18 && mx < w->x + w->w - 4 &&
            my >= w->y + 4 && my < w->y + 18) {
            close_win(idx);
            return;
        }
        if (my < w->y + 22) {
            g_drag_win = idx;
            g_drag_dx = mx - w->x;
            g_drag_dy = my - w->y;
            return;
        }
        app_click(idx, mx, my, dbl);
        return;
    }

    for (int i = 0; i < 5; i++) {
        int y = 12 + i * 78;
        if (mx >= 0 && mx < 110 && my >= y && my < y + 72) {
            g_icon_sel = i;
            if (dbl)
                start_action(g_icon_actions[i]);
            return;
        }
    }
    g_icon_sel = -1;
}

static void console_key(int key)
{
    if (key == '\n') {
        char prompt[72];
        char line[160];
        con_prompt(prompt, sizeof(prompt));
        format_buf(line, sizeof(line), "%s%s", prompt, g_con_input);
        con_add(line);
        g_con_input[g_con_len] = 0;
        gui_run_command(g_con_input);
        g_con_len = 0;
        g_con_input[0] = 0;
        g_con_scroll = 0;
        if (g_con_close) {
            int idx = find_app(APP_CONSOLE);
            if (idx >= 0)
                close_win(idx);
            g_con_close = 0;
        }
    } else if (key == '\b') {
        if (g_con_len > 0) {
            g_con_len--;
            g_con_input[g_con_len] = 0;
        }
    } else if (key == KEY_PGUP) {
        int max = g_con_count - 1;
        g_con_scroll += 8;
        if (g_con_scroll > max)
            g_con_scroll = max;
    } else if (key == KEY_PGDN) {
        g_con_scroll -= 8;
        if (g_con_scroll < 0)
            g_con_scroll = 0;
    } else if (key >= 32 && key < 127 && g_con_len < 52) {
        g_con_input[g_con_len++] = (char)key;
        g_con_input[g_con_len] = 0;
    }
}

static int files_visible_rows(void)
{
    int idx = find_app(APP_FILES);
    if (idx < 0)
        return 1;
    int cx, cy, cw, ch;
    client_rect(&g_win[idx], &cx, &cy, &cw, &ch);
    int rows = (ch - 50) / 18;
    (void)cx;
    (void)cy;
    (void)cw;
    return rows < 1 ? 1 : rows;
}

static void files_key(int key)
{
    int visible = files_visible_rows();
    if (key == KEY_UP) {
        if (g_files_sel > 0)
            g_files_sel--;
        list_scroll_fix(visible);
    } else if (key == KEY_DOWN) {
        if (g_files_sel + 1 < g_files_count)
            g_files_sel++;
        list_scroll_fix(visible);
    } else if (key == KEY_PGUP) {
        g_files_sel -= visible;
        if (g_files_sel < 0)
            g_files_sel = 0;
        list_scroll_fix(visible);
    } else if (key == KEY_PGDN) {
        g_files_sel += visible;
        if (g_files_sel >= g_files_count)
            g_files_sel = g_files_count ? g_files_count - 1 : 0;
        list_scroll_fix(visible);
    } else if (key == KEY_HOME) {
        g_files_sel = 0;
        list_scroll_fix(visible);
    } else if (key == KEY_END) {
        g_files_sel = g_files_count ? g_files_count - 1 : 0;
        list_scroll_fix(visible);
    } else if (key == '\n') {
        files_open_entry(g_files_sel);
    } else if (key == '\b') {
        files_up();
    }
}

static void notepad_key(int key)
{
    if (g_focus < 0)
        return;
    win_t *w = &g_win[g_focus];
    int cx, cy, cw, ch;
    client_rect(w, &cx, &cy, &cw, &ch);
    int visible = (ch - 28) / 16;
    if (visible < 1)
        visible = 1;
    if (key == KEY_UP) {
        if (w->scroll > 0)
            w->scroll--;
    } else if (key == KEY_DOWN) {
        if (w->scroll + visible < g_note_lines)
            w->scroll++;
    } else if (key == KEY_PGUP) {
        w->scroll -= visible;
        if (w->scroll < 0)
            w->scroll = 0;
    } else if (key == KEY_PGDN) {
        w->scroll += visible;
        int max = g_note_lines > visible ? g_note_lines - visible : 0;
        if (w->scroll > max)
            w->scroll = max;
    } else if (key == KEY_HOME) {
        w->scroll = 0;
    } else if (key == KEY_END) {
        int max = g_note_lines > visible ? g_note_lines - visible : 0;
        w->scroll = max;
    }
    (void)cx;
    (void)cy;
    (void)cw;
}

static int next_start_item(int from, int dir)
{
    int i = from;
    for (int guard = 0; guard < START_COUNT; guard++) {
        i += dir;
        if (i < 0)
            i = START_COUNT - 1;
        if (i >= START_COUNT)
            i = 0;
        if (g_start_items[i].action != ACT_SEP)
            return i;
    }
    return from;
}

static void global_key(int key)
{
    if (key == KEY_F12) {
        g_quit = 1;
        return;
    }

    if (g_start_open) {
        if (key == KEY_UP)
            g_start_sel = next_start_item(g_start_sel, -1);
        else if (key == KEY_DOWN)
            g_start_sel = next_start_item(g_start_sel, 1);
        else if (key == '\n') {
            int item = g_start_sel;
            g_start_open = 0;
            if (item >= 0 && item < START_COUNT)
                start_action(g_start_items[item].action);
        } else if (key == 27 || key == KEY_F10) {
            g_start_open = 0;
        }
        return;
    }

    if (key == KEY_F10) {
        g_start_open = 1;
        g_start_sel = 0;
        return;
    }

    if (key == 27) {
        if (g_focus >= 0)
            close_win(g_focus);
        else
            g_icon_sel = -1;
        return;
    }

    if (key == '\t' && g_order_count > 0) {
        int pos = 0;
        for (int i = 0; i < g_order_count; i++) {
            if (g_order[i] == g_focus)
                pos = i;
        }
        pos = (pos + 1) % g_order_count;
        bring_front(g_order[pos]);
        return;
    }

    if (g_focus >= 0) {
        int app = g_win[g_focus].app;
        if (app == APP_CONSOLE)
            console_key(key);
        else if (app == APP_FILES)
            files_key(key);
        else if (app == APP_NOTEPAD)
            notepad_key(key);
        return;
    }

    if (key == KEY_UP) {
        if (g_icon_sel < 0)
            g_icon_sel = 4;
        else if (g_icon_sel > 0)
            g_icon_sel--;
    } else if (key == KEY_DOWN) {
        if (g_icon_sel < 0)
            g_icon_sel = 0;
        else if (g_icon_sel < 4)
            g_icon_sel++;
    } else if (key == '\n' && g_icon_sel >= 0) {
        start_action(g_icon_actions[g_icon_sel]);
    }
}

static void draw_all(void)
{
    draw_desktop();
    draw_taskbar();
    if (g_start_open)
        draw_start_menu();
    draw_cursor();
}

int win95_desktop(void)
{
    if (vga_enter_graphics() != 0)
        return -1;

    mouse_init();
    mouse_flush();
    kbd_drain();

    memset(g_win, 0, sizeof(g_win));
    g_order_count = 0;
    g_focus = -1;
    g_start_open = 0;
    g_start_sel = 0;
    g_icon_sel = -1;
    g_drag_win = -1;
    g_quit = 0;
    g_con_close = 0;
    g_con_scroll = 0;
    g_last_click_tsc = 0;

    draw_all();
    present();

    uint64_t next_clock = g_tsc_hz ? read_tsc() + g_tsc_hz : 0;
    uint64_t next_frame = 0;

    for (;;) {
        int mx, my;
        mouse_get_state(&mx, &my, 0);
        mouse_poll();
        int nmx, nmy;
        mouse_get_state(&nmx, &nmy, 0);

        int dirty = 0;
        if (nmx != mx || nmy != my)
            dirty = 1;

        if (g_drag_win >= 0) {
            win_t *w = &g_win[g_drag_win];
            w->x = nmx - g_drag_dx;
            w->y = nmy - g_drag_dy;
            if (w->x < 0)
                w->x = 0;
            if (w->y < 0)
                w->y = 0;
            if (w->x + w->w > GW)
                w->x = GW - w->w;
            if (w->y + w->h > TASKBAR_Y)
                w->y = TASKBAR_Y - w->h;
            dirty = 1;
        }

        if (mouse_take_click()) {
            uint64_t now = read_tsc();
            int dbl = 0;
            if (g_tsc_hz && g_last_click_tsc &&
                now > g_last_click_tsc &&
                now - g_last_click_tsc < g_tsc_hz / 2 &&
                iabs(nmx - g_last_click_x) < 5 &&
                iabs(nmy - g_last_click_y) < 5)
                dbl = 1;
            if (dbl) {
                g_last_click_tsc = 0;
            } else {
                g_last_click_tsc = now;
                g_last_click_x = nmx;
                g_last_click_y = nmy;
            }
            handle_click(nmx, nmy, dbl);
            dirty = 1;
        }

        if (mouse_take_release()) {
            g_drag_win = -1;
            dirty = 1;
        }

        int key = kbd_getkey();
        if (key >= 0) {
            global_key(key);
            dirty = 1;
        }

        uint64_t now = read_tsc();
        if (g_tsc_hz && now >= next_clock) {
            dirty = 1;
            next_clock += g_tsc_hz;
            if (next_clock <= now)
                next_clock = now + g_tsc_hz;
        }

        if (dirty && (!g_tsc_hz || now >= next_frame)) {
            draw_all();
            present();
            if (g_tsc_hz)
                next_frame = now + g_tsc_hz / 40;
            dirty = 0;
        }

        if (g_quit)
            break;

        if (!dirty)
            __asm__ volatile("pause");
    }

    g_drag_win = -1;
    mouse_flush();
    vga_leave_graphics();
    return 0;
}




