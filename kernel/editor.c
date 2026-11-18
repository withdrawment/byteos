#include "kernel.h"
#include "site_data.h"

#define C_NORM VGA_ATTR(LIGHT_GRAY, BLACK)
#define C_EDIT VGA_ATTR(WHITE, BLACK)
#define C_DIM  VGA_ATTR(DARK_GRAY, BLACK)
#define C_ACC  VGA_ATTR(YELLOW, BLACK)
#define C_GOOD VGA_ATTR(LIGHT_GREEN, BLACK)
#define C_WARN VGA_ATTR(LIGHT_RED, BLACK)
#define C_LINE VGA_ATTR(LIGHT_GRAY, BLACK)

#define EDIT_CAP 16384
#define EDIT_ROWS 20
#define EDIT_COLS 76

static char g_buf[EDIT_CAP];
static int g_len;
static int g_cursor;
static int g_scroll;
static int g_modified;
static int g_readonly;
static char g_path[96];
static char g_status[96];

static int ends_ci(const char *s, const char *suffix)
{
    size_t n = kstrlen(s);
    size_t m = kstrlen(suffix);
    if (m > n)
        return 0;
    return str_eq_ci(s + n - m, suffix);
}

static int line_start_at(int pos)
{
    while (pos > 0 && g_buf[pos - 1] != '\n')
        pos--;
    return pos;
}

static int line_end_at(int pos)
{
    while (pos < g_len && g_buf[pos] != '\n')
        pos++;
    return pos;
}

static int line_number_at(int pos)
{
    int line = 0;
    for (int i = 0; i < pos && i < g_len; i++) {
        if (g_buf[i] == '\n')
            line++;
    }
    return line;
}

static void status(const char *s)
{
    str_copy(g_status, s, sizeof(g_status));
}

static int load_file(const char *path)
{
    g_readonly = 0;
    g_len = 0;
    g_cursor = 0;
    g_scroll = 0;
    g_modified = 0;
    g_status[0] = 0;
    str_copy(g_path, path, sizeof(g_path));

    if (!ends_ci(path, ".html") && !ends_ci(path, ".htm")) {
        status("The editor only supports HTML files.");
        return -1;
    }

    if (bfs_mounted()) {
        uint32_t len = 0;
        if (bfs_read(path, g_buf, sizeof(g_buf), &len) == 0) {
            g_len = (int)len;
            return 0;
        }
    }

    for (unsigned int i = 0; i < g_site_file_count; i++) {
        if (str_eq_ci(g_site_files[i].path, path)) {
            g_len = (int)g_site_files[i].size;
            if (g_len >= EDIT_CAP)
                g_len = EDIT_CAP - 1;
            memcpy(g_buf, g_site_files[i].data, (size_t)g_len);
            g_readonly = 1;
            status("Live ISO copy - install to disk to save changes.");
            return 0;
        }
    }

    if (str_eq_ci(path, "home/notes.html")) {
        static const char sample[] =
            "<!DOCTYPE html><html><head><title>Notes</title></head><body>\n"
            "<h1>My notes</h1>\n"
            "<p>This is a live editor buffer. Install ByteOS to disk to save.</p>\n"
            "<h2>Todo</h2>\n"
            "<ul><li>Install ByteOS</li><li>Edit this page</li></ul>\n"
            "</body></html>\n";
        g_len = (int)(sizeof(sample) - 1);
        memcpy(g_buf, sample, (size_t)g_len);
        g_readonly = 1;
        status("Live sample - install to disk to save changes.");
        return 0;
    }

    status("Cannot open file.");
    return -1;
}

static int save_file(void)
{
    if (g_readonly || !bfs_mounted()) {
        status("Read-only: install ByteOS to disk to save.");
        return -1;
    }
    if (bfs_write(g_path, g_buf, (uint32_t)g_len) != 0) {
        status("Save failed: disk full or I/O error.");
        return -1;
    }
    g_modified = 0;
    g_readonly = 0;
    status("Saved to ByteFS.");
    return 0;
}

static void editor_draw(void)
{
    app_screen("ByteOS Editor", "Arrows move  Ctrl+S save  Esc desktop");

    vga_fill_row(1, VGA_ATTR(BLACK, LIGHT_GRAY));
    vga_puts_at(1, 1, "Editing:", VGA_ATTR(BLACK, LIGHT_GRAY));
    vga_puts_at(1, 10, g_path, VGA_ATTR(BLACK, LIGHT_GRAY));
    if (g_modified)
        vga_puts_at(1, 69, "[modified]", VGA_ATTR(RED, LIGHT_GRAY));

    int cur_line = line_number_at(g_cursor);
    if (cur_line < g_scroll)
        g_scroll = cur_line;
    if (cur_line >= g_scroll + EDIT_ROWS)
        g_scroll = cur_line - EDIT_ROWS + 1;
    if (g_scroll < 0)
        g_scroll = 0;

    int pos = 0;
    for (int i = 0; i < g_scroll && pos < g_len; i++) {
        while (pos < g_len && g_buf[pos] != '\n')
            pos++;
        if (pos < g_len)
            pos++;
    }

    for (int row = 0; row < EDIT_ROWS; row++) {
        for (int c = 0; c < VGA_WIDTH; c++)
            vga_put_at(2 + row, c, ' ', C_NORM);
        if (pos > g_len)
            continue;

        int line_end = line_end_at(pos);
        int col = 1;
        int visual = 0;
        for (int i = pos; i < line_end && col < 1 + EDIT_COLS; i++) {
            char ch = g_buf[i];
            if (ch == '\t') {
                int spaces = 4 - (visual & 3);
                for (int s = 0; s < spaces && col < 1 + EDIT_COLS; s++) {
                    vga_put_at(2 + row, col++, ' ', C_EDIT);
                    visual++;
                }
            } else {
                vga_put_at(2 + row, col++, (ch >= 32 && ch < 127) ? ch : ' ',
                           C_EDIT);
                visual++;
            }
        }
        if (pos < g_len)
            pos = line_end + 1;
        else
            pos = g_len + 1;
    }

    int row = 2 + (cur_line - g_scroll);
    int cstart = line_start_at(g_cursor);
    int display_col = g_cursor - cstart;
    int cc = 1 + display_col;
    if (cc > EDIT_COLS)
        cc = EDIT_COLS;
    console_goto(row, cc);

    vga_fill_row(22, VGA_ATTR(LIGHT_GRAY, BLACK));
    if (g_status[0]) {
        vga_puts_at(22, 1, g_status, C_ACC);
    } else {
        vga_puts_at(22, 1, g_readonly ? "READ-ONLY" : "ByteFS document",
                    g_readonly ? C_WARN : C_DIM);
        vga_puts_at(22, 20, "Ctrl+S / F2 saves", C_DIM);
    }

    char num[16];
    int n = 0;
    int v = cur_line + 1;
    char tmp[8];
    int t = 0;
    do {
        tmp[t++] = (char)('0' + v % 10);
        v /= 10;
    } while (v && t < 7);
    while (t > 0)
        num[n++] = tmp[--t];
    num[n] = 0;

    vga_fill_row(23, VGA_ATTR(LIGHT_GRAY, BLACK));
    vga_puts_at(23, 1, "line ", C_LINE);
    vga_puts_at(23, 6, num, C_LINE);
    vga_puts_at(23, 14, "  bytes ", C_LINE);
    n = 0;
    v = g_len;
    t = 0;
    do {
        tmp[t++] = (char)('0' + v % 10);
        v /= 10;
    } while (v && t < 7);
    while (t > 0)
        num[n++] = tmp[--t];
    num[n] = 0;
    vga_puts_at(23, 22, num, C_LINE);
    if (g_readonly)
        vga_puts_at(23, 40, "live / read-only", C_WARN);
    else
        vga_puts_at(23, 40, g_modified ? "unsaved changes" : "saved",
                    g_modified ? C_ACC : C_GOOD);

    app_footer("Arrows move  PgUp/PgDn  Ctrl+S save  Esc desktop");
}

static void insert_char(char c)
{
    if (g_len + 1 >= EDIT_CAP)
        return;
    memmove(g_buf + g_cursor + 1, g_buf + g_cursor,
            (size_t)(g_len - g_cursor + 1));
    g_buf[g_cursor++] = c;
    g_len++;
    g_modified = 1;
}

static void delete_back(void)
{
    if (g_cursor <= 0)
        return;
    memmove(g_buf + g_cursor - 1, g_buf + g_cursor,
            (size_t)(g_len - g_cursor + 1));
    g_cursor--;
    g_len--;
    g_modified = 1;
}

static void delete_at(void)
{
    if (g_cursor >= g_len)
        return;
    memmove(g_buf + g_cursor, g_buf + g_cursor + 1,
            (size_t)(g_len - g_cursor));
    g_len--;
    g_modified = 1;
}

static void move_up(void)
{
    int start = line_start_at(g_cursor);
    if (start == 0)
        return;
    int col = g_cursor - start;
    int prev_end = start - 1;
    int prev_start = line_start_at(prev_end);
    int prev_len = prev_end - prev_start;
    g_cursor = prev_start + (col < prev_len ? col : prev_len);
}

static void move_down(void)
{
    int end = line_end_at(g_cursor);
    if (end >= g_len)
        return;
    int start = line_start_at(g_cursor);
    int col = g_cursor - start;
    int next_start = end + 1;
    int next_end = line_end_at(next_start);
    int next_len = next_end - next_start;
    g_cursor = next_start + (col < next_len ? col : next_len);
}

void editor_app(const char *path)
{
    load_file(path);

    for (;;) {
        editor_draw();
        int key = kbd_waitkey();
        if (key < 0)
            continue;

        if (key == 27 || key == KEY_F10) {
            if (g_modified && !g_readonly) {
                status("Unsaved changes. Press Esc again to discard.");
                editor_draw();
                int confirm = kbd_waitkey();
                if (confirm == 27 || confirm == KEY_F10)
                    return;
                continue;
            }
            return;
        } else if (key == KEY_UP) {
            move_up();
        } else if (key == KEY_DOWN) {
            move_down();
        } else if (key == KEY_LEFT) {
            if (g_cursor > 0)
                g_cursor--;
        } else if (key == KEY_RIGHT) {
            if (g_cursor < g_len)
                g_cursor++;
        } else if (key == KEY_HOME) {
            g_cursor = line_start_at(g_cursor);
        } else if (key == KEY_END) {
            g_cursor = line_end_at(g_cursor);
        } else if (key == KEY_PGUP) {
            for (int i = 0; i < 20; i++)
                move_up();
        } else if (key == KEY_PGDN) {
            for (int i = 0; i < 20; i++)
                move_down();
        } else if (key == KEY_DEL) {
            delete_at();
        } else if (key == KEY_F2 || key == 19) {
            save_file();
        } else if (key == '\n') {
            insert_char('\n');
        } else if (key == '\b') {
            delete_back();
        } else if (key == '\t') {
            insert_char(' ');
            insert_char(' ');
            insert_char(' ');
            insert_char(' ');
        } else if (key >= 32 && key < 127) {
            insert_char((char)key);
        }
    }
}
