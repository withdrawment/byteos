#include "kernel.h"
#include "site_data.h"

#define C_BAR   VGA_ATTR(BLACK, LIGHT_CYAN)
#define C_NORM  VGA_ATTR(LIGHT_GRAY, BLACK)
#define C_H1    VGA_ATTR(YELLOW, BLACK)
#define C_H2    VGA_ATTR(LIGHT_CYAN, BLACK)
#define C_RULE  VGA_ATTR(DARK_GRAY, BLACK)
#define C_PRE   VGA_ATTR(LIGHT_GREEN, BLACK)
#define C_LINK  VGA_ATTR(LIGHT_CYAN, BLACK)
#define C_DIM   VGA_ATTR(DARK_GRAY, BLACK)
#define C_WARN  VGA_ATTR(YELLOW, BLACK)

#define PAGE_MAX_LINES 520
#define PAGE_COLS 78
#define PAGE_MAX_LINKS 24
#define PAGE_SRC_MAX 32768

static char g_lines[PAGE_MAX_LINES][PAGE_COLS + 1];
static uint8_t g_page_style[PAGE_MAX_LINES];
static int g_line_count;
static int g_cur;
static int g_col;
static uint8_t g_style;

typedef struct {
    char href[96];
    char label[24];
} page_link_t;

static page_link_t g_links[PAGE_MAX_LINKS];
static int g_link_count;

static char g_src[PAGE_SRC_MAX];
static char g_path[96];
static char g_status[96];
static char g_history[8][96];
static int g_history_count;
static int g_scroll;

static char lower(char c)
{
    if (c >= 'A' && c <= 'Z')
        return (char)(c + 32);
    return c;
}

static int starts_ci(const char *s, const char *prefix)
{
    while (*prefix) {
        if (lower(*s++) != lower(*prefix++))
            return 0;
    }
    return 1;
}

static void build_str(char *dst, size_t cap, const char *a, const char *b)
{
    size_t n = 0;
    while (a && *a && n + 1 < cap)
        dst[n++] = *a++;
    while (b && *b && n + 1 < cap)
        dst[n++] = *b++;
    dst[n] = 0;
}

 
static void page_reset(void)
{
    g_line_count = 1;
    g_cur = 0;
    g_col = 0;
    g_style = 0;
    g_lines[0][0] = 0;
    g_page_style[0] = 0;
}

static void page_newline(void)
{
    if (g_cur + 1 >= PAGE_MAX_LINES)
        return;
    g_cur++;
    g_col = 0;
    g_lines[g_cur][0] = 0;
    g_page_style[g_cur] = g_style;
    if (g_cur + 1 > g_line_count)
        g_line_count = g_cur + 1;
}

static void page_blank(void)
{
    if (g_col > 0)
        page_newline();
    if (g_cur > 0 && g_lines[g_cur - 1][0] != 0)
        page_newline();
}

static void page_raw(char c)
{
    if (g_col >= PAGE_COLS) {
        int split = -1;
        for (int i = g_col - 1; i > 0; i--) {
            if (g_lines[g_cur][i] == ' ') {
                split = i;
                break;
            }
        }
        if (split > 0) {
            char word[PAGE_COLS + 1];
            int n = g_col - split - 1;
            for (int i = 0; i < n; i++)
                word[i] = g_lines[g_cur][split + 1 + i];
            word[n] = 0;
            g_lines[g_cur][split] = 0;
            page_newline();
            for (int i = 0; i < n; i++)
                g_lines[g_cur][i] = word[i];
            g_col = n;
        } else {
            page_newline();
        }
    }
    g_lines[g_cur][g_col++] = c;
    g_lines[g_cur][g_col] = 0;
    g_page_style[g_cur] = g_style;
    if (g_cur + 1 > g_line_count)
        g_line_count = g_cur + 1;
}

static void page_char(char c)
{
    if (g_style == 4) {
        if (c == '\n')
            page_newline();
        else if (c == '\t') {
            for (int i = 0; i < 4; i++)
                page_raw(' ');
        } else {
            page_raw(c);
        }
        return;
    }

    if (c == '\n' || c == '\r' || c == '\t' || c == ' ') {
        if (g_col > 0 && g_lines[g_cur][g_col - 1] != ' ')
            page_raw(' ');
        return;
    }
    page_raw(c);
}

static void page_text(const char *s)
{
    while (*s)
        page_char(*s++);
}

 
static int tag_name_eq(const char *a, const char *b)
{
    return str_eq_ci(a, b);
}

static int parse_tag(const char *s, int *idx, char *name, size_t ncap,
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
            name[n++] = lower(s[i]);
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
                attr[a++] = lower(s[i]);
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
                if (tag_name_eq(attr, "href") && h + 1 < (int)hcap)
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

static void skip_element(const char *s, int *idx, const char *name)
{
    int i = *idx;
    while (s[i]) {
        if (s[i] == '<') {
            char tag[16], href[8];
            int closing = 0;
            int j = i;
            if (parse_tag(s, &j, tag, sizeof(tag), href, sizeof(href),
                          &closing)) {
                if (closing && tag_name_eq(tag, name)) {
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

static void add_link(const char *href)
{
    if (g_link_count >= PAGE_MAX_LINKS || !href[0])
        return;
    page_link_t *l = &g_links[g_link_count++];
    str_copy(l->href, href, sizeof(l->href));
    const char *base = href;
    for (const char *p = href; *p; p++) {
        if (*p == '/')
            base = p + 1;
    }
    str_copy(l->label, base, sizeof(l->label));

    char marker[8];
    marker[0] = ' ';
    marker[1] = '[';
    marker[2] = (char)('0' + g_link_count % 10);
    marker[3] = ']';
    marker[4] = 0;
    if (g_link_count <= 9)
        page_text(marker);
}

static void render_html(const char *s)
{
    page_reset();
    g_link_count = 0;
    int i = 0;
    int in_link = 0;
    char link_href[96];
    link_href[0] = 0;

    while (s[i]) {
        if (s[i] == '<') {
            if (starts_ci(s + i, "<!--")) {
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

            char tag[16], href[96];
            int closing = 0;
            if (!parse_tag(s, &i, tag, sizeof(tag), href, sizeof(href),
                           &closing))
                continue;

            if (!closing && (tag_name_eq(tag, "script") ||
                             tag_name_eq(tag, "style") ||
                             tag_name_eq(tag, "title"))) {
                skip_element(s, &i, tag);
                continue;
            }

            if (tag_name_eq(tag, "a")) {
                if (!closing) {
                    str_copy(link_href, href, sizeof(link_href));
                    in_link = 1;
                } else if (in_link) {
                    add_link(link_href);
                    in_link = 0;
                    link_href[0] = 0;
                }
            } else if (tag_name_eq(tag, "h1")) {
                if (!closing) {
                    page_blank();
                    g_style = 1;
                } else {
                    g_style = 0;
                    page_newline();
                }
            } else if (tag_name_eq(tag, "h2") || tag_name_eq(tag, "h3")) {
                if (!closing) {
                    page_blank();
                    g_style = 2;
                } else {
                    g_style = 0;
                    page_newline();
                }
            } else if (tag_name_eq(tag, "p") || tag_name_eq(tag, "div") ||
                       tag_name_eq(tag, "section") || tag_name_eq(tag, "main") ||
                       tag_name_eq(tag, "header") || tag_name_eq(tag, "footer") ||
                       tag_name_eq(tag, "nav")) {
                if (!closing)
                    page_blank();
                else
                    page_newline();
            } else if (tag_name_eq(tag, "br")) {
                page_newline();
            } else if (tag_name_eq(tag, "hr")) {
                page_blank();
                g_style = 3;
                for (int k = 0; k < 60; k++)
                    page_raw('-');
                g_style = 0;
                page_newline();
                page_newline();
            } else if (tag_name_eq(tag, "li")) {
                if (!closing) {
                    page_blank();
                    page_text(" * ");
                } else {
                    page_newline();
                }
            } else if (tag_name_eq(tag, "ul") || tag_name_eq(tag, "ol")) {
                page_blank();
            } else if (tag_name_eq(tag, "pre")) {
                if (!closing) {
                    page_blank();
                    g_style = 4;
                } else {
                    g_style = 0;
                    page_newline();
                    page_newline();
                }
            } else if (tag_name_eq(tag, "tr")) {
                page_newline();
            } else if (tag_name_eq(tag, "td") || tag_name_eq(tag, "th")) {
                page_text("  |  ");
            } else if (tag_name_eq(tag, "img")) {
                page_text("[image] ");
            }
            continue;
        }

        if (s[i] == '&') {
            char ent[12];
            int n = 0;
            int j = i + 1;
            while (s[j] && s[j] != ';' && n < 11)
                ent[n++] = lower(s[j++]);
            ent[n] = 0;
            if (s[j] == ';') {
                if (str_eq_ci(ent, "amp"))
                    page_char('&');
                else if (str_eq_ci(ent, "lt"))
                    page_char('<');
                else if (str_eq_ci(ent, "gt"))
                    page_char('>');
                else if (str_eq_ci(ent, "quot"))
                    page_char('"');
                else if (str_eq_ci(ent, "nbsp"))
                    page_char(' ');
                else if (str_eq_ci(ent, "apos") || str_eq_ci(ent, "#39"))
                    page_char('\'');
                else
                    page_char('?');
                i = j + 1;
                continue;
            }
        }

        page_char(s[i++]);
    }

    if (in_link)
        add_link(link_href);
    page_newline();
}

 
static int read_page(const char *path, uint32_t *out_len)
{
    if (bfs_mounted()) {
        uint32_t len = 0;
        if (bfs_read(path, g_src, sizeof(g_src), &len) == 0) {
            *out_len = len;
            return 0;
        }
    }
    for (unsigned int i = 0; i < g_site_file_count; i++) {
        if (str_eq_ci(g_site_files[i].path, path)) {
            uint32_t n = g_site_files[i].size;
            if (n >= sizeof(g_src))
                n = sizeof(g_src) - 1;
            memcpy(g_src, g_site_files[i].data, n);
            g_src[n] = 0;
            *out_len = n;
            return 0;
        }
    }
    return -1;
}

static void normalize_path(const char *in, char *out, size_t cap)
{
    while (*in == ' ')
        in++;
    if (starts_ci(in, "byteos://"))
        in += 9;
    while (*in == '/')
        in++;
    if (!*in)
        in = "www/index.html";
    str_copy(out, in, cap);
}

static void path_dir(const char *path, char *out, size_t cap)
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

static void resolve_link(const char *href, char *out, size_t cap)
{
    if (starts_ci(href, "http://") || starts_ci(href, "https://")) {
        str_copy(out, href, cap);
        return;
    }
    if (starts_ci(href, "byteos://")) {
        normalize_path(href, out, cap);
        return;
    }
    if (href[0] == '/') {
        normalize_path(href, out, cap);
        return;
    }
    if (starts_ci(href, "www/") || starts_ci(href, "home/") ||
        starts_ci(href, "etc/") || starts_ci(href, "README")) {
        str_copy(out, href, cap);
        return;
    }
    char dir[64];
    path_dir(g_path, dir, sizeof(dir));
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

static int load_path(const char *path, int push_history)
{
    char normalized[96];
    normalize_path(path, normalized, sizeof(normalized));

    uint32_t len = 0;
    if (read_page(normalized, &len) != 0) {
        build_str(g_status, sizeof(g_status), "Cannot open: ", normalized);
        return -1;
    }

    if (push_history && g_path[0] && g_history_count < 8) {
        str_copy(g_history[g_history_count++], g_path,
                 sizeof(g_history[0]));
    }

    str_copy(g_path, normalized, sizeof(g_path));
    g_status[0] = 0;
    render_html(g_src);
    g_scroll = 0;
    return 0;
}

static void follow_link(int index)
{
    if (index < 0 || index >= g_link_count)
        return;
    const char *href = g_links[index].href;
    if (starts_ci(href, "http://") || starts_ci(href, "https://")) {
        build_str(g_status, sizeof(g_status), "External URL (no network): ", href);
        return;
    }
    char resolved[96];
    resolve_link(href, resolved, sizeof(resolved));
    if (load_path(resolved, 1) != 0)
        return;
    g_scroll = 0;
}

static void browser_draw(void)
{
    app_screen("ByteOS Browser", "1-9 link  u address  Backspace back  r reload  e edit  Esc desktop");

    vga_fill_row(1, VGA_ATTR(BLACK, LIGHT_GRAY));
    vga_puts_at(1, 1, "byteos://", VGA_ATTR(BLACK, LIGHT_GRAY));
    vga_puts_at(1, 10, g_path, VGA_ATTR(BLACK, LIGHT_GRAY));

    for (int row = 0; row < 20; row++) {
        int idx = g_scroll + row;
        for (int c = 0; c < VGA_WIDTH; c++)
            vga_put_at(2 + row, c, ' ', C_NORM);
        if (idx < g_line_count) {
            uint8_t attr = C_NORM;
            if (g_page_style[idx] == 1)
                attr = C_H1;
            else if (g_page_style[idx] == 2)
                attr = C_H2;
            else if (g_page_style[idx] == 3)
                attr = C_RULE;
            else if (g_page_style[idx] == 4)
                attr = C_PRE;
            vga_puts_at(2 + row, 0, g_lines[idx], attr);
        }
    }

    vga_fill_row(22, VGA_ATTR(LIGHT_GRAY, BLACK));
    vga_puts_at(22, 1, "Links:", C_DIM);
    int col = 8;
    for (int i = 0; i < g_link_count && i < 5; i++) {
        char item[32];
        item[0] = (char)('1' + i);
        item[1] = ':';
        str_copy(item + 2, g_links[i].label, sizeof(item) - 3);
        vga_puts_at(22, col, item, C_LINK);
        col += (int)kstrlen(item) + 2;
        if (col >= 76)
            break;
    }

    vga_fill_row(23, VGA_ATTR(LIGHT_GRAY, BLACK));
    if (g_status[0]) {
        vga_puts_at(23, 1, g_status, C_WARN);
    } else {
        char info[64];
        char num[8];
        int n = 0;
        int v = g_scroll + 1;
        char tmp[8];
        int t = 0;
        do {
            tmp[t++] = (char)('0' + v % 10);
            v /= 10;
        } while (v && t < 7);
        while (t > 0)
            num[n++] = tmp[--t];
        num[n] = 0;
        build_str(info, sizeof(info), "line ", num);
        vga_puts_at(23, 1, info, C_DIM);
        char alt[32];
        n = 0;
        v = g_line_count;
        char tmp2[8];
        t = 0;
        do {
            tmp2[t++] = (char)('0' + v % 10);
            v /= 10;
        } while (v && t < 7);
        while (t > 0)
            alt[n++] = tmp2[--t];
        alt[n] = 0;
        vga_puts_at(23, 12, "/ ", C_DIM);
        vga_puts_at(23, 14, alt, C_DIM);
    }
    app_footer("1-9 link  u address  Backspace back  r reload  e edit  Esc desktop");
}

void browser_app(const char *start_path)
{
    g_path[0] = 0;
    g_history_count = 0;
    g_scroll = 0;
    g_status[0] = 0;
    load_path(start_path && start_path[0] ? start_path : "www/index.html", 0);

    for (;;) {
        browser_draw();
        int key = kbd_waitkey();
        if (key < 0)
            continue;
        if (key == 27 || key == KEY_F10)
            return;
        if (key == KEY_UP) {
            if (g_scroll > 0)
                g_scroll--;
        } else if (key == KEY_DOWN) {
            if (g_scroll + 20 < g_line_count)
                g_scroll++;
        } else if (key == KEY_PGUP) {
            g_scroll -= 10;
            if (g_scroll < 0)
                g_scroll = 0;
        } else if (key == KEY_PGDN) {
            g_scroll += 10;
            if (g_scroll + 20 >= g_line_count)
                g_scroll = g_line_count > 20 ? g_line_count - 20 : 0;
        } else if (key == KEY_HOME) {
            g_scroll = 0;
        } else if (key == KEY_END) {
            g_scroll = g_line_count > 20 ? g_line_count - 20 : 0;
        } else if (key >= '1' && key <= '9') {
            follow_link(key - '1');
        } else if (key == 'u' || key == 'U') {
            char path[64];
            vga_fill_row(23, VGA_ATTR(BLACK, LIGHT_GRAY));
            vga_puts_at(23, 1, "Open: byteos://", VGA_ATTR(BLACK, LIGHT_GRAY));
            console_set_region(1, 24);
            console_goto(23, 17);
            console_set_attr(VGA_ATTR(BLACK, LIGHT_GRAY));
            path[0] = 0;
            console_read_line(path, sizeof(path), 0);
            console_set_region(1, 23);
            if (path[0])
                load_path(path, 1);
        } else if (key == '\b') {
            if (g_history_count > 0) {
                char previous[96];
                str_copy(previous, g_history[--g_history_count],
                         sizeof(previous));
                load_path(previous, 0);
            }
        } else if (key == 'r' || key == 'R' || key == KEY_F5) {
            load_path(g_path, 0);
        } else if (key == 'h' || key == 'H') {
            load_path("www/index.html", 1);
        } else if (key == 'e' || key == 'E') {
            if (!bfs_mounted()) {
                str_copy(g_status, "Install to disk to edit local HTML.",
                         sizeof(g_status));
            } else if (!bfs_is_html(g_path)) {
                str_copy(g_status, "The editor only opens HTML files.",
                         sizeof(g_status));
            } else {
                editor_app(g_path);
                load_path(g_path, 0);
            }
        }
    }
}
