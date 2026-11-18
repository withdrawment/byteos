#include "kernel.h"
#include "site_data.h"

#define C_BAR   VGA_ATTR(BLACK, LIGHT_CYAN)
#define C_DESK  VGA_ATTR(LIGHT_GRAY, BLUE)
#define C_DIM   VGA_ATTR(LIGHT_GRAY, BLUE)
#define C_WHITE VGA_ATTR(WHITE, BLUE)
#define C_ACC   VGA_ATTR(YELLOW, BLUE)
#define C_ART   VGA_ATTR(LIGHT_MAGENTA, BLUE)
#define C_ART2  VGA_ATTR(LIGHT_CYAN, BLUE)
#define C_SEL   VGA_ATTR(BLACK, LIGHT_GRAY)
#define C_ERR   VGA_ATTR(LIGHT_RED, BLACK)

static int g_desktop_active;

static const char *g_items[] = {
    "Files",
    "Browser",
    "Editor",
    "Terminal",
    "Fetch",
    "Donut",
    "Exit desktop"
};

static const char *g_desc[] = {
    "Browse ByteFS and the bundled live website",
    "Read local HTML files, including www/index.html",
    "Edit HTML files and save them to the disk",
    "Run the full ByteOS shell inside the desktop",
    "Show CPU, memory, uptime and boot information",
    "Spin the z-buffered ASCII donut",
    "Close ByteDesktop and return to the console"
};

#define MENU_X 31
#define MENU_Y 4
#define MENU_W 38

int desktop_active(void)
{
    return g_desktop_active;
}

static void clock_text(char *buf)
{
    rtc_time_t tm;
    if (rtc_read(&tm) != 0) {
        str_copy(buf, "--:--", 6);
        return;
    }
    buf[0] = (char)('0' + tm.hour / 10);
    buf[1] = (char)('0' + tm.hour % 10);
    buf[2] = ':';
    buf[3] = (char)('0' + tm.minute / 10);
    buf[4] = (char)('0' + tm.minute % 10);
    buf[5] = 0;
}

static void draw_topbar(void)
{
    vga_fill_row(0, C_BAR);
    vga_puts_at(0, 1, "ByteOS " BYTEOS_VERSION " \"" BYTEOS_CODENAME "\"", C_BAR);
    vga_puts_at(0, 33, "ByteDesktop", C_BAR);
    char tm[6];
    clock_text(tm);
    vga_puts_at(0, 74, tm, C_BAR);
}

static void draw_box(int top, int left, int height, int width)
{
    uint8_t attr = VGA_ATTR(LIGHT_CYAN, BLACK);
    for (int c = 0; c < width; c++) {
        vga_put_at(top, left + c, '-', attr);
        vga_put_at(top + height - 1, left + c, '-', attr);
    }
    for (int r = 0; r < height; r++) {
        vga_put_at(top + r, left, '|', attr);
        vga_put_at(top + r, left + width - 1, '|', attr);
    }
    vga_put_at(top, left, '+', attr);
    vga_put_at(top, left + width - 1, '+', attr);
    vga_put_at(top + height - 1, left, '+', attr);
    vga_put_at(top + height - 1, left + width - 1, '+', attr);
}

static void draw_desktop(int selected)
{
    console_clear(C_DESK);
    draw_topbar();

    static const char *art[8] = {
        "      .-\"\"\"\"\"-.",
        "    .'  .---.  '.",
        "   /   /     \\   \\",
        "  |   |       |   |",
        "  |   |       |   |",
        "   \\   \\     /   /",
        "    '.  '---'  .'",
        "      '-.....-'"
    };
    for (int i = 0; i < 8; i++)
        vga_puts_at(4 + i, 4, art[i], i < 4 ? C_ART : C_ART2);

    vga_puts_at(13, 5, "BYTEOS", C_ACC);
    vga_puts_at(14, 5, "0.2  Sprinkles", C_DIM);
    vga_puts_at(17, 3, "64-bit bare metal", C_DIM);
    vga_puts_at(18, 3, "BIOS  /  ATA  /  VGA", C_DIM);

    draw_box(MENU_Y - 1, MENU_X - 1, 11, MENU_W + 2);
    vga_puts_at(MENU_Y - 1, MENU_X + 1, " Applications ", VGA_ATTR(YELLOW, BLACK));

    for (int i = 0; i < 7; i++) {
        int row = MENU_Y + 1 + i;
        if (i == selected) {
            for (int c = 0; c < MENU_W; c++)
                vga_put_at(row, MENU_X + c, ' ', C_SEL);
            vga_puts_at(row, MENU_X + 2, g_items[i], C_SEL);
            vga_puts_at(row, MENU_X + MENU_W - 4, "ENT", C_SEL);
        } else {
            for (int c = 0; c < MENU_W; c++)
                vga_put_at(row, MENU_X + c, ' ', VGA_ATTR(LIGHT_GRAY, BLACK));
            vga_puts_at(row, MENU_X + 2, g_items[i], VGA_ATTR(LIGHT_CYAN, BLACK));
        }
    }

    for (int row = 15; row <= 16; row++)
        for (int c = 1; c < VGA_WIDTH - 1; c++)
            vga_put_at(row, c, ' ', C_DESK);
    vga_puts_at(15, 2, g_desc[selected], VGA_ATTR(WHITE, BLUE));

    vga_puts_at(19, 2, "Signed in:", C_DIM);
    char who[48];
    str_copy(who, auth_username(), sizeof(who));
    if (auth_is_root())
        str_copy(who, "root", sizeof(who));
    vga_puts_at(19, 13, who, VGA_ATTR(LIGHT_GREEN, BLUE));
    vga_puts_at(19, 24, "@", C_DIM);
    vga_puts_at(19, 26, auth_hostname(), VGA_ATTR(LIGHT_CYAN, BLUE));

    vga_puts_at(20, 2, "Storage:", C_DIM);
    if (bfs_mounted()) {
        char sz[32];
        uint32_t freeb = bfs_free_bytes();
        sz[0] = 0;
        /* compact free-space text */
        uint32_t kib = freeb / 1024u;
        int n = 0;
        char tmp[16];
        do {
            tmp[n++] = (char)('0' + (kib % 10));
            kib /= 10;
        } while (kib && n < 15);
        int pos = 0;
        sz[pos++] = 'B';
        sz[pos++] = 'y';
        sz[pos++] = 't';
        sz[pos++] = 'e';
        sz[pos++] = 'F';
        sz[pos++] = 'S';
        sz[pos++] = ' ';
        while (n > 0)
            sz[pos++] = tmp[--n];
        sz[pos++] = ' ';
        sz[pos++] = 'K';
        sz[pos++] = 'i';
        sz[pos++] = 'B';
        sz[pos++] = ' ';
        sz[pos++] = 'f';
        sz[pos++] = 'r';
        sz[pos++] = 'e';
        sz[pos++] = 'e';
        sz[pos] = 0;
        vga_puts_at(20, 13, sz, VGA_ATTR(LIGHT_GREEN, BLUE));
    } else {
        vga_puts_at(20, 13, "live ISO (read-only)", VGA_ATTR(YELLOW, BLUE));
    }

    vga_fill_row(24, C_BAR);
    vga_puts_at(24, 2, "Arrows select   Enter open   F10 console", C_BAR);
    vga_puts_at(24, 57, "ByteDesktop 0.2", C_BAR);
}

/* ------------------------------------------------------------- app shell */
void app_screen(const char *title, const char *hint)
{
    console_clear(VGA_ATTR(LIGHT_GRAY, BLACK));
    vga_fill_row(0, C_BAR);
    vga_puts_at(0, 1, title, C_BAR);
    vga_puts_at(0, 64, "Esc / F10 desktop", C_BAR);
    vga_fill_row(24, C_BAR);
    vga_puts_at(24, 2, hint, C_BAR);
    console_set_region(1, 23);
    console_goto(1, 0);
    console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
}

void app_footer(const char *text)
{
    vga_fill_row(24, C_BAR);
    vga_puts_at(24, 2, text, C_BAR);
}

/* --------------------------------------------------------------- terminal */
static void terminal_app(void)
{
    app_screen("ByteOS Terminal", "Type exit to return to ByteDesktop");
    console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
    console_goto(1, 0);
    console_set_attr(VGA_ATTR(LIGHT_CYAN, BLACK));
    console_puts("ByteOS terminal session. The full shell is available here.\n");
    console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
    console_puts("Type \"help\", \"fetch\", \"ls\", or \"exit\".\n\n");
    shell_set_exit_mode(1);
    shell();
    shell_set_exit_mode(0);
}

/* ----------------------------------------------------------- file browser */
typedef struct {
    const char *name;
    uint32_t size;
    int html;
    int builtin;
} file_entry_t;

static file_entry_t g_entries[64];
static int g_entry_count;

static void load_entries(void)
{
    g_entry_count = 0;
    if (bfs_mounted()) {
        for (int i = 0; i < BFS_MAX_FILES && g_entry_count < 64; i++) {
            bfs_file_t *f = bfs_file_at(i);
            if (!f || str_eq(f->name, "etc/passwd"))
                continue;
            file_entry_t *e = &g_entries[g_entry_count++];
            e->name = f->name;
            e->size = f->size;
            e->html = (f->flags & BFS_FILE_HTML) != 0;
            e->builtin = 0;
        }
    } else {
        for (unsigned int i = 0; i < g_site_file_count && g_entry_count < 64; i++) {
            const site_file_t *f = &g_site_files[i];
            file_entry_t *e = &g_entries[g_entry_count++];
            e->name = f->path;
            e->size = f->size;
            e->html = f->html != 0;
            e->builtin = 1;
        }
    }
}

static void files_app(void)
{
    load_entries();
    int selected = 0;
    int scroll = 0;
    char status[80];
    status[0] = 0;

    for (;;) {
        app_screen("Files  -  ByteFS", "Enter open   F4 edit HTML   Esc desktop");
        console_goto(1, 0);
        console_set_attr(VGA_ATTR(LIGHT_CYAN, BLACK));
        kprintf("Location: %s   Files: %d   ",
                bfs_mounted() ? "installed disk" : "live image", g_entry_count);
        if (bfs_mounted()) {
            console_set_attr(VGA_ATTR(LIGHT_GREEN, BLACK));
            kprintf("Free: ");
            print_size(bfs_free_bytes());
        } else {
            console_set_attr(VGA_ATTR(YELLOW, BLACK));
            kprintf("read-only");
        }
        console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));

        for (int row = 2; row <= 21; row++) {
            for (int c = 0; c < VGA_WIDTH; c++)
                vga_put_at(row, c, ' ', VGA_ATTR(LIGHT_GRAY, BLACK));
            int idx = scroll + (row - 2);
            if (idx >= g_entry_count)
                continue;
            file_entry_t *e = &g_entries[idx];
            uint8_t attr = (idx == selected) ? C_SEL : VGA_ATTR(LIGHT_GRAY, BLACK);
            if (idx == selected)
                for (int c = 0; c < VGA_WIDTH; c++)
                    vga_put_at(row, c, ' ', C_SEL);
            vga_puts_at(row, 2, e->html ? "[HTML]" : "[FILE]", attr);
            char name[54];
            str_copy(name, e->name, sizeof(name));
            vga_puts_at(row, 10, name, attr);
            char sz[24];
            /* right-aligned size in bytes/KiB */
            uint32_t v = e->size;
            int n = 0;
            char tmp[16];
            do {
                tmp[n++] = (char)('0' + (v % 10));
                v /= 10;
            } while (v && n < 15);
            int p = 0;
            while (n > 0)
                sz[p++] = tmp[--n];
            sz[p++] = ' ';
            sz[p++] = 'B';
            sz[p] = 0;
            vga_puts_at(row, 66, sz, attr);
            vga_puts_at(row, 74, e->builtin ? "ISO" : "DISK", attr);
        }

        if (status[0]) {
            vga_puts_at(22, 2, status, VGA_ATTR(YELLOW, BLACK));
        } else {
            vga_puts_at(22, 2,
                        bfs_mounted() ? "HTML opens in Browser; F4 opens the Editor."
                                      : "Live files are built into the ISO.",
                        VGA_ATTR(DARK_GRAY, BLACK));
        }

        int key = kbd_waitkey();
        if (key < 0)
            continue;
        if (key == KEY_UP && selected > 0) {
            selected--;
        } else if (key == KEY_DOWN && selected + 1 < g_entry_count) {
            selected++;
        } else if (key == KEY_PGUP) {
            selected -= 10;
            if (selected < 0)
                selected = 0;
        } else if (key == KEY_PGDN) {
            selected += 10;
            if (selected >= g_entry_count)
                selected = g_entry_count ? g_entry_count - 1 : 0;
        } else if (key == KEY_HOME) {
            selected = 0;
        } else if (key == KEY_END) {
            selected = g_entry_count ? g_entry_count - 1 : 0;
        } else if (key == 27 || key == KEY_F10) {
            return;
        } else if ((key == '\n' || key == 'b' || key == 'B') && g_entry_count) {
            file_entry_t *e = &g_entries[selected];
            browser_app(e->name);
            load_entries();
        } else if ((key == KEY_F4 || key == 'e' || key == 'E') && g_entry_count) {
            file_entry_t *e = &g_entries[selected];
            if (!e->html) {
                str_copy(status, "Only HTML files can be edited.", sizeof(status));
            } else if (e->builtin) {
                str_copy(status, "Install to disk to edit this file.", sizeof(status));
            } else {
                editor_app(e->name);
                load_entries();
            }
        }
        if (selected < scroll)
            scroll = selected;
        if (selected >= scroll + 20)
            scroll = selected - 19;
    }
}

/* ---------------------------------------------------------------- desktop */
void desktop(void)
{
    int selected = 0;
    kbd_drain();
    g_desktop_active = 1;
    draw_desktop(selected);

    uint64_t next_tick = g_tsc_hz ? read_tsc() + g_tsc_hz : 0;

    for (;;) {
        int key = kbd_getkey();
        if (key >= 0) {
            if (key == KEY_UP) {
                selected = (selected + 6) % 7;
                draw_desktop(selected);
            } else if (key == KEY_DOWN) {
                selected = (selected + 1) % 7;
                draw_desktop(selected);
            } else if (key == KEY_F10 || key == 27 || key == 'q' || key == 'Q') {
                break;
            } else if (key == '\n') {
                if (selected == 6)
                    break;
                if (selected == 0) {
                    files_app();
                } else if (selected == 1) {
                    browser_app("www/index.html");
                } else if (selected == 2) {
                    editor_app("home/notes.html");
                } else if (selected == 3) {
                    terminal_app();
                } else if (selected == 4) {
                    fetch_screen();
                } else if (selected == 5) {
                    donut_mode();
                }
                draw_desktop(selected);
            } else if (key == 'f' || key == 'F') {
                selected = 0;
                files_app();
                draw_desktop(selected);
            } else if (key == 'b' || key == 'B') {
                selected = 1;
                browser_app("www/index.html");
                draw_desktop(selected);
            } else if (key == 'e' || key == 'E') {
                selected = 2;
                editor_app("home/notes.html");
                draw_desktop(selected);
            } else if (key == 't' || key == 'T') {
                selected = 3;
                terminal_app();
                draw_desktop(selected);
            }
        }

        if (g_tsc_hz && read_tsc() >= next_tick) {
            draw_topbar();
            next_tick += g_tsc_hz;
        }
    }

    g_desktop_active = 0;
    console_clear(VGA_ATTR(LIGHT_GRAY, BLACK));
}
