#include "kernel.h"

#define C_LABEL VGA_ATTR(LIGHT_CYAN, BLACK)
#define C_VALUE VGA_ATTR(WHITE, BLACK)
#define C_DIM   VGA_ATTR(LIGHT_GRAY, BLACK)
#define C_WARN  VGA_ATTR(LIGHT_RED, BLACK)
#define C_GOOD  VGA_ATTR(LIGHT_GREEN, BLACK)
#define C_BAR   VGA_ATTR(BLACK, LIGHT_CYAN)
#define AUTH_USER_MAX 32
#define AUTH_HOST_MAX 32

typedef struct {
    int valid;
    char user[AUTH_USER_MAX];
    char hash[17];
    char root[17];
    char host[AUTH_HOST_MAX];
} auth_config_t;

static auth_config_t g_cfg;
static char g_user[AUTH_USER_MAX] = "live";
static int g_root;

static uint64_t hash_password(const char *s)
{
    uint64_t h = 1469598103934665603ULL;
    while (*s) {
        h ^= (unsigned char)*s++;
        h *= 1099511628211ULL;
    }
    return h;
}

static void hash_hex(const char *s, char out[17])
{
    static const char hex[] = "0123456789abcdef";
    uint64_t h = hash_password(s);
    for (int i = 15; i >= 0; i--) {
        out[i] = hex[h & 0x0F];
        h >>= 4;
    }
    out[16] = 0;
}

static int verify_hash(const char *pw, const char *expect)
{
    char got[17];
    hash_hex(pw, got);
    return memcmp(got, expect, 17) == 0;
}

static int starts_with(const char *s, const char *prefix)
{
    while (*prefix) {
        if (*s++ != *prefix++)
            return 0;
    }
    return 1;
}

static int parse_key(const char *line, const char *key, char *out, size_t cap)
{
    size_t n = kstrlen(key);
    if (!starts_with(line, key) || line[n] != '=')
        return 0;
    str_copy(out, line + n + 1, cap);
    return 1;
}

int auth_load(void)
{
    memset(&g_cfg, 0, sizeof(g_cfg));
    if (!bfs_mounted())
        return -1;

    static char buf[1024];
    if (bfs_read("etc/passwd", buf, sizeof(buf), 0) != 0)
        return -1;

    char *p = buf;
    while (*p) {
        char *end = p;
        while (*end && *end != '\n')
            end++;
        char saved = *end;
        *end = 0;
        if (*p && *p != '#') {
            parse_key(p, "user", g_cfg.user, sizeof(g_cfg.user));
            parse_key(p, "hash", g_cfg.hash, sizeof(g_cfg.hash));
            parse_key(p, "root", g_cfg.root, sizeof(g_cfg.root));
            parse_key(p, "host", g_cfg.host, sizeof(g_cfg.host));
        }
        *end = saved;
        p = *end ? end + 1 : end;
    }

    if (!g_cfg.user[0] || !g_cfg.hash[0] || !g_cfg.root[0])
        return -1;
    if (!g_cfg.host[0])
        str_copy(g_cfg.host, "byteos", sizeof(g_cfg.host));
    g_cfg.valid = 1;
    str_copy(g_user, g_cfg.user, sizeof(g_user));
    g_root = 0;
    return 0;
}

int auth_configured(void)
{
    return g_cfg.valid;
}

const char *auth_username(void)
{
    return g_user;
}

const char *auth_hostname(void)
{
    return g_cfg.valid ? g_cfg.host : "byteos";
}

int auth_is_root(void)
{
    return g_root;
}

static int append(char *dst, size_t cap, size_t *pos, const char *s)
{
    while (*s) {
        if (*pos + 1 >= cap)
            return -1;
        dst[(*pos)++] = *s++;
    }
    dst[*pos] = 0;
    return 0;
}

static int save_config(void)
{
    char buf[512];
    size_t n = 0;
    buf[0] = 0;
    append(buf, sizeof(buf), &n, "# ByteOS account database v1\n");
    append(buf, sizeof(buf), &n, "user=");
    append(buf, sizeof(buf), &n, g_cfg.user);
    append(buf, sizeof(buf), &n, "\nhash=");
    append(buf, sizeof(buf), &n, g_cfg.hash);
    append(buf, sizeof(buf), &n, "\nroot=");
    append(buf, sizeof(buf), &n, g_cfg.root);
    append(buf, sizeof(buf), &n, "\nhost=");
    append(buf, sizeof(buf), &n, g_cfg.host);
    append(buf, sizeof(buf), &n, "\n");
    if (!bfs_mounted())
        return -1;
    return bfs_write("etc/passwd", buf, (uint32_t)n);
}

int auth_set_credentials(const char *user, const char *password,
                         const char *root_password, const char *host)
{
    memset(&g_cfg, 0, sizeof(g_cfg));
    str_copy(g_cfg.user, user, sizeof(g_cfg.user));
    str_copy(g_cfg.host, host && host[0] ? host : "byteos", sizeof(g_cfg.host));
    hash_hex(password, g_cfg.hash);
    hash_hex(root_password, g_cfg.root);
    g_cfg.valid = 1;
    if (save_config() != 0) {
        g_cfg.valid = 0;
        return -1;
    }
    return 0;
}

static void wait_enter(void)
{
    console_set_attr(C_DIM);
    console_puts("Press Enter to continue...");
    console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
    for (;;) {
        int c = kbd_getchar();
        if (c == '\n')
            break;
    }
}

static void login_screen(void)
{
    console_clear(VGA_ATTR(LIGHT_GRAY, BLUE));
    vga_fill_row(0, C_BAR);
    vga_puts_at(0, 1, "ByteOS " BYTEOS_VERSION "  /  sign in", C_BAR);
    vga_puts_at(0, VGA_WIDTH - 19, "x86_64 / BIOS", C_BAR);

    const char *title = "B Y T E O S";
    vga_puts_at(9, (VGA_WIDTH - (int)kstrlen(title)) / 2, title,
                VGA_ATTR(YELLOW, BLUE));
    const char *sub = "64-bit bare-metal system";
    vga_puts_at(10, (VGA_WIDTH - (int)kstrlen(sub)) / 2, sub,
                VGA_ATTR(LIGHT_GRAY, BLUE));

    for (int r = 13; r <= 19; r++)
        for (int c = 18; c <= 61; c++)
            vga_put_at(r, c, ' ', VGA_ATTR(LIGHT_GRAY, BLACK));
    vga_puts_at(13, 20, "+----------------------------------------+",
                VGA_ATTR(CYAN, BLACK));
    vga_puts_at(19, 20, "+----------------------------------------+",
                VGA_ATTR(CYAN, BLACK));
    for (int r = 14; r <= 18; r++) {
        vga_put_at(r, 20, '|', VGA_ATTR(CYAN, BLACK));
        vga_put_at(r, 61, '|', VGA_ATTR(CYAN, BLACK));
    }
    vga_puts_at(14, 22, "ByteOS login", VGA_ATTR(YELLOW, BLACK));

    console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
    console_goto(16, 22);
    console_puts("username: ");
    console_set_attr(VGA_ATTR(WHITE, BLACK));
}

int auth_login(void)
{
    if (!g_cfg.valid)
        return -1;

    char user[AUTH_USER_MAX];
    char pass[64];

    for (;;) {
        login_screen();
        user[0] = 0;
        console_read_line(user, sizeof(user), 0);

        console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
        console_goto(17, 22);
        console_puts("password: ");
        console_set_attr(VGA_ATTR(WHITE, BLACK));
        pass[0] = 0;
        console_read_line(pass, sizeof(pass), 1);

        int ok = 0;
        if (str_eq_ci(user, g_cfg.user) && verify_hash(pass, g_cfg.hash)) {
            ok = 1;
            g_root = 0;
            str_copy(g_user, g_cfg.user, sizeof(g_user));
        } else if (str_eq_ci(user, "root") && verify_hash(pass, g_cfg.root)) {
            ok = 1;
            g_root = 1;
            str_copy(g_user, "root", sizeof(g_user));
        }

        if (ok) {
            console_goto(21, 0);
            console_set_attr(C_GOOD);
            console_puts("  Access granted. Starting ByteOS...");
            console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
            return 0;
        }

        console_goto(21, 0);
        console_set_attr(C_WARN);
        console_puts("  Login incorrect.");
        console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
        wait_enter();
    }
}

int auth_su(void)
{
    if (!g_cfg.valid) {
        console_set_attr(C_WARN);
        kprintf("su: no installed account database\n");
        console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
        return -1;
    }

    char pass[64];
    console_set_attr(C_LABEL);
    console_puts("root password: ");
    console_set_attr(VGA_ATTR(WHITE, BLACK));
    console_read_line(pass, sizeof(pass), 1);
    if (!verify_hash(pass, g_cfg.root)) {
        console_set_attr(C_WARN);
        kprintf("su: authentication failure\n");
        console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
        return -1;
    }
    g_root = 1;
    str_copy(g_user, "root", sizeof(g_user));
    console_set_attr(C_GOOD);
    kprintf("now root\n");
    console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
    return 0;
}

int auth_passwd(void)
{
    if (!g_cfg.valid) {
        console_set_attr(C_WARN);
        kprintf("passwd: only available on an installed ByteOS disk\n");
        console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
        return -1;
    }

    char current[64], new1[64], new2[64];
    console_set_attr(C_LABEL);
    console_puts("current password: ");
    console_set_attr(VGA_ATTR(WHITE, BLACK));
    console_read_line(current, sizeof(current), 1);

    const char *expect = g_root ? g_cfg.root : g_cfg.hash;
    if (!verify_hash(current, expect)) {
        console_set_attr(C_WARN);
        kprintf("passwd: authentication failure\n");
        console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
        return -1;
    }

    console_set_attr(C_LABEL);
    console_puts("new password: ");
    console_set_attr(VGA_ATTR(WHITE, BLACK));
    console_read_line(new1, sizeof(new1), 1);
    console_set_attr(C_LABEL);
    console_puts("repeat new password: ");
    console_set_attr(VGA_ATTR(WHITE, BLACK));
    console_read_line(new2, sizeof(new2), 1);

    if (!new1[0] || !str_eq(new1, new2)) {
        console_set_attr(C_WARN);
        kprintf("passwd: passwords do not match\n");
        console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
        return -1;
    }

    if (g_root)
        hash_hex(new1, g_cfg.root);
    else
        hash_hex(new1, g_cfg.hash);

    if (save_config() != 0) {
        console_set_attr(C_WARN);
        kprintf("passwd: could not write account database\n");
        console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
        return -1;
    }
    console_set_attr(C_GOOD);
    kprintf("password updated\n");
    console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
    return 0;
}
