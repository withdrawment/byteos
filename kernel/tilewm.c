#include "kernel.h"
#include "site_data.h"
#include "font8x16.h"
#include "fetch_art.h"
#include <stdarg.h>

#define GW 640
#define GH 480

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

#define TW_MAX      8
#define TERM_ROWS   64
#define TERM_COLS   74
#define TERM_HIST   10
#define FILES_MAX   40
#define TW_PATH     96
#define VIEW_CAP    20480
#define CFG_CAP     4096
#define CFG_FILE    "bytetile.conf"
#define BR_MAX_LINES 520
#define BR_MAX_COLS  78
#define BR_SRC_CAP   32768
#define BR_MAX_LINKS 24
#define BR_HIST      8

#define LAYOUT_TILE     0
#define LAYOUT_MONOCLE  1
#define LAYOUT_GRID     2

#define CLI_TERM  0
#define CLI_FILES 1
#define CLI_SYS   2
#define CLI_VIEW  3
#define CLI_CFG   4
#define CLI_KEYS  5
#define CLI_BROWSER 6

#define TC_NORM 0
#define TC_ACC  1
#define TC_ERR  2
#define TC_DIM  3
#define TC_GOOD 4

typedef struct {
    int x, y, w, h;
} tw_rect_t;

typedef struct {
    int gaps;
    int border;
    int master;
    int bar;
    int accent;
    int bar_fg;
    int bar_bg;
    int border_active;
    int border_normal;
    int layout;
} tw_cfg_t;

typedef struct {
    int used;
    int type;
    uint32_t tags;
    char title[40];
    int scroll;
    int sel;
    char path[TW_PATH];
    char cwd[TW_PATH];
    int term_count;
    char term[TERM_ROWS][TERM_COLS + 1];
    uint8_t term_attr[TERM_ROWS];
    char input[TERM_COLS + 1];
    int input_len;
    char hist[TERM_HIST][TERM_COLS + 1];
    int hist_count;
    int hist_pos;
    bfs_dir_entry_t files[FILES_MAX];
    int file_count;
    int file_sel;
    int file_scroll;
    int view_len;
    char view[VIEW_CAP];
} client_t;

static const char g_default_cfg[] =
    "# ByteTile configuration\n"
    "# Alt+Shift+C opens this file inside ByteTile.\n"
    "# Ctrl+S saves and applies. Ctrl+R reloads.\n"
    "#\n"
    "# gaps         outer and inner window gaps, pixels\n"
    "# border       window border thickness, pixels\n"
    "# master       master area width, percent\n"
    "# layout       tile | monocle | grid\n"
    "# bar          top bar height, pixels\n"
    "# accent       selected tag color, 0-15\n"
    "# bar_fg       bar text color, 0-15\n"
    "# bar_bg       bar background color, 0-15\n"
    "# border_active focused window border color, 0-15\n"
    "# border_normal other window border color, 0-15\n"
    "gaps=5\n"
    "border=2\n"
    "master=55\n"
    "layout=tile\n"
    "bar=18\n"
    "accent=14\n"
    "bar_fg=15\n"
    "bar_bg=1\n"
    "border_active=11\n"
    "border_normal=8\n";

static client_t g_clients[TW_MAX];
static tw_cfg_t g_cfg;
static int g_focus = -1;
static int g_tag;
static int g_quit;
static int g_launcher;
static int g_launch_sel;
static int g_cfg_len;
static int g_cfg_cursor;
static int g_cfg_scroll;
static int g_cfg_hscroll;
static int g_cfg_modified;
static char g_cfg_buf[CFG_CAP];
static char g_status[96];
static uint64_t g_status_until;

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
static uint8_t g_rev8[256];
static const uint8_t g_plane_masks[4][16] = {
    { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
    { 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2 },
    { 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4 },
    { 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8 }
};

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
    if (!vga_probe())
        return -1;

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

static void fb_rect(int x, int y, int w, int h, int b, uint8_t color)
{
    if (b <= 0 || w <= 0 || h <= 0)
        return;
    fb_fill(x, y, w, b, color);
    fb_fill(x, y + h - b, w, b, color);
    fb_fill(x, y, b, h, color);
    fb_fill(x + w - b, y, b, h, color);
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

static void fb_text_lim(int x, int y, const char *s, uint8_t fg, int bg,
                        int maxw)
{
    int n = 0;
    while (*s && n * 8 < maxw) {
        fb_char(x + n * 8, y, (unsigned char)*s, fg, bg);
        n++;
        s++;
    }
}

static void fb_text_center(int x, int y, int w, const char *s,
                           uint8_t fg, int bg)
{
    int len = (int)kstrlen(s);
    fb_text(x + (w - len * 8) / 2, y, s, fg, bg);
}

static void fb_text_right(int x, int y, int right, const char *s,
                          uint8_t fg, int bg)
{
    int len = (int)kstrlen(s);
    fb_text(x + right - len * 8, y, s, fg, bg);
}

static void format_va(char *out, size_t cap, const char *fmt, va_list ap)
{
    size_t pos = 0;
    char tmp[32];

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
    out[pos] = 0;
}

static void format_buf(char *out, size_t cap, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    format_va(out, cap, fmt, ap);
    va_end(ap);
}

static void init_present(void)
{
    for (int i = 0; i < 256; i++) {
        uint8_t v = 0;
        for (int b = 0; b < 8; b++) {
            if (i & (1 << b))
                v = (uint8_t)(v | (0x80 >> b));
        }
        g_rev8[i] = v;
    }
}

static uint16_t plane_bits16(const uint8_t *src, const uint8_t *mask)
{
    uint32_t bits;
    __asm__ volatile(
        "movdqu (%1), %%xmm0\n"
        "movdqu (%2), %%xmm1\n"
        "pand %%xmm1, %%xmm0\n"
        "pxor %%xmm2, %%xmm2\n"
        "pcmpeqb %%xmm2, %%xmm0\n"
        "pmovmskb %%xmm0, %0\n"
        : "=r"(bits)
        : "r"(src), "r"(mask)
        : "xmm0", "xmm1", "xmm2");
    return (uint16_t)(~bits);
}

static void present_span(int y0, int y1)
{
    if (y0 < 0)
        y0 = 0;
    if (y1 > GH)
        y1 = GH;
    if (y0 >= y1)
        return;

    int first = y0 * GW;
    int last = y1 * GW;
    int plane_size = (GW * GH) / 8;

    for (int i = first; i < last; i += 16) {
        uint16_t m0 = plane_bits16(&g_fb[i], g_plane_masks[0]);
        uint16_t m1 = plane_bits16(&g_fb[i], g_plane_masks[1]);
        uint16_t m2 = plane_bits16(&g_fb[i], g_plane_masks[2]);
        uint16_t m3 = plane_bits16(&g_fb[i], g_plane_masks[3]);
        int off = i / 8;
        g_plane[off] = g_rev8[m0 & 0xFF];
        g_plane[off + 1] = g_rev8[m0 >> 8];
        g_plane[plane_size + off] = g_rev8[m1 & 0xFF];
        g_plane[plane_size + off + 1] = g_rev8[m1 >> 8];
        g_plane[plane_size * 2 + off] = g_rev8[m2 & 0xFF];
        g_plane[plane_size * 2 + off + 1] = g_rev8[m2 >> 8];
        g_plane[plane_size * 3 + off] = g_rev8[m3 & 0xFF];
        g_plane[plane_size * 3 + off + 1] = g_rev8[m3 >> 8];
    }

    int bytes = (y1 - y0) * (GW / 8);
    int offset = y0 * (GW / 8);
    uint8_t *vga = (uint8_t *)0xA0000;
    for (int p = 0; p < 4; p++) {
        wseq(2, (uint8_t)(1 << p));
        memcpy(vga + offset, g_plane + p * plane_size + offset,
               (size_t)bytes);
    }
    wseq(2, 0x0F);
}

static void present(void)
{
    present_span(0, GH);
}

static int clamp_int(int v, int lo, int hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

static void set_status(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    format_va(g_status, sizeof(g_status), fmt, ap);
    va_end(ap);
    g_status_until = (g_tsc_hz ? read_tsc() + g_tsc_hz * 3ULL : 1);
}

static char *trim_spaces(char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\r')
        s++;
    size_t n = kstrlen(s);
    while (n > 0) {
        char c = s[n - 1];
        if (c != ' ' && c != '\t' && c != '\r')
            break;
        s[--n] = 0;
    }
    return s;
}

static int parse_int(const char *s, int def)
{
    int sign = 1;
    int value = 0;
    int digits = 0;

    while (*s == ' ' || *s == '\t')
        s++;
    if (*s == '-') {
        sign = -1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    while (*s >= '0' && *s <= '9') {
        value = value * 10 + (*s - '0');
        digits++;
        if (value > 100000)
            return def;
        s++;
    }
    if (!digits)
        return def;
    return value * sign;
}

static int color_value(const char *s)
{
    static const struct {
        const char *name;
        int value;
    } names[] = {
        { "black", G_BLACK }, { "navy", G_NAVY }, { "blue", G_BLUE },
        { "green", G_GREEN }, { "lime", G_LIME }, { "teal", G_TEAL },
        { "cyan", G_CYAN }, { "maroon", G_MAROON }, { "red", G_RED },
        { "purple", G_PURPLE }, { "magenta", G_MAGENTA },
        { "olive", G_OLIVE }, { "brown", G_OLIVE }, { "yellow", G_YELLOW },
        { "silver", G_SILVER }, { "gray", G_GRAY }, { "grey", G_GRAY },
        { "white", G_WHITE }
    };

    if (*s >= '0' && *s <= '9')
        return clamp_int(parse_int(s, 7), 0, 15);
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (str_eq_ci(s, names[i].name))
            return names[i].value;
    }
    return -1;
}

static int layout_value(const char *s)
{
    if (str_eq_ci(s, "tile") || str_eq_ci(s, "tiling"))
        return LAYOUT_TILE;
    if (str_eq_ci(s, "monocle") || str_eq_ci(s, "mono"))
        return LAYOUT_MONOCLE;
    if (str_eq_ci(s, "grid") || str_eq_ci(s, "float"))
        return LAYOUT_GRID;
    return parse_int(s, LAYOUT_TILE);
}

static void cfg_defaults(void)
{
    g_cfg.gaps = 5;
    g_cfg.border = 2;
    g_cfg.master = 55;
    g_cfg.bar = 18;
    g_cfg.accent = G_YELLOW;
    g_cfg.bar_fg = G_WHITE;
    g_cfg.bar_bg = G_NAVY;
    g_cfg.border_active = G_CYAN;
    g_cfg.border_normal = G_GRAY;
    g_cfg.layout = LAYOUT_TILE;
}

static void cfg_apply_line(char *line)
{
    char *s = trim_spaces(line);
    if (!*s || *s == '#')
        return;

    char *eq = s;
    while (*eq && *eq != '=')
        eq++;
    if (*eq != '=')
        return;
    *eq = 0;
    char *key = trim_spaces(s);
    char *val = trim_spaces(eq + 1);
    if (!*key)
        return;

    if (str_eq_ci(key, "gaps")) {
        g_cfg.gaps = clamp_int(parse_int(val, 5), 0, 30);
    } else if (str_eq_ci(key, "border")) {
        g_cfg.border = clamp_int(parse_int(val, 2), 0, 6);
    } else if (str_eq_ci(key, "master")) {
        g_cfg.master = clamp_int(parse_int(val, 55), 20, 85);
    } else if (str_eq_ci(key, "layout")) {
        g_cfg.layout = clamp_int(layout_value(val), 0, 2);
    } else if (str_eq_ci(key, "bar")) {
        g_cfg.bar = clamp_int(parse_int(val, 18), 14, 28);
    } else if (str_eq_ci(key, "accent")) {
        int c = color_value(val);
        if (c >= 0)
            g_cfg.accent = c;
    } else if (str_eq_ci(key, "bar_fg")) {
        int c = color_value(val);
        if (c >= 0)
            g_cfg.bar_fg = c;
    } else if (str_eq_ci(key, "bar_bg")) {
        int c = color_value(val);
        if (c >= 0)
            g_cfg.bar_bg = c;
    } else if (str_eq_ci(key, "border_active")) {
        int c = color_value(val);
        if (c >= 0)
            g_cfg.border_active = c;
    } else if (str_eq_ci(key, "border_normal")) {
        int c = color_value(val);
        if (c >= 0)
            g_cfg.border_normal = c;
    }
}

static void cfg_apply_buf(const char *text, int len)
{
    char line[192];
    int pos = 0;

    cfg_defaults();
    while (pos < len) {
        int n = 0;
        int line_len = 0;
        while (pos < len && text[pos] != '\n') {
            if (line_len < (int)sizeof(line) - 1)
                line[line_len++] = text[pos];
            pos++;
            n++;
        }
        if (pos < len && text[pos] == '\n')
            pos++;
        line[line_len] = 0;
        cfg_apply_line(line);
        if (n == 0 && pos >= len)
            break;
    }
}

static void cfg_load(void)
{
    uint32_t len = 0;

    str_copy(g_cfg_buf, g_default_cfg, sizeof(g_cfg_buf));
    g_cfg_len = (int)kstrlen(g_cfg_buf);

    if (bfs_mounted() && bfs_exists(CFG_FILE)) {
        if (bfs_read(CFG_FILE, g_cfg_buf, sizeof(g_cfg_buf), &len) == 0)
            g_cfg_len = (int)len;
        else
            set_status("Config unreadable, using defaults");
    } else if (bfs_mounted()) {
        (void)bfs_add(CFG_FILE, g_default_cfg,
                      (uint32_t)kstrlen(g_default_cfg), 0);
    }

    g_cfg_buf[g_cfg_len] = 0;
    cfg_apply_buf(g_cfg_buf, g_cfg_len);
    g_cfg_cursor = 0;
    g_cfg_scroll = 0;
    g_cfg_hscroll = 0;
    g_cfg_modified = 0;
}

static int cfg_save(void)
{
    int rc = -1;

    cfg_apply_buf(g_cfg_buf, g_cfg_len);
    g_cfg_modified = 0;

    if (!bfs_mounted()) {
        set_status("Config applied in RAM - install to disk to persist");
        return 0;
    }

    rc = bfs_write(CFG_FILE, g_cfg_buf, (uint32_t)g_cfg_len);
    if (rc == 0)
        set_status("Saved /%s", CFG_FILE);
    else
        set_status("Config applied, could not write /%s", CFG_FILE);
    return rc;
}

void tile_config_info(void)
{
    static char info[CFG_CAP];
    uint32_t len = 0;
    int have = 0;

    kprintf("ByteTile config: /%s\n", CFG_FILE);
    kprintf("Edit inside ByteTile with Alt+Shift+C, save with Ctrl+S.\n\n");

    if (bfs_mounted() && bfs_exists(CFG_FILE) &&
        bfs_read(CFG_FILE, info, sizeof(info), &len) == 0) {
        have = 1;
    }

    if (have) {
        info[len] = 0;
        kprintf("--- current ---\n%s\n", info);
    } else {
        kprintf("--- default ---\n%s\n", g_default_cfg);
    }
}

static client_t *focused_client(void)
{
    if (g_focus < 0 || g_focus >= TW_MAX)
        return 0;
    if (!g_clients[g_focus].used)
        return 0;
    return &g_clients[g_focus];
}

static int client_visible(client_t *c)
{
    return c->used && (c->tags & (1u << g_tag));
}

static int first_visible(void)
{
    for (int i = 0; i < TW_MAX; i++) {
        if (client_visible(&g_clients[i]))
            return i;
    }
    return -1;
}

static int next_visible(int dir)
{
    if (g_focus < 0)
        return first_visible();
    int idx = g_focus;
    for (int n = 0; n < TW_MAX; n++) {
        idx += dir;
        if (idx >= TW_MAX)
            idx = 0;
        if (idx < 0)
            idx = TW_MAX - 1;
        if (client_visible(&g_clients[idx]))
            return idx;
    }
    return first_visible();
}

static void term_add(client_t *c, uint8_t attr, const char *text)
{
    if (c->term_count >= TERM_ROWS) {
        memmove(c->term[0], c->term[1],
                sizeof(c->term[0]) * (TERM_ROWS - 1));
        memmove(c->term_attr, c->term_attr + 1, TERM_ROWS - 1);
        c->term_count = TERM_ROWS - 1;
    }
    str_copy(c->term[c->term_count], text, TERM_COLS + 1);
    c->term_attr[c->term_count] = attr;
    c->term_count++;
    if (c->scroll > 0)
        c->scroll = 0;
}

static void term_fmt(client_t *c, uint8_t attr, const char *fmt, ...)
{
    char buf[256];
    va_list ap;

    va_start(ap, fmt);
    format_va(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (!buf[0]) {
        term_add(c, attr, "");
        return;
    }

    char *s = buf;
    while (*s) {
        char line[TERM_COLS + 1];
        int n = 0;
        while (*s && *s != '\n' && n < TERM_COLS)
            line[n++] = *s++;
        line[n] = 0;
        term_add(c, attr, line);
        if (*s == '\n')
            s++;
    }
}

static void fs_name(const char *path, char *out, size_t cap)
{
    while (*path == '/')
        path++;
    str_copy(out, path, cap);
}

static void join_path(const char *base, const char *name, char *out, size_t cap)
{
    if (!name || !*name) {
        str_copy(out, base, cap);
        return;
    }
    if (name[0] == '/') {
        str_copy(out, name, cap);
        return;
    }
    size_t n = kstrlen(base);
    if (n == 0 || base[n - 1] != '/') {
        if (n + 1 < cap) {
            memcpy(out, base, n);
            out[n++] = '/';
            out[n] = 0;
        } else {
            str_copy(out, base, cap);
            return;
        }
    } else {
        str_copy(out, base, cap);
        n = kstrlen(out);
    }
    str_copy(out + n, name, cap - n);
}

static void parent_path(const char *path, char *out, size_t cap)
{
    size_t n = kstrlen(path);
    while (n > 1 && path[n - 1] == '/')
        n--;
    while (n > 1 && path[n - 1] != '/')
        n--;
    while (n > 1 && path[n - 1] == '/')
        n--;
    if (n == 0)
        n = 1;
    size_t copy = n < cap - 1 ? n : cap - 1;
    memcpy(out, path, copy);
    out[copy] = 0;
    if (out[0] != '/') {
        memmove(out + 1, out, copy + 1);
        out[0] = '/';
    }
}

static void files_refresh(client_t *c)
{
    c->file_count = 0;
    c->file_sel = 0;
    c->file_scroll = 0;

    if (bfs_mounted()) {
        c->file_count = bfs_list_dir(c->path, c->files, FILES_MAX);
        if (c->file_count < 0)
            c->file_count = 0;
    } else {
        for (unsigned i = 0; i < g_site_file_count && i < FILES_MAX; i++) {
            bfs_dir_entry_t *e = &c->files[c->file_count++];
            memset(e, 0, sizeof(*e));
            str_copy(e->name, g_site_files[i].path, sizeof(e->name));
            e->size = g_site_files[i].size;
            e->html = (int)g_site_files[i].html;
        }
    }
}

static void view_load(client_t *c, const char *path)
{
    uint32_t len = 0;
    int found = 0;
    char fsp[TW_PATH];

    c->view_len = 0;
    c->scroll = 0;
    str_copy(c->path, path, sizeof(c->path));
    fs_name(path, fsp, sizeof(fsp));

    if (bfs_mounted() && bfs_read(fsp, c->view, VIEW_CAP, &len) == 0) {
        c->view_len = (int)len;
        found = 1;
    } else {
        for (unsigned i = 0; i < g_site_file_count; i++) {
            if (str_eq_ci(g_site_files[i].path, path)) {
                unsigned n = g_site_files[i].size;
                if (n >= VIEW_CAP)
                    n = VIEW_CAP - 1;
                memcpy(c->view, g_site_files[i].data, n);
                c->view_len = (int)n;
                found = 1;
                break;
            }
        }
    }

    if (!found) {
        char msg[160];
        format_buf(msg, sizeof(msg), "File not found: %s\n", path);
        str_copy(c->view, msg, VIEW_CAP);
        c->view_len = (int)kstrlen(c->view);
    }
    c->view[c->view_len] = 0;
}

static char g_br_lines[BR_MAX_LINES][BR_MAX_COLS + 1];
static uint8_t g_br_style[BR_MAX_LINES];
static int g_br_line_count;
static int g_br_cur;
static int g_br_col;
static int g_br_render_cols = 74;
static int g_br_rows;
static uint8_t g_br_current_style;
static char g_br_src[BR_SRC_CAP];
static char g_br_path[TW_PATH];
static char g_br_status[128];
static char g_br_hist[BR_HIST][TW_PATH];
static int g_br_hist_count;
static char g_br_href[BR_MAX_LINKS][TW_PATH];
static int g_br_link_count;
static int g_br_prompt;
static char g_br_input[TW_PATH];
static int g_br_input_len;

static char br_lower(char c)
{
    if (c >= 'A' && c <= 'Z')
        return (char)(c + 32);
    return c;
}

static int br_starts_ci(const char *s, const char *prefix)
{
    while (*prefix) {
        if (br_lower(*s++) != br_lower(*prefix++))
            return 0;
    }
    return 1;
}

static void br_path_dir(const char *path, char *out, size_t cap)
{
    const char *slash = 0;
    for (const char *p = path; *p; p++) {
        if (*p == '/')
            slash = p;
    }
    if (!slash) {
        out[0] = 0;
        return;
    }
    size_t n = (size_t)(slash - path) + 1;
    if (n >= cap)
        n = cap - 1;
    memcpy(out, path, n);
    out[n] = 0;
}

static void br_normalize(const char *in, char *out, size_t cap)
{
    while (*in == ' ' || *in == '\t')
        in++;
    if (br_starts_ci(in, "byteos://"))
        in += 9;
    while (*in == '/')
        in++;
    if (!*in)
        in = "www/index.html";
    str_copy(out, in, cap);
}

static void br_resolve(const char *href, char *out, size_t cap)
{
    if (br_starts_ci(href, "http://") || br_starts_ci(href, "https://")) {
        str_copy(out, href, cap);
        return;
    }
    if (br_starts_ci(href, "byteos://")) {
        br_normalize(href, out, cap);
        return;
    }
    if (href[0] == '/') {
        br_normalize(href, out, cap);
        return;
    }
    if (br_starts_ci(href, "www/") || br_starts_ci(href, "home/") ||
        br_starts_ci(href, "etc/") || br_starts_ci(href, "README")) {
        str_copy(out, href, cap);
        return;
    }
    char dir[TW_PATH];
    br_path_dir(g_br_path, dir, sizeof(dir));
    size_t n = 0;
    while (dir[n] && n + 1 < cap) {
        out[n] = dir[n];
        n++;
    }
    const char *p = href;
    while (*p && n + 1 < cap)
        out[n++] = *p++;
    out[n] = 0;
}

static void br_reset(void)
{
    g_br_line_count = 1;
    g_br_cur = 0;
    g_br_col = 0;
    g_br_current_style = 0;
    g_br_lines[0][0] = 0;
    g_br_style[0] = 0;
}

static void br_newline(void)
{
    if (g_br_cur + 1 >= BR_MAX_LINES)
        return;
    g_br_cur++;
    g_br_col = 0;
    g_br_lines[g_br_cur][0] = 0;
    g_br_style[g_br_cur] = g_br_current_style;
    if (g_br_cur + 1 > g_br_line_count)
        g_br_line_count = g_br_cur + 1;
}

static void br_blank(void)
{
    if (g_br_col > 0)
        br_newline();
    if (g_br_cur > 0 && g_br_lines[g_br_cur - 1][0] != 0)
        br_newline();
}

static void br_raw(char c)
{
    if (g_br_col >= g_br_render_cols) {
        int split = -1;
        for (int i = g_br_col - 1; i > 0; i--) {
            if (g_br_lines[g_br_cur][i] == ' ') {
                split = i;
                break;
            }
        }
        if (split > 0) {
            char word[BR_MAX_COLS + 1];
            int n = g_br_col - split - 1;
            for (int i = 0; i < n; i++)
                word[i] = g_br_lines[g_br_cur][split + 1 + i];
            word[n] = 0;
            g_br_lines[g_br_cur][split] = 0;
            br_newline();
            for (int i = 0; i < n; i++)
                g_br_lines[g_br_cur][i] = word[i];
            g_br_col = n;
        } else {
            br_newline();
        }
    }
    g_br_lines[g_br_cur][g_br_col++] = c;
    g_br_lines[g_br_cur][g_br_col] = 0;
    g_br_style[g_br_cur] = g_br_current_style;
    if (g_br_cur + 1 > g_br_line_count)
        g_br_line_count = g_br_cur + 1;
}

static void br_char(char c)
{
    if (g_br_current_style == 4) {
        if (c == '\n')
            br_newline();
        else if (c == '\t') {
            for (int i = 0; i < 4; i++)
                br_raw(' ');
        } else {
            br_raw(c);
        }
        return;
    }
    if (c == '\n' || c == '\r' || c == '\t' || c == ' ') {
        if (g_br_col > 0 && g_br_lines[g_br_cur][g_br_col - 1] != ' ')
            br_raw(' ');
        return;
    }
    br_raw(c);
}

static void br_text(const char *s)
{
    while (*s)
        br_char(*s++);
}

static int br_parse_tag(const char *s, int *idx, char *name, size_t ncap,
                        char *href, size_t hcap, int *closing)
{
    int i = *idx;
    if (s[i] != '<')
        return 0;
    i++;
    while (s[i] == ' ')
        i++;
    *closing = 0;
    if (s[i] == '/') {
        *closing = 1;
        i++;
    }
    int n = 0;
    while (s[i] && s[i] != '>' && s[i] != ' ' && s[i] != '\t' &&
           s[i] != '/') {
        if (n + 1 < (int)ncap)
            name[n++] = br_lower(s[i]);
        i++;
    }
    name[n] = 0;
    href[0] = 0;

    while (s[i] && s[i] != '>') {
        while (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')
            i++;
        int a = 0;
        char attr[16];
        while (s[i] && s[i] != '=' && s[i] != '>' && s[i] != ' ' &&
               s[i] != '\t') {
            if (a + 1 < (int)sizeof(attr))
                attr[a++] = br_lower(s[i]);
            i++;
        }
        attr[a] = 0;
        while (s[i] == ' ')
            i++;
        if (s[i] == '=') {
            i++;
            while (s[i] == ' ')
                i++;
            char quote = 0;
            if (s[i] == '"' || s[i] == '\'')
                quote = s[i++];
            int h = 0;
            while (s[i] && (quote ? s[i] != quote : s[i] != '>' &&
                            s[i] != ' ' && s[i] != '\t')) {
                if (str_eq_ci(attr, "href") && h + 1 < (int)hcap)
                    href[h++] = s[i];
                i++;
            }
            if (quote && s[i] == quote)
                i++;
            href[h] = 0;
        }
        if (s[i] == '>')
            break;
        if (!s[i])
            break;
        if (a == 0)
            i++;
    }
    if (s[i] == '>')
        i++;
    *idx = i;
    return 1;
}

static void br_skip_tag(const char *s, int *idx, const char *name)
{
    int i = *idx;
    while (s[i]) {
        if (s[i] == '<') {
            char tag[16], href[8];
            int closing = 0;
            int j = i;
            if (br_parse_tag(s, &j, tag, sizeof(tag), href, sizeof(href),
                             &closing)) {
                if (closing && str_eq_ci(tag, name)) {
                    *idx = j;
                    return;
                }
                i = j;
                continue;
            }
        }
        i++;
    }
    *idx = i;
}

static void br_add_link(const char *href)
{
    if (g_br_link_count >= BR_MAX_LINKS || !href[0])
        return;
    int idx = g_br_link_count++;
    str_copy(g_br_href[idx], href, sizeof(g_br_href[idx]));

    char marker[8];
    marker[0] = ' ';
    marker[1] = '[';
    marker[2] = (char)('0' + (idx + 1) % 10);
    marker[3] = ']';
    marker[4] = 0;
    if (idx < 9)
        br_text(marker);
}

static void br_render_html(const char *s)
{
    br_reset();
    g_br_link_count = 0;
    int i = 0;
    int in_link = 0;
    char link_href[TW_PATH];
    link_href[0] = 0;

    while (s[i]) {
        if (s[i] == '<') {
            if (br_starts_ci(s + i, "<!--")) {
                i += 4;
                while (s[i] && !(s[i] == '-' && s[i + 1] == '-' &&
                                 s[i + 2] == '>'))
                    i++;
                if (s[i])
                    i += 3;
                continue;
            }
            if (s[i + 1] == '!' || s[i + 1] == '?') {
                while (s[i] && s[i] != '>')
                    i++;
                if (s[i])
                    i++;
                continue;
            }

            char tag[16], href[TW_PATH];
            int closing = 0;
            if (!br_parse_tag(s, &i, tag, sizeof(tag), href, sizeof(href),
                              &closing))
                continue;

            if (!closing && (str_eq_ci(tag, "script") ||
                             str_eq_ci(tag, "style") ||
                             str_eq_ci(tag, "title"))) {
                br_skip_tag(s, &i, tag);
                continue;
            }

            if (str_eq_ci(tag, "a")) {
                if (!closing) {
                    str_copy(link_href, href, sizeof(link_href));
                    in_link = 1;
                } else if (in_link) {
                    br_add_link(link_href);
                    in_link = 0;
                    link_href[0] = 0;
                }
            } else if (str_eq_ci(tag, "h1")) {
                if (!closing) {
                    br_blank();
                    g_br_current_style = 1;
                } else {
                    g_br_current_style = 0;
                    br_newline();
                }
            } else if (str_eq_ci(tag, "h2") || str_eq_ci(tag, "h3")) {
                if (!closing) {
                    br_blank();
                    g_br_current_style = 2;
                } else {
                    g_br_current_style = 0;
                    br_newline();
                }
            } else if (str_eq_ci(tag, "p") || str_eq_ci(tag, "div") ||
                       str_eq_ci(tag, "section") || str_eq_ci(tag, "main") ||
                       str_eq_ci(tag, "header") || str_eq_ci(tag, "footer") ||
                       str_eq_ci(tag, "nav")) {
                if (!closing)
                    br_blank();
                else
                    br_newline();
            } else if (str_eq_ci(tag, "br")) {
                br_newline();
            } else if (str_eq_ci(tag, "hr")) {
                br_blank();
                g_br_current_style = 3;
                for (int k = 0; k < 60; k++)
                    br_raw('-');
                g_br_current_style = 0;
                br_newline();
                br_newline();
            } else if (str_eq_ci(tag, "li")) {
                if (!closing) {
                    br_blank();
                    br_text(" * ");
                } else {
                    br_newline();
                }
            } else if (str_eq_ci(tag, "ul") || str_eq_ci(tag, "ol")) {
                br_blank();
            } else if (str_eq_ci(tag, "pre")) {
                if (!closing) {
                    br_blank();
                    g_br_current_style = 4;
                } else {
                    g_br_current_style = 0;
                    br_newline();
                    br_newline();
                }
            } else if (str_eq_ci(tag, "tr")) {
                br_newline();
            } else if (str_eq_ci(tag, "td") || str_eq_ci(tag, "th")) {
                br_text("  |  ");
            } else if (str_eq_ci(tag, "img")) {
                br_text("[image] ");
            }
            continue;
        }

        if (s[i] == '&') {
            char ent[12];
            int n = 0;
            int j = i + 1;
            while (s[j] && s[j] != ';' && n < 11)
                ent[n++] = br_lower(s[j++]);
            ent[n] = 0;
            if (s[j] == ';') {
                if (str_eq_ci(ent, "amp"))
                    br_char('&');
                else if (str_eq_ci(ent, "lt"))
                    br_char('<');
                else if (str_eq_ci(ent, "gt"))
                    br_char('>');
                else if (str_eq_ci(ent, "quot"))
                    br_char('"');
                else if (str_eq_ci(ent, "nbsp"))
                    br_char(' ');
                else if (str_eq_ci(ent, "apos") || str_eq_ci(ent, "#39"))
                    br_char('\'');
                else
                    br_char('?');
                i = j + 1;
                continue;
            }
        }

        br_char(s[i++]);
    }

    if (in_link)
        br_add_link(link_href);
    br_newline();
}

static int br_read_file(const char *path, char *buf, size_t cap,
                        uint32_t *out_len)
{
    char fsp[TW_PATH];
    fs_name(path, fsp, sizeof(fsp));
    if (bfs_mounted() && bfs_read(fsp, buf, (uint32_t)cap - 1, out_len) == 0)
        return 0;

    for (unsigned i = 0; i < g_site_file_count; i++) {
        char alt[TW_PATH];
        format_buf(alt, sizeof(alt), "/%s", g_site_files[i].path);
        if (str_eq_ci(path, g_site_files[i].path) || str_eq_ci(path, alt)) {
            unsigned n = g_site_files[i].size;
            if (n >= cap)
                n = (unsigned)cap - 1;
            memcpy(buf, g_site_files[i].data, n);
            buf[n] = 0;
            *out_len = n;
            return 0;
        }
    }
    return -1;
}

static int br_load(const char *path, int push)
{
    char norm[TW_PATH];
    br_normalize(path, norm, sizeof(norm));

    if (br_starts_ci(norm, "http://") || br_starts_ci(norm, "https://")) {
        format_buf(g_br_status, sizeof(g_br_status), "No network: %s", norm);
        set_status("No network stack");
        return -1;
    }

    uint32_t len = 0;
    int ok = br_read_file(norm, g_br_src, sizeof(g_br_src), &len) == 0;
    if (ok) {
        g_br_src[len] = 0;
    } else {
        format_buf(g_br_src, sizeof(g_br_src),
                   "<h1>Not found</h1><p>%s</p>"
                   "<p>Press u to open another page.</p>", norm);
        format_buf(g_br_status, sizeof(g_br_status), "Cannot open %s", norm);
    }

    if (push && g_br_path[0] && g_br_hist_count < BR_HIST)
        str_copy(g_br_hist[g_br_hist_count++], g_br_path, TW_PATH);
    str_copy(g_br_path, norm, sizeof(g_br_path));
    g_br_status[0] = 0;
    if (!ok)
        format_buf(g_br_status, sizeof(g_br_status), "Cannot open %s", norm);
    br_render_html(g_br_src);
    for (int i = 0; i < TW_MAX; i++) {
        if (g_clients[i].used && g_clients[i].type == CLI_BROWSER)
            g_clients[i].scroll = 0;
    }
    return ok ? 0 : -1;
}

static void term_welcome(client_t *c)
{
    term_fmt(c, TC_GOOD, "ByteTile - ByteOS tiling window manager");
    term_fmt(c, TC_DIM, "Super+Return terminal  Super+Space launcher  Super+Q close");
    term_fmt(c, TC_DIM, "Alt+Return terminal  Alt+p launcher  Alt+Shift+C config");
    term_fmt(c, TC_DIM, "Alt+j/k focus  Alt+Shift+j/k move  Alt+h/l master");
    term_fmt(c, TC_DIM, "Alt+Space layout  Alt+f zoom  Alt+1..9 tags  Alt+q close");
    term_fmt(c, TC_NORM, "");
    term_fmt(c, TC_NORM, "Type help for commands.");
    term_fmt(c, TC_NORM, "");
}

static int client_open(int type, const char *arg)
{
    if (type == CLI_CFG || type == CLI_BROWSER) {
        for (int i = 0; i < TW_MAX; i++) {
            if (g_clients[i].used && g_clients[i].type == type) {
                g_focus = i;
                if (type == CLI_BROWSER && arg && *arg)
                    br_load(arg, 0);
                return i;
            }
        }
    }

    int slot = -1;
    for (int i = 0; i < TW_MAX; i++) {
        if (!g_clients[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        set_status("Window limit reached (8)");
        return -1;
    }

    client_t *c = &g_clients[slot];
    memset(c, 0, sizeof(*c));
    c->used = 1;
    c->type = type;
    c->tags = 1u << g_tag;
    c->hist_pos = 0;
    g_focus = slot;

    switch (type) {
    case CLI_TERM:
        str_copy(c->title, "terminal", sizeof(c->title));
        str_copy(c->cwd, "/", sizeof(c->cwd));
        term_welcome(c);
        break;
    case CLI_FILES:
        str_copy(c->title, "files", sizeof(c->title));
        str_copy(c->path, "/", sizeof(c->path));
        files_refresh(c);
        break;
    case CLI_SYS:
        str_copy(c->title, "system monitor", sizeof(c->title));
        break;
    case CLI_VIEW:
        str_copy(c->title, arg && *arg ? arg : "viewer", sizeof(c->title));
        view_load(c, arg && *arg ? arg : "bytetile.conf");
        break;
    case CLI_CFG:
        str_copy(c->title, "bytetile.conf", sizeof(c->title));
        break;
    case CLI_KEYS:
        str_copy(c->title, "key reference", sizeof(c->title));
        break;
    case CLI_BROWSER:
        str_copy(c->title, "browser", sizeof(c->title));
        br_load(arg && *arg ? arg : (g_br_path[0] ? g_br_path : "www/index.html"), 0);
        break;
    default:
        c->used = 0;
        return -1;
    }
    return slot;
}

static void client_close(int idx)
{
    if (idx < 0 || idx >= TW_MAX || !g_clients[idx].used)
        return;

    for (int i = idx; i < TW_MAX - 1; i++)
        g_clients[i] = g_clients[i + 1];
    memset(&g_clients[TW_MAX - 1], 0, sizeof(g_clients[0]));

    if (g_focus == idx) {
        g_focus = next_visible(1);
        if (g_focus < 0)
            g_focus = first_visible();
    } else if (g_focus > idx) {
        g_focus--;
    }
}

static void focus_tag(int tag)
{
    g_tag = clamp_int(tag, 0, 8);
    g_focus = first_visible();
    set_status("Tag %d", g_tag + 1);
}

static void move_to_tag(int tag)
{
    client_t *c = focused_client();
    if (!c) {
        set_status("No focused window");
        return;
    }
    tag = clamp_int(tag, 0, 8);
    c->tags = 1u << tag;
    set_status("Moved %s to tag %d", c->title, tag + 1);
    g_focus = first_visible();
}

static void move_client(int dir)
{
    if (g_focus < 0)
        return;
    int target = -1;
    for (int i = g_focus + dir; i >= 0 && i < TW_MAX; i += dir) {
        if (g_clients[i].used) {
            target = i;
            break;
        }
    }
    if (target < 0)
        return;
    client_t tmp = g_clients[g_focus];
    g_clients[g_focus] = g_clients[target];
    g_clients[target] = tmp;
    g_focus = target;
}

static char g_scratch[VIEW_CAP];
static bfs_dir_entry_t g_dir_scratch[FILES_MAX];

static void term_history_push(client_t *c, const char *cmd)
{
    if (!cmd[0])
        return;
    if (c->hist_count < TERM_HIST) {
        str_copy(c->hist[c->hist_count++], cmd, TERM_COLS + 1);
    } else {
        memmove(c->hist[0], c->hist[1],
                sizeof(c->hist[0]) * (TERM_HIST - 1));
        str_copy(c->hist[TERM_HIST - 1], cmd, TERM_COLS + 1);
    }
    c->hist_pos = c->hist_count;
}

static void term_history_move(client_t *c, int dir)
{
    if (c->hist_count == 0)
        return;
    int pos = c->hist_pos + dir;
    if (pos < 0)
        pos = 0;
    if (pos > c->hist_count)
        pos = c->hist_count;
    c->hist_pos = pos;
    if (pos == c->hist_count) {
        c->input[0] = 0;
        c->input_len = 0;
        return;
    }
    str_copy(c->input, c->hist[pos], sizeof(c->input));
    c->input_len = (int)kstrlen(c->input);
}

static void term_art(client_t *c)
{
    static const uint8_t art_colors[3] = { TC_GOOD, TC_ACC, TC_DIM };
    for (int i = 0; i < FETCH_ART_ROWS; i++)
        term_add(c, art_colors[i % 3], g_fetch_art[i]);
    term_fmt(c, TC_DIM, "ByteOS %s \"%s\" - %s",
             BYTEOS_VERSION, BYTEOS_CODENAME, g_cpu_brand);
}

static int term_read_file(const char *arg, const char *abs, uint32_t *out_len)
{
    uint32_t len = 0;
    char fsp[TW_PATH];
    fs_name(abs, fsp, sizeof(fsp));

    if (bfs_mounted() &&
        bfs_read(fsp, g_scratch, (uint32_t)sizeof(g_scratch) - 1, &len) == 0) {
        g_scratch[len] = 0;
        *out_len = len;
        return 0;
    }

    for (unsigned i = 0; i < g_site_file_count; i++) {
        char site[TW_PATH];
        format_buf(site, sizeof(site), "/%s", g_site_files[i].path);
        if (str_eq_ci(g_site_files[i].path, arg) ||
            str_eq_ci(site, abs)) {
            unsigned n = g_site_files[i].size;
            if (n >= sizeof(g_scratch))
                n = sizeof(g_scratch) - 1;
            memcpy(g_scratch, g_site_files[i].data, n);
            g_scratch[n] = 0;
            *out_len = n;
            return 0;
        }
    }
    return -1;
}

static void term_cat(client_t *c, const char *arg, const char *abs)
{
    uint32_t len = 0;
    if (bfs_mounted() && bfs_is_dir(abs)) {
        term_fmt(c, TC_ERR, "cat: %s: is a directory", abs);
        return;
    }
    if (term_read_file(arg, abs, &len) != 0) {
        term_fmt(c, TC_ERR, "cat: %s: not found", abs);
        return;
    }

    char *p = g_scratch;
    while (*p) {
        char *nl = p;
        while (*nl && *nl != '\n')
            nl++;
        char save = *nl;
        *nl = 0;
        term_fmt(c, TC_NORM, "%s", p);
        if (!save)
            break;
        p = nl + 1;
    }
}

static void term_ls(client_t *c, const char *abs)
{
    if (!bfs_mounted()) {
        for (unsigned i = 0; i < g_site_file_count; i++)
            term_fmt(c, TC_DIM, "  %s  (%u bytes)",
                     g_site_files[i].path, g_site_files[i].size);
        return;
    }

    int n = bfs_list_dir(abs, g_dir_scratch, FILES_MAX);
    for (int i = 0; i < n; i++) {
        bfs_dir_entry_t *e = &g_dir_scratch[i];
        if (e->is_dir)
            term_fmt(c, TC_ACC, "  %s/", e->name);
        else if (e->html)
            term_fmt(c, TC_GOOD, "  %s  (%u bytes, html)", e->name, e->size);
        else
            term_fmt(c, TC_NORM, "  %s  (%u bytes)", e->name, e->size);
    }
    if (n == 0)
        term_fmt(c, TC_DIM, "  (empty)");
}

static void term_tags(client_t *c)
{
    for (int t = 0; t < 9; t++) {
        int count = 0;
        for (int i = 0; i < TW_MAX; i++) {
            if (g_clients[i].used && (g_clients[i].tags & (1u << t)))
                count++;
        }
        term_fmt(c, t == g_tag ? TC_ACC : TC_DIM,
                 "  tag %d %s  %d window%s", t + 1,
                 t == g_tag ? "<current>" : "         ",
                 count, count == 1 ? "" : "s");
    }
}

extern uint8_t __bss_end[];

static const char *term_base(const char *p)
{
    const char *base = p;
    for (; *p; p++) {
        if (*p == '/')
            base = p + 1;
    }
    return base;
}

static int term_split2(char *s, char **a, char **b)
{
    *a = s;
    while (*s && *s != ' ' && *s != '\t')
        s++;
    if (!*s)
        return 0;
    *s++ = 0;
    while (*s == ' ' || *s == '\t')
        s++;
    if (!*s)
        return 0;
    *b = s;
    while (*s && *s != ' ' && *s != '\t')
        s++;
    *s = 0;
    return 1;
}

static char term_lower(char c)
{
    if (c >= 'A' && c <= 'Z')
        return (char)(c + 32);
    return c;
}

static int term_contains_ci(const char *hay, const char *needle)
{
    if (!*needle)
        return 1;
    for (; *hay; hay++) {
        const char *h = hay;
        const char *n = needle;
        while (*h && *n && term_lower(*h) == term_lower(*n)) {
            h++;
            n++;
        }
        if (!*n)
            return 1;
    }
    return 0;
}

static void term_usage(client_t *c, const char *usage)
{
    term_fmt(c, TC_ERR, "usage: %s", usage);
}

static void term_mkdir(client_t *c, const char *arg)
{
    if (!bfs_mounted()) {
        term_fmt(c, TC_ERR, "mkdir: read-only live ISO");
        return;
    }
    if (!arg || !*arg) {
        term_usage(c, "mkdir <directory>");
        return;
    }
    char abs[TW_PATH];
    char fsp[TW_PATH];
    path_normalize(c->cwd, arg, abs, sizeof(abs));
    if (str_eq(abs, "/")) {
        term_fmt(c, TC_ERR, "mkdir: /: exists");
        return;
    }
    fs_name(abs, fsp, sizeof(fsp));
    if (bfs_mkdir(fsp) != 0)
        term_fmt(c, TC_ERR, "mkdir: %s: exists or parent missing", abs);
}

static void term_rmdir(client_t *c, const char *arg)
{
    if (!bfs_mounted()) {
        term_fmt(c, TC_ERR, "rmdir: read-only live ISO");
        return;
    }
    if (!arg || !*arg) {
        term_usage(c, "rmdir <directory>");
        return;
    }
    char abs[TW_PATH];
    char fsp[TW_PATH];
    path_normalize(c->cwd, arg, abs, sizeof(abs));
    fs_name(abs, fsp, sizeof(fsp));
    int rc = bfs_rmdir(fsp);
    if (rc == -2)
        term_fmt(c, TC_ERR, "rmdir: %s: not a directory", abs);
    else if (rc == -3)
        term_fmt(c, TC_ERR, "rmdir: %s: directory not empty", abs);
    else if (rc != 0)
        term_fmt(c, TC_ERR, "rmdir: %s: cannot remove", abs);
}

static void term_touch(client_t *c, const char *arg)
{
    if (!bfs_mounted()) {
        term_fmt(c, TC_ERR, "touch: read-only live ISO");
        return;
    }
    if (!arg || !*arg) {
        term_usage(c, "touch <file>");
        return;
    }
    char abs[TW_PATH];
    char fsp[TW_PATH];
    path_normalize(c->cwd, arg, abs, sizeof(abs));
    if (bfs_is_dir(abs)) {
        term_fmt(c, TC_ERR, "touch: %s: is a directory", abs);
        return;
    }
    fs_name(abs, fsp, sizeof(fsp));
    if (!bfs_exists(fsp)) {
        if (bfs_add(fsp, "", 0, bfs_is_html(abs)) != 0)
            term_fmt(c, TC_ERR, "touch: %s: cannot create file", abs);
    }
}

static void term_rm(client_t *c, const char *arg)
{
    if (!bfs_mounted()) {
        term_fmt(c, TC_ERR, "rm: read-only live ISO");
        return;
    }
    if (!arg || !*arg) {
        term_usage(c, "rm <file>");
        return;
    }
    char abs[TW_PATH];
    char fsp[TW_PATH];
    path_normalize(c->cwd, arg, abs, sizeof(abs));
    fs_name(abs, fsp, sizeof(fsp));
    if (!bfs_exists(fsp)) {
        term_fmt(c, TC_ERR, "rm: %s: no such file", abs);
        return;
    }
    int rc = bfs_unlink(fsp);
    if (rc == -2)
        term_fmt(c, TC_ERR, "rm: %s: is a directory (use rmdir)", abs);
    else if (rc != 0)
        term_fmt(c, TC_ERR, "rm: %s: cannot remove", abs);
}

static void term_copy_move(client_t *c, char *arg, int move)
{
    const char *name = move ? "mv" : "cp";
    if (!bfs_mounted()) {
        term_fmt(c, TC_ERR, "%s: read-only live ISO", name);
        return;
    }
    char *src;
    char *dst;
    if (!term_split2(arg, &src, &dst)) {
        term_usage(c, move ? "mv <source> <dest>" : "cp <source> <dest>");
        return;
    }

    char sabs[TW_PATH];
    char dabs[TW_PATH];
    char sfs[TW_PATH];
    char dfs[TW_PATH];
    path_normalize(c->cwd, src, sabs, sizeof(sabs));
    path_normalize(c->cwd, dst, dabs, sizeof(dabs));
    fs_name(sabs, sfs, sizeof(sfs));
    fs_name(dabs, dfs, sizeof(dfs));

    if (bfs_is_dir(sabs)) {
        term_fmt(c, TC_ERR, "%s: %s: is a directory", name, sabs);
        return;
    }
    if (move && !bfs_exists(sfs)) {
        term_fmt(c, TC_ERR, "mv: %s: no such file", sabs);
        return;
    }
    if (bfs_is_dir(dabs)) {
        char joined[TW_PATH];
        size_t n = kstrlen(dabs);
        if (n > 0 && dabs[n - 1] == '/')
            format_buf(joined, sizeof(joined), "%s%s", dabs, term_base(sabs));
        else
            format_buf(joined, sizeof(joined), "%s/%s", dabs, term_base(sabs));
        str_copy(dabs, joined, sizeof(dabs));
        fs_name(dabs, dfs, sizeof(dfs));
    }
    if (str_eq(sabs, dabs))
        return;

    uint32_t len = 0;
    if (term_read_file(src, sabs, &len) != 0) {
        term_fmt(c, TC_ERR, "%s: %s: not found", name, sabs);
        return;
    }

    int rc;
    if (bfs_exists(dfs))
        rc = bfs_write(dfs, g_scratch, len);
    else
        rc = bfs_add(dfs, g_scratch, len, bfs_is_html(dabs));
    if (rc != 0) {
        term_fmt(c, TC_ERR, "%s: %s: cannot write", name, dabs);
        return;
    }
    if (move)
        (void)bfs_unlink(sfs);
}

static void term_headtail(client_t *c, char *arg, int tail)
{
    if (!*arg) {
        term_usage(c, tail ? "tail <file>" : "head <file>");
        return;
    }
    char abs[TW_PATH];
    uint32_t len = 0;
    path_normalize(c->cwd, arg, abs, sizeof(abs));
    if (term_read_file(arg, abs, &len) != 0) {
        term_fmt(c, TC_ERR, "%s: %s: not found", tail ? "tail" : "head", abs);
        return;
    }

    int total = 0;
    for (uint32_t i = 0; i < len; i++) {
        if (g_scratch[i] == '\n')
            total++;
    }
    if (len > 0 && g_scratch[len - 1] != '\n')
        total++;

    int start = tail && total > 10 ? total - 10 : 0;
    int line = 0;
    int drawn = 0;
    char *p = g_scratch;
    while (*p && drawn < 10) {
        char *nl = p;
        while (*nl && *nl != '\n')
            nl++;
        char save = *nl;
        *nl = 0;
        if (line >= start) {
            term_fmt(c, TC_NORM, "%s", p);
            drawn++;
        }
        line++;
        if (!save)
            break;
        p = nl + 1;
    }
}

static void term_wc(client_t *c, char *arg)
{
    if (!*arg) {
        term_usage(c, "wc <file>");
        return;
    }
    char abs[TW_PATH];
    uint32_t len = 0;
    path_normalize(c->cwd, arg, abs, sizeof(abs));
    if (term_read_file(arg, abs, &len) != 0) {
        term_fmt(c, TC_ERR, "wc: %s: not found", abs);
        return;
    }

    int lines = 0;
    int words = 0;
    int inword = 0;
    for (uint32_t i = 0; i < len; i++) {
        char ch = g_scratch[i];
        if (ch == '\n')
            lines++;
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
            inword = 0;
        } else if (!inword) {
            words++;
            inword = 1;
        }
    }
    term_fmt(c, TC_NORM, "%d %d %u %s", lines, words, (unsigned)len, abs);
}

static void term_grep(client_t *c, char *arg)
{
    char *pattern;
    char *file;
    if (!term_split2(arg, &pattern, &file)) {
        term_usage(c, "grep <pattern> <file>");
        return;
    }
    char abs[TW_PATH];
    uint32_t len = 0;
    path_normalize(c->cwd, file, abs, sizeof(abs));
    if (term_read_file(file, abs, &len) != 0) {
        term_fmt(c, TC_ERR, "grep: %s: not found", abs);
        return;
    }

    int line_no = 1;
    int hits = 0;
    char *p = g_scratch;
    while (*p) {
        char *nl = p;
        while (*nl && *nl != '\n')
            nl++;
        char save = *nl;
        *nl = 0;
        if (term_contains_ci(p, pattern)) {
            term_fmt(c, TC_NORM, "%d: %s", line_no, p);
            hits++;
        }
        line_no++;
        if (!save)
            break;
        p = nl + 1;
    }
    if (hits == 0)
        term_fmt(c, TC_DIM, "grep: no matches");
}

static void term_lsblk(client_t *c)
{
    term_fmt(c, TC_ACC, "NAME    SIZE       TYPE  MODEL");
    int mounted = bfs_mounted() ? bfs_slot() : -1;
    int any = 0;
    for (int i = 0; i < ATA_MAX_DISKS; i++) {
        ata_disk_t *d = ata_slot(i);
        if (!d)
            continue;
        any = 1;
        char name[8];
        format_buf(name, sizeof(name), "sd%c", (char)('a' + i));
        uint64_t mb = (uint64_t)d->sectors * 512ULL / (1024ULL * 1024ULL);
        term_fmt(c, TC_NORM, "%s  %llu MB  disk  %s", name,
                 (unsigned long long)mb,
                 d->model[0] ? d->model : "ATA disk");
        if (i == mounted) {
            uint64_t part = (uint64_t)(d->sectors - BFS_PART_LBA) * 512ULL /
                            (1024ULL * 1024ULL);
            term_fmt(c, TC_DIM, "`-%s1  %llu MB  part  ByteFS", name,
                     (unsigned long long)part);
        }
    }
    if (!any)
        term_fmt(c, TC_ACC, "  no ATA disks detected");
}

static void term_df(client_t *c)
{
    if (bfs_mounted()) {
        ata_disk_t *d = ata_slot(bfs_slot());
        uint64_t total = d ? (uint64_t)(d->sectors - BFS_PART_LBA) * 512ULL : 0;
        uint64_t used = bfs_used_bytes();
        uint64_t avail = total > used ? total - used : 0;
        term_fmt(c, TC_ACC, "Filesystem  1K-blocks      Used  Available  Mounted");
        term_fmt(c, TC_NORM, "/dev/sd%c1  %10llu  %8llu  %9llu  /",
                 (char)('a' + bfs_slot()),
                 (unsigned long long)(total / 1024ULL),
                 (unsigned long long)(used / 1024ULL),
                 (unsigned long long)(avail / 1024ULL));
    } else {
        term_fmt(c, TC_ACC, "Filesystem  1K-blocks      Used  Available  Mounted");
        term_fmt(c, TC_DIM, "romfs              0         0          0  / (live ISO)");
    }
}

static void term_free(client_t *c)
{
    uint64_t total = g_mem_usable;
    uint64_t used = (uint64_t)((uintptr_t)__bss_end - 0x10000ULL);
    if (used > total)
        used = total;
    uint64_t freeb = total - used;
    term_fmt(c, TC_ACC, "              total        used        free");
    term_fmt(c, TC_NORM, "Mem:      %9lluK %10lluK %10lluK",
             (unsigned long long)(total / 1024ULL),
             (unsigned long long)(used / 1024ULL),
             (unsigned long long)(freeb / 1024ULL));
    term_fmt(c, TC_DIM, "Swap:               0          0          0");
}

static void term_uname(client_t *c, const char *arg)
{
    if (str_eq_ci(arg, "-a"))
        term_fmt(c, TC_NORM, "ByteOS %s 0.2 %s #1 x86_64 GNU/ByteOS",
                 auth_hostname(), BYTEOS_CODENAME);
    else if (str_eq_ci(arg, "-r"))
        term_fmt(c, TC_NORM, "0.2");
    else if (str_eq_ci(arg, "-m"))
        term_fmt(c, TC_NORM, "x86_64");
    else if (str_eq_ci(arg, "-n"))
        term_fmt(c, TC_NORM, "%s", auth_hostname());
    else if (str_eq_ci(arg, "-s"))
        term_fmt(c, TC_NORM, "ByteOS");
    else
        term_fmt(c, TC_NORM, "ByteOS %s x86_64", auth_hostname());
}

static void term_uptime(client_t *c)
{
    uint64_t up = 0;
    if (g_tsc_hz && read_tsc() > g_boot_tsc)
        up = (read_tsc() - g_boot_tsc) / g_tsc_hz;
    uint64_t days = up / 86400ULL;
    if (days)
        term_fmt(c, TC_NORM, " %llu day%s, %llu:%02llu up, 1 user",
                 (unsigned long long)days, days == 1 ? "" : "s",
                 (unsigned long long)((up / 3600ULL) % 24ULL),
                 (unsigned long long)((up / 60ULL) % 60ULL));
    else
        term_fmt(c, TC_NORM, " %llu:%02llu up, 1 user, load average: 0.00",
                 (unsigned long long)(up / 3600ULL),
                 (unsigned long long)((up / 60ULL) % 60ULL));
}

static void term_specs(client_t *c)
{
    term_fmt(c, TC_ACC, "ByteOS system report");
    term_fmt(c, TC_NORM, "cpu      %s", g_cpu_brand);
    term_fmt(c, TC_NORM, "vendor   %s  family %u model %u step %u",
             g_cpu_vendor, g_cpu_family, g_cpu_model, g_cpu_stepping);
    term_fmt(c, TC_NORM, "cpus     %u logical%s", g_logical_cpus,
             g_hypervisor ? " (virtualized)" : "");
    term_fmt(c, TC_NORM, "tsc      %llu MHz",
             (unsigned long long)((g_tsc_hz + 500000ULL) / 1000000ULL));
    term_fmt(c, TC_NORM, "memory   %llu MB usable / %llu MB top",
             (unsigned long long)(g_mem_usable / (1024ULL * 1024ULL)),
             (unsigned long long)(g_mem_top / (1024ULL * 1024ULL)));
    term_fmt(c, TC_NORM, "kernel   ByteOS 0.2 Sprinkles x86_64");
    term_fmt(c, TC_DIM, "boot     %s", g_boot_ok ? "BIOS / El Torito" : "unknown");
}

static void term_memmap(client_t *c)
{
    uint32_t count = g_boot ? g_boot->e820_count : g_e820_count;
    term_fmt(c, TC_ACC, "BIOS E820 memory map (%u entries)", count);
    for (uint32_t i = 0; i < count; i++) {
        e820_entry_t *e = &g_boot->entries[i];
        const char *kind = "unknown";
        if (e->type == 1)
            kind = "usable";
        else if (e->type == 2)
            kind = "reserved";
        else if (e->type == 3)
            kind = "acpi reclaim";
        else if (e->type == 4)
            kind = "acpi nvs";
        else if (e->type == 5)
            kind = "bad";
        term_fmt(c, TC_NORM, " %08llx-%08llx %8llu MB %s",
                 (unsigned long long)e->base,
                 (unsigned long long)(e->base + e->length),
                 (unsigned long long)(e->length / (1024ULL * 1024ULL)), kind);
    }
}

static void term_about(client_t *c)
{
    term_fmt(c, TC_ACC, "ByteOS 0.2 \"Sprinkles\"");
    term_fmt(c, TC_NORM, "A tiny 64-bit bare-metal operating system.");
    term_fmt(c, TC_NORM, "ByteTile is the dwm-style tiling desktop.");
    term_fmt(c, TC_DIM, "github.com/withdrawment/byteos");
}

static void term_env(client_t *c)
{
    term_fmt(c, TC_NORM, "USER=%s", auth_username());
    term_fmt(c, TC_NORM, "HOME=/home");
    term_fmt(c, TC_NORM, "PWD=%s", c->cwd);
    term_fmt(c, TC_NORM, "HOSTNAME=%s", auth_hostname());
    term_fmt(c, TC_NORM, "SHELL=bytetile");
}

static void term_history(client_t *c)
{
    for (int i = 0; i < c->hist_count; i++)
        term_fmt(c, TC_NORM, "%2d  %s", i + 1, c->hist[i]);
    if (c->hist_count == 0)
        term_fmt(c, TC_DIM, "history: empty");
}

static const char *client_type_name(int type)
{
    switch (type) {
    case CLI_TERM:
        return "terminal";
    case CLI_FILES:
        return "files";
    case CLI_SYS:
        return "system";
    case CLI_VIEW:
        return "viewer";
    case CLI_CFG:
        return "config";
    case CLI_KEYS:
        return "keys";
    case CLI_BROWSER:
        return "browser";
    default:
        return "window";
    }
}

static void term_ps(client_t *c)
{
    term_fmt(c, TC_ACC, "  PID  TAG  CLIENT    TITLE");
    for (int i = 0; i < TW_MAX; i++) {
        client_t *w = &g_clients[i];
        if (!w->used)
            continue;
        int tag = 0;
        while (tag < 8 && !(w->tags & (1u << tag)))
            tag++;
        term_fmt(c, i == g_focus ? TC_ACC : TC_NORM, "  %3d  tag %d  %s  %s",
                 i + 1, tag + 1, client_type_name(w->type), w->title);
    }
}

static void term_tileconf(client_t *c)
{
    term_fmt(c, TC_ACC, "ByteTile config: /%s", CFG_FILE);
    char *p = g_cfg_buf;
    while (*p) {
        char *nl = p;
        while (*nl && *nl != '\n')
            nl++;
        char save = *nl;
        *nl = 0;
        term_fmt(c, TC_NORM, "%s", p);
        if (!save)
            break;
        p = nl + 1;
    }
}

static void term_help(client_t *c)
{
    term_fmt(c, TC_ACC, "commands:");
    term_fmt(c, TC_NORM, "  files   ls dir cd pwd cat type mkdir rmdir touch rm cp mv");
    term_fmt(c, TC_NORM, "          head tail wc grep less");
    term_fmt(c, TC_NORM, "  system  date time uptime free df lsblk uname hostname whoami");
    term_fmt(c, TC_NORM, "          id env ps top");
    term_fmt(c, TC_NORM, "  apps    open browse edit keys tags");
    term_fmt(c, TC_NORM, "  byteos  fetch specs memmap about version tileconf reboot");
    term_fmt(c, TC_NORM, "  shell   history echo clear help man exit");
    term_fmt(c, TC_DIM, "Window keys: Super+Return, Super+Space, Super+Q");
    term_fmt(c, TC_DIM, "Alt aliases: Alt+Return, Alt+p, Alt+j/k, Alt+q");
}

static void term_exec(client_t *c, char *line)
{
    char *s = trim_spaces(line);
    if (!*s)
        return;

    char *arg = s;
    while (*arg && *arg != ' ' && *arg != '\t')
        arg++;
    if (*arg) {
        *arg = 0;
        arg++;
        arg = trim_spaces(arg);
    }

    if (str_eq_ci(s, "help") || str_eq_ci(s, "man")) {
        term_help(c);
    } else if (str_eq_ci(s, "clear") || str_eq_ci(s, "cls")) {
        c->term_count = 0;
        c->scroll = 0;
    } else if (str_eq_ci(s, "ls") || str_eq_ci(s, "dir")) {
        char abs[TW_PATH];
        path_normalize(c->cwd, arg, abs, sizeof(abs));
        term_ls(c, abs);
    } else if (str_eq_ci(s, "cat") || str_eq_ci(s, "type")) {
        if (!*arg) {
            term_usage(c, "cat <file>");
        } else {
            char abs[TW_PATH];
            path_normalize(c->cwd, arg, abs, sizeof(abs));
            term_cat(c, arg, abs);
        }
    } else if (str_eq_ci(s, "cd") || str_eq_ci(s, "chdir")) {
        char abs[TW_PATH];
        path_normalize(c->cwd, arg, abs, sizeof(abs));
        if (bfs_mounted() && !bfs_is_dir(abs)) {
            term_fmt(c, TC_ERR, "cd: %s: not a directory", abs);
        } else {
            str_copy(c->cwd, abs, sizeof(c->cwd));
            term_fmt(c, TC_DIM, "cwd %s", c->cwd);
        }
    } else if (str_eq_ci(s, "pwd")) {
        term_fmt(c, TC_NORM, "%s", c->cwd);
    } else if (str_eq_ci(s, "echo")) {
        term_fmt(c, TC_NORM, "%s", arg);
    } else if (str_eq_ci(s, "date")) {
        rtc_time_t t;
        if (rtc_read(&t) == 0)
            term_fmt(c, TC_NORM, "%04d-%02d-%02d %02d:%02d:%02d",
                     t.year, t.month, t.day, t.hour, t.minute, t.second);
        else
            term_fmt(c, TC_ERR, "date: RTC unavailable");
    } else if (str_eq_ci(s, "time")) {
        rtc_time_t t;
        if (rtc_read(&t) == 0)
            term_fmt(c, TC_NORM, "%02d:%02d:%02d", t.hour, t.minute, t.second);
        else
            term_fmt(c, TC_ERR, "time: RTC unavailable");
    } else if (str_eq_ci(s, "uptime")) {
        term_uptime(c);
    } else if (str_eq_ci(s, "whoami")) {
        term_fmt(c, TC_NORM, "%s", auth_username());
    } else if (str_eq_ci(s, "id")) {
        term_fmt(c, TC_NORM, "user=%s root=%s host=%s", auth_username(),
                 auth_is_root() ? "yes" : "no", auth_hostname());
    } else if (str_eq_ci(s, "hostname")) {
        term_fmt(c, TC_NORM, "%s", auth_hostname());
    } else if (str_eq_ci(s, "uname")) {
        term_uname(c, arg);
    } else if (str_eq_ci(s, "free")) {
        term_free(c);
    } else if (str_eq_ci(s, "df")) {
        term_df(c);
    } else if (str_eq_ci(s, "lsblk")) {
        term_lsblk(c);
    } else if (str_eq_ci(s, "fetch")) {
        term_art(c);
    } else if (str_eq_ci(s, "specs") || str_eq_ci(s, "sysinfo")) {
        term_specs(c);
    } else if (str_eq_ci(s, "memmap") || str_eq_ci(s, "e820")) {
        term_memmap(c);
    } else if (str_eq_ci(s, "about")) {
        term_about(c);
    } else if (str_eq_ci(s, "version")) {
        term_fmt(c, TC_ACC, "ByteOS %s \"%s\"  /  x86_64",
                 BYTEOS_VERSION, BYTEOS_CODENAME);
    } else if (str_eq_ci(s, "env")) {
        term_env(c);
    } else if (str_eq_ci(s, "history")) {
        term_history(c);
    } else if (str_eq_ci(s, "ps")) {
        term_ps(c);
    } else if (str_eq_ci(s, "top")) {
        client_open(CLI_SYS, 0);
        term_fmt(c, TC_DIM, "system monitor opened");
    } else if (str_eq_ci(s, "tileconf")) {
        term_tileconf(c);
    } else if (str_eq_ci(s, "open") || str_eq_ci(s, "view")) {
        if (!*arg) {
            term_usage(c, "open <file>");
        } else {
            size_t n = kstrlen(arg);
            int html = (n >= 5 && str_eq_ci(arg + n - 5, ".html")) ||
                       (n >= 4 && str_eq_ci(arg + n - 4, ".htm"));
            client_open(html ? CLI_BROWSER : CLI_VIEW, arg);
            term_fmt(c, TC_DIM, "opened %s", arg);
        }
    } else if (str_eq_ci(s, "browse") || str_eq_ci(s, "browser")) {
        client_open(CLI_BROWSER, *arg ? arg : "www/index.html");
        term_fmt(c, TC_DIM, "browser opened");
    } else if (str_eq_ci(s, "less") || str_eq_ci(s, "more")) {
        if (!*arg) {
            term_usage(c, "less <file>");
        } else {
            client_open(CLI_VIEW, arg);
            term_fmt(c, TC_DIM, "opened %s", arg);
        }
    } else if (str_eq_ci(s, "edit") || str_eq_ci(s, "config")) {
        client_open(CLI_CFG, 0);
        term_fmt(c, TC_DIM, "config editor opened");
    } else if (str_eq_ci(s, "keys")) {
        client_open(CLI_KEYS, 0);
        term_fmt(c, TC_DIM, "key reference opened");
    } else if (str_eq_ci(s, "tags")) {
        term_tags(c);
    } else if (str_eq_ci(s, "mkdir")) {
        term_mkdir(c, arg);
    } else if (str_eq_ci(s, "rmdir")) {
        term_rmdir(c, arg);
    } else if (str_eq_ci(s, "touch")) {
        term_touch(c, arg);
    } else if (str_eq_ci(s, "rm")) {
        term_rm(c, arg);
    } else if (str_eq_ci(s, "cp")) {
        term_copy_move(c, arg, 0);
    } else if (str_eq_ci(s, "mv")) {
        term_copy_move(c, arg, 1);
    } else if (str_eq_ci(s, "head")) {
        term_headtail(c, arg, 0);
    } else if (str_eq_ci(s, "tail")) {
        term_headtail(c, arg, 1);
    } else if (str_eq_ci(s, "wc")) {
        term_wc(c, arg);
    } else if (str_eq_ci(s, "grep")) {
        term_grep(c, arg);
    } else if (str_eq_ci(s, "reboot")) {
        reboot();
    } else if (str_eq_ci(s, "su") || str_eq_ci(s, "passwd")) {
        term_fmt(c, TC_ERR, "%s: use the console shell (F12 to exit)", s);
    } else if (str_eq_ci(s, "byte-install")) {
        term_fmt(c, TC_ERR, "byte-install runs in the console; press F12 first");
    } else if (str_eq_ci(s, "start-desktop")) {
        term_fmt(c, TC_DIM, "ByteTile is already running; F12 exits");
    } else if (str_eq_ci(s, "donut")) {
        term_fmt(c, TC_ERR, "donut is a console app; press F12 to exit ByteTile");
    } else if (str_eq_ci(s, "logo") || str_eq_ci(s, "home")) {
        client_open(CLI_BROWSER, "www/index.html");
        term_fmt(c, TC_DIM, "browser home opened");
    } else if (str_eq_ci(s, "exit") || str_eq_ci(s, "quit") ||
               str_eq_ci(s, "close")) {
        int idx = (int)(c - g_clients);
        client_close(idx);
    } else {
        term_fmt(c, TC_ERR, "%s: command not found", s);
        term_fmt(c, TC_DIM, "Type help for commands.");
    }
}

static void files_enter(client_t *c)
{
    if (c->file_count <= 0 || c->file_sel < 0 || c->file_sel >= c->file_count)
        return;

    bfs_dir_entry_t *e = &c->files[c->file_sel];
    char next[TW_PATH];

    if (bfs_mounted()) {
        join_path(c->path, e->name, next, sizeof(next));
        if (e->is_dir || bfs_is_dir(next)) {
            str_copy(c->path, next, sizeof(c->path));
            files_refresh(c);
            return;
        }
        client_open(e->html ? CLI_BROWSER : CLI_VIEW, next);
    } else {
        client_open(e->html ? CLI_BROWSER : CLI_VIEW, e->name);
    }
}

static void files_up(client_t *c)
{
    char up[TW_PATH];
    parent_path(c->path, up, sizeof(up));
    str_copy(c->path, up, sizeof(c->path));
    files_refresh(c);
}

static void files_move(client_t *c, int dir)
{
    if (c->file_count <= 0)
        return;
    c->file_sel += dir;
    if (c->file_sel < 0)
        c->file_sel = c->file_count - 1;
    if (c->file_sel >= c->file_count)
        c->file_sel = 0;
}

static int cfg_line_start(int pos)
{
    if (pos > g_cfg_len)
        pos = g_cfg_len;
    while (pos > 0 && g_cfg_buf[pos - 1] != '\n')
        pos--;
    return pos;
}

static int cfg_line_end(int pos)
{
    if (pos > g_cfg_len)
        pos = g_cfg_len;
    while (pos < g_cfg_len && g_cfg_buf[pos] != '\n')
        pos++;
    return pos;
}

static void cfg_insert(char ch)
{
    if (g_cfg_len + 1 >= CFG_CAP)
        return;
    if (g_cfg_cursor < 0)
        g_cfg_cursor = 0;
    if (g_cfg_cursor > g_cfg_len)
        g_cfg_cursor = g_cfg_len;
    memmove(g_cfg_buf + g_cfg_cursor + 1, g_cfg_buf + g_cfg_cursor,
            (size_t)(g_cfg_len - g_cfg_cursor) + 1);
    g_cfg_buf[g_cfg_cursor++] = ch;
    g_cfg_len++;
    g_cfg_modified = 1;
}

static void cfg_backspace(void)
{
    if (g_cfg_cursor <= 0)
        return;
    memmove(g_cfg_buf + g_cfg_cursor - 1, g_cfg_buf + g_cfg_cursor,
            (size_t)(g_cfg_len - g_cfg_cursor) + 1);
    g_cfg_cursor--;
    g_cfg_len--;
    g_cfg_modified = 1;
}

static void cfg_delete(void)
{
    if (g_cfg_cursor >= g_cfg_len)
        return;
    memmove(g_cfg_buf + g_cfg_cursor, g_cfg_buf + g_cfg_cursor + 1,
            (size_t)(g_cfg_len - g_cfg_cursor));
    g_cfg_len--;
    g_cfg_modified = 1;
}

static void cfg_move_vertical(int dir)
{
    int start = cfg_line_start(g_cfg_cursor);
    int col = g_cfg_cursor - start;

    if (dir < 0) {
        if (start == 0)
            return;
        int prev_end = start - 1;
        int prev_start = cfg_line_start(prev_end);
        int next = prev_start + col;
        if (next > prev_end)
            next = prev_end;
        g_cfg_cursor = next;
    } else {
        int end = cfg_line_end(g_cfg_cursor);
        if (end >= g_cfg_len)
            return;
        int next_start = end + 1;
        int next_end = cfg_line_end(next_start);
        int next = next_start + col;
        if (next > next_end)
            next = next_end;
        g_cfg_cursor = next;
    }
}

static const char *const g_launch_items[] = {
    "terminal",
    "browser",
    "files",
    "system monitor",
    "config editor",
    "key reference",
    "reload config",
    "quit ByteTile"
};
#define LAUNCH_COUNT ((int)(sizeof(g_launch_items) / sizeof(g_launch_items[0])))

static uint8_t ink_on(uint8_t bg)
{
    if (bg == G_SILVER || bg == G_BLUE || bg == G_LIME || bg == G_CYAN ||
        bg == G_RED || bg == G_MAGENTA || bg == G_YELLOW || bg == G_WHITE)
        return G_BLACK;
    return G_WHITE;
}

static const char *layout_name(void)
{
    if (g_cfg.layout == LAYOUT_MONOCLE)
        return "monocle";
    if (g_cfg.layout == LAYOUT_GRID)
        return "grid";
    return "tile";
}

static const char *layout_symbol(void)
{
    if (g_cfg.layout == LAYOUT_MONOCLE)
        return "[M]";
    if (g_cfg.layout == LAYOUT_GRID)
        return "###";
    return "[]=";
}

static uint8_t term_color(uint8_t attr)
{
    switch (attr) {
    case TC_ACC:
        return (uint8_t)g_cfg.accent;
    case TC_ERR:
        return G_RED;
    case TC_DIM:
        return G_GRAY;
    case TC_GOOD:
        return G_LIME;
    default:
        return G_SILVER;
    }
}

static void visible_order(int *order, int *count)
{
    *count = 0;
    client_t *f = focused_client();
    if (f && client_visible(f))
        order[(*count)++] = g_focus;
    for (int i = 0; i < TW_MAX; i++) {
        if (g_clients[i].used && client_visible(&g_clients[i]) && i != g_focus)
            order[(*count)++] = i;
    }
}

static int tag_occupied(int tag)
{
    for (int i = 0; i < TW_MAX; i++) {
        if (g_clients[i].used && (g_clients[i].tags & (1u << tag)))
            return 1;
    }
    return 0;
}

static void draw_bar(void)
{
    int h = g_cfg.bar;
    fb_fill(0, 0, GW, h, (uint8_t)g_cfg.bar_bg);

    if (h >= 18) {
        int x = 6;
        for (int t = 0; t < 9; t++) {
            int cell = 20;
            int ty = (h - 16) / 2;
            if (t == g_tag) {
                fb_fill(x, (h - 18) / 2, cell, 18, (uint8_t)g_cfg.accent);
                fb_char(x + 6, ty, (unsigned char)('1' + t),
                        ink_on((uint8_t)g_cfg.accent), -1);
            } else {
                fb_char(x + 6, ty, (unsigned char)('1' + t),
                        tag_occupied(t) ? (uint8_t)g_cfg.bar_fg : G_GRAY, -1);
            }
            if (t != g_tag && tag_occupied(t))
                fb_hline(x + 4, h - 4, cell - 8, (uint8_t)g_cfg.bar_fg);
            x += cell;
        }

        x += 8;
        fb_text(x, (h - 16) / 2, layout_symbol(), (uint8_t)g_cfg.accent, -1);
        x += 4 * 8 + 8;

        char clock[8];
        rtc_time_t now;
        if (rtc_read(&now) == 0)
            format_buf(clock, sizeof(clock), "%02d:%02d", now.hour, now.minute);
        else
            str_copy(clock, "--:--", sizeof(clock));
        int clock_x = GW - 6 - 5 * 8;
        fb_text(clock_x, (h - 16) / 2, clock, (uint8_t)g_cfg.bar_fg, -1);

        int right = clock_x - 8;
        if (g_status[0] && g_status_until &&
            read_tsc() < g_status_until) {
            int slen = (int)kstrlen(g_status);
            if (slen > 30)
                slen = 30;
            right -= slen * 8;
            fb_text_lim(right, (h - 16) / 2, g_status,
                        (uint8_t)g_cfg.accent, -1, slen * 8);
            right -= 8;
        }

        client_t *f = focused_client();
        if (f && right > x) {
            char title[48];
            format_buf(title, sizeof(title), "%s", f->title);
            fb_text_lim(x, (h - 16) / 2, title,
                        (uint8_t)g_cfg.bar_fg, -1, right - x);
        }
    }
}

static void compute_rects(tw_rect_t *rects, int count)
{
    int x = g_cfg.gaps;
    int y = g_cfg.bar + g_cfg.gaps;
    int w = GW - g_cfg.gaps * 2;
    int h = GH - g_cfg.bar - g_cfg.gaps * 2;

    if (count <= 0)
        return;
    if (w < 20)
        w = 20;
    if (h < 20)
        h = 20;

    if (count == 1 || g_cfg.layout == LAYOUT_MONOCLE) {
        rects[0].x = x;
        rects[0].y = y;
        rects[0].w = w;
        rects[0].h = h;
        return;
    }

    if (g_cfg.layout == LAYOUT_TILE) {
        int master_w = (w - g_cfg.gaps) * g_cfg.master / 100;
        if (master_w < 100)
            master_w = 100;
        if (master_w > w - g_cfg.gaps - 100)
            master_w = w - g_cfg.gaps - 100;
        if (master_w < 40)
            master_w = w;
        rects[0].x = x;
        rects[0].y = y;
        rects[0].w = master_w;
        rects[0].h = h;

        int sx = x + master_w + g_cfg.gaps;
        int sw = w - master_w - g_cfg.gaps;
        int sh = (h - g_cfg.gaps * (count - 2)) / (count - 1);
        for (int i = 1; i < count; i++) {
            rects[i].x = sx;
            rects[i].y = y + (i - 1) * (sh + g_cfg.gaps);
            rects[i].w = sw;
            rects[i].h = sh;
            if (i == count - 1)
                rects[i].h = y + h - rects[i].y;
        }
        return;
    }

    int cols = 1;
    while (cols * cols < count)
        cols++;
    int rows = (count + cols - 1) / cols;
    int cellw = (w - g_cfg.gaps * (cols - 1)) / cols;
    int cellh = (h - g_cfg.gaps * (rows - 1)) / rows;
    for (int i = 0; i < count; i++) {
        int r = i / cols;
        int c = i % cols;
        rects[i].x = x + c * (cellw + g_cfg.gaps);
        rects[i].y = y + r * (cellh + g_cfg.gaps);
        rects[i].w = cellw;
        rects[i].h = cellh;
        if (c == cols - 1)
            rects[i].w = x + w - rects[i].x;
        if (r == rows - 1)
            rects[i].h = y + h - rects[i].y;
    }
}

static int term_line_len(const char *s)
{
    int n = 0;
    while (n < TERM_COLS && s[n])
        n++;
    return n;
}

static int term_wrap_rows(const char *s, int cols)
{
    int n = term_line_len(s);
    if (n <= 0)
        return 1;
    return (n + cols - 1) / cols;
}

static void draw_terminal(client_t *c, int cx, int cy, int cw, int ch)
{
    int x = cx + 6;
    int y = cy + 4;
    int cols = (cw - 12) / 8;
    int rows = (ch - 8) / 16;

    if (cols <= 0 || rows < 2)
        return;

    int hist_rows = rows - 1;
    int end = c->term_count - c->scroll;
    if (end > c->term_count)
        end = c->term_count;
    if (end < 0)
        end = 0;

    int start = end;
    int used = 0;
    while (start > 0) {
        int need = term_wrap_rows(c->term[start - 1], cols);
        if (used + need > hist_rows)
            break;
        used += need;
        start--;
    }

    int line = 0;
    for (int i = start; i < end && line < hist_rows; i++) {
        const char *s = c->term[i];
        int len = term_line_len(s);
        int chunks = len ? (len + cols - 1) / cols : 1;
        for (int k = 0; k < chunks && line < hist_rows; k++, line++) {
            char buf[TERM_COLS + 1];
            int off = k * cols;
            int take = len - off;
            if (take < 0)
                take = 0;
            if (take > cols)
                take = cols;
            memcpy(buf, s + off, take);
            buf[take] = 0;
            fb_text(x, y + line * 16, buf, term_color(c->term_attr[i]), -1);
        }
    }

    int py = y + hist_rows * 16;
    fb_text(x, py, "$ ", (uint8_t)g_cfg.accent, -1);
    fb_text_lim(x + 16, py, c->input, G_WHITE, -1, cw - 28);
    int px = x + 16 + c->input_len * 8;
    if (px < cx + cw - 8)
        fb_fill(px, py + 14, 6, 2, (uint8_t)g_cfg.accent);
}

static void draw_files(client_t *c, int cx, int cy, int cw, int ch)
{
    int x = cx + 6;
    int y = cy + 4;
    int rows = (ch - 32) / 16;

    if (rows < 1)
        return;

    fb_text_lim(x, y, c->path, (uint8_t)g_cfg.accent, -1, cw - 12);
    y += 20;

    if (c->file_sel < c->file_scroll)
        c->file_scroll = c->file_sel;
    if (c->file_sel >= c->file_scroll + rows)
        c->file_scroll = c->file_sel - rows + 1;
    if (c->file_scroll < 0)
        c->file_scroll = 0;

    for (int r = 0; r < rows; r++) {
        int i = c->file_scroll + r;
        if (i >= c->file_count)
            break;
        bfs_dir_entry_t *e = &c->files[i];
        int ry = y + r * 16;
        uint8_t fg = e->is_dir ? G_CYAN : G_SILVER;

        if (i == c->file_sel) {
            fb_fill(cx + 2, ry - 1, cw - 4, 16, (uint8_t)g_cfg.accent);
            fg = ink_on((uint8_t)g_cfg.accent);
        }

        char label[64];
        format_buf(label, sizeof(label), "%s%s", e->name, e->is_dir ? "/" : "");
        fb_text_lim(x, ry, label, fg, -1, cw - 100);

        if (!e->is_dir) {
            char size[24];
            format_buf(size, sizeof(size), "%u", e->size);
            fb_text_right(x, ry, cw - 16, size, fg, -1);
        }
    }

    if (c->file_count == 0)
        fb_text(x, y, "(empty)", G_GRAY, -1);

    fb_hline(cx + 4, cy + ch - 22, cw - 8, G_GRAY);
    fb_text_lim(x, cy + ch - 18,
                bfs_mounted() ? "Enter open  Backspace up  Super+Q close"
                              : "Live ISO files  Enter open  Super+Q close",
                G_GRAY, -1, cw - 12);
}

static void draw_sys(client_t *c, int cx, int cy, int cw, int ch)
{
    (void)c;
    int x = cx + 10;
    int y = cy + 8;

    fb_text(x, y, "ByteOS system monitor", (uint8_t)g_cfg.accent, -1);
    y += 24;

    char line[96];
    fb_text(x, y, "CPU", G_GRAY, -1);
    format_buf(line, sizeof(line), "%s", g_cpu_brand);
    fb_text_lim(x + 64, y, line, G_WHITE, -1, cw - 80);
    y += 16;

    format_buf(line, sizeof(line), "cores %u   tsc ~%llu MHz",
               g_logical_cpus,
               (unsigned long long)((g_tsc_hz + 500000ULL) / 1000000ULL));
    fb_text(x, y, "SYS", G_GRAY, -1);
    fb_text_lim(x + 64, y, line, G_WHITE, -1, cw - 80);
    y += 16;

    format_buf(line, sizeof(line), "%llu MB usable / %llu MB top",
               (unsigned long long)(g_mem_usable / (1024ULL * 1024ULL)),
               (unsigned long long)(g_mem_top / (1024ULL * 1024ULL)));
    fb_text(x, y, "MEM", G_GRAY, -1);
    fb_text_lim(x + 64, y, line, G_WHITE, -1, cw - 80);
    y += 16;

    if (bfs_mounted())
        format_buf(line, sizeof(line), "ByteFS %u KB used, %u KB free",
                   (unsigned)(bfs_used_bytes() / 1024u),
                   (unsigned)(bfs_free_bytes() / 1024u));
    else
        format_buf(line, sizeof(line), "ByteFS not mounted (live ISO)");
    fb_text(x, y, "DISK", G_GRAY, -1);
    fb_text_lim(x + 64, y, line, G_WHITE, -1, cw - 80);
    y += 16;

    format_buf(line, sizeof(line), "VGA 640x480x16  ByteTile %s", layout_name());
    fb_text(x, y, "WM", G_GRAY, -1);
    fb_text_lim(x + 64, y, line, G_WHITE, -1, cw - 80);
    y += 24;

    if (y + FETCH_ART_ROWS * 16 < cy + ch) {
        uint8_t art_colors[3] = { G_LIME, (uint8_t)g_cfg.accent, G_GRAY };
        for (int i = 0; i < FETCH_ART_ROWS; i++) {
            if (y + i * 16 + 16 > cy + ch)
                break;
            fb_text_lim(x, y + i * 16, g_fetch_art[i],
                        art_colors[i % 3], -1, cw - 20);
        }
    }
}

static void draw_view(client_t *c, int cx, int cy, int cw, int ch)
{
    int x = cx + 6;
    int y = cy + 4;
    int rows = (ch - 8) / 16;
    int maxw = cw - 12;

    if (rows < 1)
        return;

    int line = 0;
    int drawn = 0;
    const char *p = c->view;

    while (*p && drawn < rows) {
        const char *nl = p;
        while (*nl && *nl != '\n')
            nl++;
        if (line >= c->scroll) {
            char tmp[256];
            int n = 0;
            for (const char *q = p; q < nl && n < (int)sizeof(tmp) - 1 &&
                                   n * 8 < maxw; q++) {
                tmp[n++] = (*q == '\t' || *q == '\r') ? ' ' : *q;
            }
            tmp[n] = 0;
            fb_text(x, y + drawn * 16, tmp, G_SILVER, -1);
            drawn++;
        }
        line++;
        if (!*nl)
            break;
        p = nl + 1;
    }

    if (drawn == 0)
        fb_text(x, y, "(empty or end of file)", G_GRAY, -1);
}

static uint8_t br_line_color(uint8_t style)
{
    switch (style) {
    case 1:
        return G_YELLOW;
    case 2:
        return G_CYAN;
    case 3:
        return G_GRAY;
    case 4:
        return G_LIME;
    default:
        return G_SILVER;
    }
}

static void draw_browser(client_t *c, int cx, int cy, int cw, int ch)
{
    int x = cx + 6;
    int y = cy + 4;
    int cols = (cw - 12) / 8;
    int rows = (ch - 46) / 16;

    if (cols < 10 || rows < 1)
        return;
    if (cols > BR_MAX_COLS)
        cols = BR_MAX_COLS;

    if (cols != g_br_render_cols && g_br_src[0]) {
        g_br_render_cols = cols;
        br_render_html(g_br_src);
    }
    g_br_rows = rows;

    fb_text_lim(x, y, g_br_path[0] ? g_br_path : "www/index.html",
                (uint8_t)g_cfg.accent, -1, cw - 12);
    fb_hline(cx + 4, y + 18, cw - 8, G_GRAY);

    int max_scroll = g_br_line_count - rows;
    if (max_scroll < 0)
        max_scroll = 0;
    if (c->scroll > max_scroll)
        c->scroll = max_scroll;
    if (c->scroll < 0)
        c->scroll = 0;

    int top = y + 24;
    for (int r = 0; r < rows; r++) {
        int i = c->scroll + r;
        if (i >= g_br_line_count)
            break;
        fb_text_lim(x, top + r * 16, g_br_lines[i],
                    br_line_color(g_br_style[i]), -1, cw - 20);
    }

    if (g_br_line_count > rows) {
        int track = rows * 16;
        int thumb = track * rows / g_br_line_count;
        if (thumb < 10)
            thumb = 10;
        if (thumb > track)
            thumb = track;
        int pos = (track - thumb) * c->scroll / max_scroll;
        fb_fill(cx + cw - 7, top + pos, 3, thumb, G_GRAY);
    }

    int fy = cy + ch - 18;
    fb_hline(cx + 4, fy - 4, cw - 8, G_GRAY);
    if (g_br_prompt) {
        fb_text(x, fy, "open:", (uint8_t)g_cfg.accent, -1);
        fb_text_lim(x + 48, fy, g_br_input, G_WHITE, -1, cw - 60);
        int px = x + 48 + g_br_input_len * 8;
        if (px < cx + cw - 8)
            fb_fill(px, fy + 14, 6, 2, (uint8_t)g_cfg.accent);
    } else if (g_br_status[0]) {
        fb_text_lim(x, fy, g_br_status, G_YELLOW, -1, cw - 12);
    } else {
        fb_text_lim(x, fy, "1-9 link  Bksp back  r reload  u open  Esc close",
                    G_GRAY, -1, cw - 12);
    }
}

static void draw_config(client_t *c, int cx, int cy, int cw, int ch)
{
    (void)c;
    int x = cx + 6;
    int y = cy + 4;
    int gutter = 4 * 8 + 6;
    int rows = (ch - 34) / 16;
    int cols = (cw - 12 - gutter) / 8;

    if (rows < 1 || cols < 4)
        return;

    int cursor_start = cfg_line_start(g_cfg_cursor);
    int cursor_col = g_cfg_cursor - cursor_start;
    int cursor_row = 0;
    for (int i = 0; i < cursor_start; i++) {
        if (g_cfg_buf[i] == '\n')
            cursor_row++;
    }

    if (cursor_row < g_cfg_scroll)
        g_cfg_scroll = cursor_row;
    if (cursor_row >= g_cfg_scroll + rows)
        g_cfg_scroll = cursor_row - rows + 1;
    if (g_cfg_scroll < 0)
        g_cfg_scroll = 0;

    if (cursor_col < g_cfg_hscroll)
        g_cfg_hscroll = cursor_col;
    if (cursor_col >= g_cfg_hscroll + cols)
        g_cfg_hscroll = cursor_col - cols + 1;
    if (g_cfg_hscroll < 0)
        g_cfg_hscroll = 0;

    int row = 0;
    int pos = 0;
    while (pos <= g_cfg_len && row < g_cfg_scroll) {
        while (pos < g_cfg_len && g_cfg_buf[pos] != '\n')
            pos++;
        if (pos < g_cfg_len && g_cfg_buf[pos] == '\n')
            pos++;
        row++;
    }

    for (int r = 0; r < rows; r++) {
        if (pos > g_cfg_len)
            break;
        int start = pos;
        int end = start;
        while (end < g_cfg_len && g_cfg_buf[end] != '\n')
            end++;

        char num[8];
        format_buf(num, sizeof(num), "%03d", g_cfg_scroll + r + 1);
        fb_text(x, y + r * 16, num, G_GRAY, -1);

        char text[192];
        int n = 0;
        for (int i = start + g_cfg_hscroll;
             i < end && n < (int)sizeof(text) - 1 && n < cols; i++)
            text[n++] = g_cfg_buf[i];
        text[n] = 0;
        fb_text(x + gutter, y + r * 16, text, G_SILVER, -1);

        if ((g_cfg_scroll + r) == cursor_row) {
            int cxp = x + gutter + (cursor_col - g_cfg_hscroll) * 8;
            if (cxp >= x + gutter && cxp < cx + cw - 6)
                fb_fill(cxp, y + r * 16, 2, 16, (uint8_t)g_cfg.accent);
        }

        if (end >= g_cfg_len)
            break;
        pos = end + 1;
    }

    fb_hline(cx + 4, cy + ch - 26, cw - 8, G_GRAY);
    char hint[96];
    format_buf(hint, sizeof(hint), "Ctrl+S save   Ctrl+R reload   %s   Esc close",
               g_cfg_modified ? "modified*" : "saved");
    fb_text_lim(x, cy + ch - 20, hint, G_GRAY, -1, cw - 12);
}

static void draw_keys(client_t *c, int cx, int cy, int cw, int ch)
{
    (void)c;
    static const struct {
        const char *key;
        const char *desc;
    } rows[] = {
        { "Super+Return", "open terminal (Alt+Return)" },
        { "Super+Space", "launcher menu (Alt+p)" },
        { "Super+Q", "close window (Alt+q)" },
        { "Alt+j / k", "focus next / previous" },
        { "Alt+Shift+j/k", "move window in stack" },
        { "Alt+h / l", "shrink / grow master area" },
        { "Alt+Space", "cycle tile, monocle, grid" },
        { "Alt+f", "zoom (tile / monocle)" },
        { "Alt+1..9", "view tag" },
        { "Alt+Shift+1..9", "move window to tag" },
        { "Alt+Shift+C", "open config editor" },
        { "Alt+Shift+R", "reload /bytetile.conf" },
        { "Alt+Shift+E", "quit ByteTile" },
        { "F12", "quit ByteTile" },
        { "", "" },
        { "config keys", "gaps border master layout bar" },
        { "", "accent bar_fg bar_bg" },
        { "", "border_active border_normal" },
        { "config file", "/bytetile.conf" },
        { "terminal", "ls cd cat open edit fetch" }
    };

    int x = cx + 10;
    int y = cy + 8;
    int keyw = 18 * 8;

    for (unsigned i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        int ry = y + (int)i * 16;
        if (ry + 16 > cy + ch - 4)
            break;
        if (!rows[i].key[0] && !rows[i].desc[0])
            continue;
        fb_text_lim(x, ry, rows[i].key, (uint8_t)g_cfg.accent, -1, keyw);
        fb_text_lim(x + keyw, ry, rows[i].desc, G_SILVER, -1, cw - keyw - 20);
    }
}

static void draw_client(tw_rect_t r, int idx, int active)
{
    client_t *c = &g_clients[idx];
    uint8_t border = active ? (uint8_t)g_cfg.border_active
                            : (uint8_t)g_cfg.border_normal;
    int b = g_cfg.border;

    fb_fill(r.x, r.y, r.w, r.h, border);
    fb_fill(r.x + b, r.y + b, r.w - b * 2, r.h - b * 2, G_BLACK);

    int cx = r.x + b;
    int cy = r.y + b;
    int cw = r.w - b * 2;
    int ch = r.h - b * 2;
    if (cw <= 0 || ch <= 0)
        return;

    switch (c->type) {
    case CLI_TERM:
        draw_terminal(c, cx, cy, cw, ch);
        break;
    case CLI_FILES:
        draw_files(c, cx, cy, cw, ch);
        break;
    case CLI_SYS:
        draw_sys(c, cx, cy, cw, ch);
        break;
    case CLI_VIEW:
        draw_view(c, cx, cy, cw, ch);
        break;
    case CLI_BROWSER:
        draw_browser(c, cx, cy, cw, ch);
        break;
    case CLI_CFG:
        draw_config(c, cx, cy, cw, ch);
        break;
    case CLI_KEYS:
        draw_keys(c, cx, cy, cw, ch);
        break;
    default:
        break;
    }
}

static void draw_welcome(void)
{
    int w = 496;
    int h = 320;
    int x = (GW - w) / 2;
    int y = g_cfg.bar + (GH - g_cfg.bar - h) / 2;
    if (y < g_cfg.bar + 8)
        y = g_cfg.bar + 8;

    fb_fill(x, y, w, h, G_NAVY);
    fb_rect(x, y, w, h, 2, (uint8_t)g_cfg.border_active);

    fb_text_center(x, y + 16, w, "B Y T E T I L E", (uint8_t)g_cfg.accent, -1);
    fb_text_center(x, y + 36, w, "a dwm-style tiling desktop",
                   G_SILVER, -1);

    static const char *lines[] = {
        "Super+Return  open terminal (Alt+Return)",
        "Super+Space   launcher (Alt+p)",
        "Super+Q       close window (Alt+q)",
        "Alt+j / k     focus windows",
        "Alt+Shift+j/k move window in stack",
        "Alt+h / l     master area size",
        "Alt+Space     tile / monocle / grid",
        "Alt+f         zoom",
        "Alt+1..9      view tag",
        "Alt+Shift+1..9  move window to tag",
        "Alt+Shift+C   edit /bytetile.conf",
        "Alt+Shift+R   reload config",
        "Alt+Shift+E   quit ByteTile"
    };
    int ly = y + 68;
    for (unsigned i = 0; i < sizeof(lines) / sizeof(lines[0]); i++) {
        fb_text(x + 28, ly + (int)i * 16, lines[i], G_SILVER, -1);
    }
    fb_text_center(x, y + h - 24, w,
                   "Alt+Shift+C, then Ctrl+S to save your config",
                   G_GRAY, -1);
}

static void draw_launcher(void)
{
    int w = 360;
    int h = LAUNCH_COUNT * 22 + 74;
    int x = (GW - w) / 2;
    int y = (GH - h) / 2;

    fb_fill(x, y, w, h, G_NAVY);
    fb_rect(x, y, w, h, 2, (uint8_t)g_cfg.border_active);
    fb_text_center(x + 8, y + 10, w - 16, "BYTETILE LAUNCHER",
                   (uint8_t)g_cfg.accent, -1);

    for (int i = 0; i < LAUNCH_COUNT; i++) {
        int ry = y + 38 + i * 22;
        if (i == g_launch_sel) {
            fb_fill(x + 14, ry, w - 28, 20, (uint8_t)g_cfg.accent);
            fb_text(x + 24, ry + 2, g_launch_items[i],
                    ink_on((uint8_t)g_cfg.accent), -1);
        } else {
            fb_text(x + 24, ry + 2, g_launch_items[i], G_SILVER, -1);
        }
    }

    fb_text_center(x, y + h - 22, w,
                   "Up/Down select   Enter open   Esc close", G_GRAY, -1);
}

static void draw_all(void)
{
    memset(g_fb, 0, GW * GH);

    int order[TW_MAX];
    int count = 0;
    visible_order(order, &count);

    if (count == 0) {
        draw_welcome();
    } else {
        tw_rect_t rects[TW_MAX];
        if (g_cfg.layout == LAYOUT_MONOCLE)
            count = 1;
        compute_rects(rects, count);
        for (int i = 0; i < count; i++)
            draw_client(rects[i], order[i], order[i] == g_focus);
    }

    draw_bar();
    if (g_launcher)
        draw_launcher();
}

static int view_lines(client_t *c)
{
    int count = 1;
    for (int i = 0; i < c->view_len; i++) {
        if (c->view[i] == '\n')
            count++;
    }
    return count;
}

static void cycle_layout(void)
{
    g_cfg.layout++;
    if (g_cfg.layout > LAYOUT_GRID)
        g_cfg.layout = LAYOUT_TILE;
    set_status("Layout: %s", layout_name());
}

static int digit_key(int key)
{
    static const char shifted[9] = { '!', '@', '#', '$', '%', '^', '&', '*', '(' };
    if (key >= '1' && key <= '9')
        return key - '1';
    for (int i = 0; i < 9; i++) {
        if (key == shifted[i])
            return i;
    }
    return -1;
}

static void launcher_activate(void)
{
    switch (g_launch_sel) {
    case 0:
        client_open(CLI_TERM, 0);
        break;
    case 1:
        client_open(CLI_BROWSER, "www/index.html");
        break;
    case 2:
        client_open(CLI_FILES, 0);
        break;
    case 3:
        client_open(CLI_SYS, 0);
        break;
    case 4:
        client_open(CLI_CFG, 0);
        break;
    case 5:
        client_open(CLI_KEYS, 0);
        break;
    case 6:
        cfg_load();
        set_status("Reloaded /%s", CFG_FILE);
        break;
    case 7:
        g_quit = 1;
        break;
    default:
        break;
    }
    g_launcher = 0;
}

static void launcher_key(int key)
{
    if (key == KEY_UP || key == 'k' || key == 'K') {
        g_launch_sel--;
        if (g_launch_sel < 0)
            g_launch_sel = LAUNCH_COUNT - 1;
    } else if (key == KEY_DOWN || key == 'j' || key == 'J') {
        g_launch_sel++;
        if (g_launch_sel >= LAUNCH_COUNT)
            g_launch_sel = 0;
    } else if (key == '\n' || key == '\r') {
        launcher_activate();
    } else if (key == 27) {
        g_launcher = 0;
    } else if (key >= '1' && key <= '9') {
        int idx = key - '1';
        if (idx < LAUNCH_COUNT) {
            g_launch_sel = idx;
            launcher_activate();
        }
    }
}

static void config_key(client_t *c, int key, int ctrl)
{
    if (key == 19 || (ctrl && (key == 's' || key == 'S'))) {
        cfg_save();
        return;
    }
    if (key == 18 || (ctrl && (key == 'r' || key == 'R'))) {
        cfg_load();
        set_status("Reloaded /%s", CFG_FILE);
        return;
    }
    if (ctrl)
        return;

    if (key == 27) {
        client_close((int)(c - g_clients));
    } else if (key == KEY_UP) {
        cfg_move_vertical(-1);
    } else if (key == KEY_DOWN) {
        cfg_move_vertical(1);
    } else if (key == KEY_LEFT) {
        if (g_cfg_cursor > 0)
            g_cfg_cursor--;
    } else if (key == KEY_RIGHT) {
        if (g_cfg_cursor < g_cfg_len)
            g_cfg_cursor++;
    } else if (key == KEY_HOME) {
        g_cfg_cursor = cfg_line_start(g_cfg_cursor);
    } else if (key == KEY_END) {
        g_cfg_cursor = cfg_line_end(g_cfg_cursor);
    } else if (key == KEY_PGUP) {
        g_cfg_cursor = cfg_line_start(g_cfg_cursor);
        for (int i = 0; i < 10; i++)
            cfg_move_vertical(-1);
    } else if (key == KEY_PGDN) {
        g_cfg_cursor = cfg_line_end(g_cfg_cursor);
        for (int i = 0; i < 10; i++)
            cfg_move_vertical(1);
    } else if (key == KEY_DEL) {
        cfg_delete();
    } else if (key == 8 || key == 127) {
        cfg_backspace();
    } else if (key == '\n' || key == '\r') {
        cfg_insert('\n');
    } else if (key == '\t') {
        cfg_insert(' ');
        cfg_insert(' ');
    } else if (key >= 32 && key < 127) {
        cfg_insert((char)key);
    }
}

static void terminal_key(client_t *c, int key, int ctrl)
{
    if (key == '\n' || key == '\r') {
        char cmd[TERM_COLS + 1];
        str_copy(cmd, c->input, sizeof(cmd));
        term_fmt(c, TC_DIM, "$ %s", cmd);
        term_history_push(c, cmd);
        c->input[0] = 0;
        c->input_len = 0;
        term_exec(c, cmd);
    } else if (key == 8 || key == 127) {
        if (c->input_len > 0) {
            c->input[--c->input_len] = 0;
        }
    } else if (key == KEY_UP) {
        term_history_move(c, -1);
    } else if (key == KEY_DOWN) {
        term_history_move(c, 1);
    } else if (key == KEY_PGUP) {
        c->scroll += 5;
        if (c->scroll > c->term_count)
            c->scroll = c->term_count;
    } else if (key == KEY_PGDN) {
        c->scroll -= 5;
        if (c->scroll < 0)
            c->scroll = 0;
    } else if (key == 27) {
        c->input[0] = 0;
        c->input_len = 0;
    } else if (ctrl && (key == 'l' || key == 'L' || key == 12)) {
        c->term_count = 0;
        c->scroll = 0;
    } else if (ctrl) {
        if (key == 3) {
            c->input[0] = 0;
            c->input_len = 0;
        }
    } else if (key >= 32 && key < 127) {
        if (c->input_len < TERM_COLS) {
            c->input[c->input_len++] = (char)key;
            c->input[c->input_len] = 0;
        }
    }
}

static void files_key(client_t *c, int key)
{
    if (key == KEY_UP || key == 'k' || key == 'K') {
        files_move(c, -1);
    } else if (key == KEY_DOWN || key == 'j' || key == 'J') {
        files_move(c, 1);
    } else if (key == KEY_PGUP) {
        files_move(c, -10);
    } else if (key == KEY_PGDN) {
        files_move(c, 10);
    } else if (key == KEY_HOME) {
        c->file_sel = 0;
    } else if (key == KEY_END) {
        c->file_sel = c->file_count > 0 ? c->file_count - 1 : 0;
    } else if (key == '\n' || key == '\r') {
        files_enter(c);
    } else if (key == 8 || key == 127) {
        files_up(c);
    } else if (key == 'r' || key == 'R') {
        files_refresh(c);
        set_status("Refreshed %s", c->path);
    } else if (key == 27) {
        files_up(c);
    }
}

static void viewer_key(client_t *c, int key)
{
    int lines = view_lines(c);
    if (key == KEY_UP || key == 'k' || key == 'K') {
        c->scroll--;
    } else if (key == KEY_DOWN || key == 'j' || key == 'J') {
        c->scroll++;
    } else if (key == KEY_PGUP) {
        c->scroll -= 10;
    } else if (key == KEY_PGDN) {
        c->scroll += 10;
    } else if (key == KEY_HOME) {
        c->scroll = 0;
    } else if (key == KEY_END) {
        c->scroll = lines - 1;
    } else if (key == 27) {
        client_close((int)(c - g_clients));
        return;
    }
    if (c->scroll < 0)
        c->scroll = 0;
    if (c->scroll > lines - 1)
        c->scroll = lines - 1;
}

static void br_follow(int index)
{
    if (index < 0 || index >= g_br_link_count) {
        set_status("No link %d", index + 1);
        return;
    }
    if (br_starts_ci(g_br_href[index], "http://") ||
        br_starts_ci(g_br_href[index], "https://")) {
        set_status("No network: %s", g_br_href[index]);
        return;
    }
    char resolved[TW_PATH];
    br_resolve(g_br_href[index], resolved, sizeof(resolved));
    br_load(resolved, 1);
}

static void browser_key(client_t *c, int key)
{
    if (g_br_prompt) {
        if (key == '\n' || key == '\r') {
            g_br_prompt = 0;
            if (g_br_input[0])
                br_load(g_br_input, 1);
        } else if (key == 27) {
            g_br_prompt = 0;
        } else if (key == 8 || key == 127) {
            if (g_br_input_len > 0)
                g_br_input[--g_br_input_len] = 0;
        } else if (key >= 32 && key < 127) {
            if (g_br_input_len < TW_PATH - 1) {
                g_br_input[g_br_input_len++] = (char)key;
                g_br_input[g_br_input_len] = 0;
            }
        }
        return;
    }

    int rows = g_br_rows > 0 ? g_br_rows : 20;
    int max_scroll = g_br_line_count - rows;
    if (max_scroll < 0)
        max_scroll = 0;

    if (key == KEY_UP) {
        c->scroll--;
    } else if (key == KEY_DOWN) {
        c->scroll++;
    } else if (key == KEY_PGUP) {
        c->scroll -= rows - 2;
    } else if (key == KEY_PGDN) {
        c->scroll += rows - 2;
    } else if (key == KEY_HOME) {
        c->scroll = 0;
    } else if (key == KEY_END) {
        c->scroll = max_scroll;
    } else if (key >= '1' && key <= '9') {
        br_follow(key - '1');
    } else if (key == 8 || key == 127 || key == 'b' || key == 'B') {
        if (g_br_hist_count > 0) {
            char prev[TW_PATH];
            str_copy(prev, g_br_hist[--g_br_hist_count], sizeof(prev));
            br_load(prev, 0);
        }
    } else if (key == 'r' || key == 'R') {
        br_load(g_br_path, 0);
    } else if (key == 'u' || key == 'U') {
        g_br_prompt = 1;
        g_br_input[0] = 0;
        g_br_input_len = 0;
    } else if (key == 'h' || key == 'H') {
        br_load("www/index.html", 1);
    } else if (key == 27) {
        client_close((int)(c - g_clients));
        return;
    }

    if (c->scroll < 0)
        c->scroll = 0;
    if (c->scroll > max_scroll)
        c->scroll = max_scroll;
}

static void handle_key(int key, int mods)
{
    int alt = (mods & KBD_MOD_ALT) != 0;
    int super = (mods & KBD_MOD_SUPER) != 0;
    int shift = (mods & KBD_MOD_SHIFT) != 0;
    int ctrl = (mods & KBD_MOD_CTRL) != 0;

    if (super) {
        if (key == ' ') {
            g_launcher = !g_launcher;
            if (g_launcher)
                g_launch_sel = 0;
            return;
        }
        if (key == 'q' || key == 'Q') {
            g_launcher = 0;
            if (g_focus >= 0)
                client_close(g_focus);
            return;
        }
        if (key == '\n' || key == '\r') {
            g_launcher = 0;
            client_open(CLI_TERM, 0);
            return;
        }
    }

    if (g_launcher) {
        launcher_key(key);
        return;
    }

    if (alt) {
        if (shift) {
            if (key == 'C' || key == 'c') {
                client_open(CLI_CFG, 0);
                return;
            }
            if (key == 'R' || key == 'r') {
                cfg_load();
                set_status("Reloaded /%s", CFG_FILE);
                return;
            }
            if (key == 'E' || key == 'e' ||
                key == 'Q' || key == 'q') {
                g_quit = 1;
                return;
            }
            int dtag = digit_key(key);
            if (dtag >= 0) {
                move_to_tag(dtag);
                return;
            }
            if (key == 'J' || key == 'j') {
                move_client(1);
                return;
            }
            if (key == 'K' || key == 'k') {
                move_client(-1);
                return;
            }
        }

        switch (key) {
        case '\n':
        case '\r':
            client_open(CLI_TERM, 0);
            return;
        case 'p':
        case 'P':
            g_launcher = 1;
            g_launch_sel = 0;
            return;
        case 'j':
        case 'J': {
            int n = next_visible(1);
            if (n >= 0)
                g_focus = n;
            return;
        }
        case 'k':
        case 'K': {
            int n = next_visible(-1);
            if (n >= 0)
                g_focus = n;
            return;
        }
        case 'h':
        case 'H':
            g_cfg.master = clamp_int(g_cfg.master - 5, 20, 85);
            set_status("Master: %d%%", g_cfg.master);
            return;
        case 'l':
        case 'L':
            g_cfg.master = clamp_int(g_cfg.master + 5, 20, 85);
            set_status("Master: %d%%", g_cfg.master);
            return;
        case 'f':
        case 'F':
            g_cfg.layout = (g_cfg.layout == LAYOUT_MONOCLE)
                               ? LAYOUT_TILE : LAYOUT_MONOCLE;
            set_status("Layout: %s", layout_name());
            return;
        case ' ':
            cycle_layout();
            return;
        case 'q':
        case 'Q':
            if (g_focus >= 0)
                client_close(g_focus);
            return;
        default:
            break;
        }

        int dtag = digit_key(key);
        if (dtag >= 0) {
            focus_tag(dtag);
            return;
        }
        return;
    }

    if (key == KEY_F12) {
        g_quit = 1;
        return;
    }

    client_t *c = focused_client();
    if (!c)
        return;

    switch (c->type) {
    case CLI_TERM:
        terminal_key(c, key, ctrl);
        break;
    case CLI_FILES:
        files_key(c, key);
        break;
    case CLI_VIEW:
        viewer_key(c, key);
        break;
    case CLI_BROWSER:
        browser_key(c, key);
        break;
    case CLI_CFG:
        config_key(c, key, ctrl);
        break;
    case CLI_SYS:
    case CLI_KEYS:
        if (key == 27)
            client_close((int)(c - g_clients));
        break;
    default:
        break;
    }
}

int tilewm_desktop(void)
{
    if (vga_enter_graphics() != 0)
        return -1;

    kbd_drain();
    init_present();
    memset(g_clients, 0, sizeof(g_clients));
    g_focus = -1;
    g_tag = 0;
    g_quit = 0;
    g_launcher = 0;
    g_launch_sel = 0;
    g_status[0] = 0;
    g_status_until = 0;

    cfg_load();
    client_open(CLI_TERM, 0);
    g_focus = first_visible();

    draw_all();
    present();

    uint64_t next_clock = g_tsc_hz ? read_tsc() + g_tsc_hz : 0;
    uint64_t next_frame = 0;
    int pending = 0;

    while (!g_quit) {
        int key = kbd_getkey();
        if (key >= 0) {
            handle_key(key, kbd_mods());
            pending = 1;
        }

        uint64_t now = read_tsc();
        if (g_tsc_hz && now >= next_clock) {
            pending = 1;
            next_clock += g_tsc_hz;
            if (next_clock <= now)
                next_clock = now + g_tsc_hz;
        }

        if (pending && (!g_tsc_hz || now >= next_frame)) {
            draw_all();
            present();
            pending = 0;
            if (g_tsc_hz)
                next_frame = now + g_tsc_hz / 50;
        }

        if (!pending)
            __asm__ volatile("pause");
    }

    vga_leave_graphics();
    return 0;
}

/*__TILEWM_APPEND__*/
