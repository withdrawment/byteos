#include "kernel.h"

 



typedef struct __attribute__((packed)) {
    char magic[8];
    uint32_t version;
    uint32_t max_files;
    uint32_t part_lba;
    uint32_t data_lba;
    uint32_t next_lba;
    uint32_t end_lba;
    uint32_t reserved[4];
    bfs_file_t files[BFS_MAX_FILES];
} bfs_super_t;

 

#define BFS_SUPER_SECTORS 4u

static bfs_super_t g_super;
static int g_mounted;
static int g_slot = -1;

static int name_eq(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z')
            ca = (char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z')
            cb = (char)(cb + 32);
        if (ca != cb)
            return 0;
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

static int ends_with_ci(const char *s, const char *suffix)
{
    size_t n = kstrlen(s);
    size_t m = kstrlen(suffix);
    if (m > n)
        return 0;
    return name_eq(s + (n - m), suffix);
}

static int write_super(void)
{
    ata_disk_t *d = ata_slot(g_slot);
    if (!d)
        return -1;
    uint8_t block[BFS_SUPER_SECTORS * 512];
    memset(block, 0, sizeof(block));
    memcpy(block, &g_super, sizeof(g_super));
    if (ata_write_sectors(d, g_super.part_lba, BFS_SUPER_SECTORS, block) != 0)
        return -1;
    ata_flush(d);
    return 0;
}

int bfs_mount(int slot, uint32_t part_lba)
{
    ata_disk_t *d = ata_slot(slot);
    g_mounted = 0;
    g_slot = -1;
    if (!d || part_lba + 8 >= d->sectors)
        return -1;

    uint8_t block[BFS_SUPER_SECTORS * 512];
    if (ata_read_sectors(d, part_lba, BFS_SUPER_SECTORS, block) != 0)
        return -1;
    memcpy(&g_super, block, sizeof(g_super));

    if (memcmp(g_super.magic, "BYTEFS1", 8) != 0)
        return -1;
    if (g_super.version != 1 || g_super.max_files != BFS_MAX_FILES)
        return -1;
    if (g_super.part_lba != part_lba || g_super.next_lba < g_super.data_lba ||
        g_super.next_lba > g_super.end_lba)
        return -1;

    g_slot = slot;
    g_mounted = 1;
    return 0;
}

int bfs_format(int slot, uint32_t part_lba, uint32_t end_lba)
{
    ata_disk_t *d = ata_slot(slot);
    if (!d || end_lba <= part_lba + 8 || end_lba > d->sectors)
        return -1;

    memset(&g_super, 0, sizeof(g_super));
    memcpy(g_super.magic, "BYTEFS1", 8);
    g_super.version = 1;
    g_super.max_files = BFS_MAX_FILES;
    g_super.part_lba = part_lba;
    g_super.data_lba = part_lba + 8;
    g_super.next_lba = g_super.data_lba;
    g_super.end_lba = end_lba;

    g_slot = slot;
    g_mounted = 1;
    if (write_super() != 0) {
        g_mounted = 0;
        return -1;
    }
    return 0;
}

int bfs_mounted(void)
{
    return g_mounted;
}

int bfs_slot(void)
{
    return g_slot;
}

int bfs_file_count(void)
{
    int n = 0;
    for (int i = 0; i < BFS_MAX_FILES; i++) {
        if (g_super.files[i].flags & BFS_FILE_USED)
            n++;
    }
    return n;
}

bfs_file_t *bfs_file_at(int index)
{
    if (index < 0 || index >= BFS_MAX_FILES)
        return 0;
    if (!(g_super.files[index].flags & BFS_FILE_USED))
        return 0;
    return &g_super.files[index];
}

int bfs_lookup(const char *path)
{
    if (!g_mounted || !path)
        return -1;
    while (*path == '/')
        path++;
    for (int i = 0; i < BFS_MAX_FILES; i++) {
        if (!(g_super.files[i].flags & BFS_FILE_USED))
            continue;
        if (name_eq(g_super.files[i].name, path))
            return i;
    }
    return -1;
}

int bfs_read(const char *path, void *buf, uint32_t cap, uint32_t *out_len)
{
    int idx = bfs_lookup(path);
    if (idx < 0)
        return -1;
    bfs_file_t *f = &g_super.files[idx];
    if (cap < f->size + 1)
        return -1;

    ata_disk_t *d = ata_slot(g_slot);
    if (!d)
        return -1;

    uint8_t *out = (uint8_t *)buf;
    uint32_t left = f->size;
    uint32_t lba = f->start_lba;
    uint8_t sector[512];
    while (left) {
        if (ata_read_sectors(d, lba, 1, sector) != 0)
            return -1;
        uint32_t n = left < 512 ? left : 512;
        memcpy(out, sector, n);
        out += n;
        left -= n;
        lba++;
    }
    *out = 0;
    if (out_len)
        *out_len = f->size;
    return 0;
}

int bfs_add(const char *path, const void *data, uint32_t len, int is_html)
{
    if (!g_mounted || !path || !data)
        return -1;
    if (kstrlen(path) >= BFS_NAME_LEN || kstrlen(path) == 0)
        return -1;

    int idx = -1;
    for (int i = 0; i < BFS_MAX_FILES; i++) {
        if (!(g_super.files[i].flags & BFS_FILE_USED)) {
            idx = i;
            break;
        }
    }
    if (idx < 0)
        return -1;

    uint32_t blocks = (len + 511) / 512;
    if (blocks == 0)
        blocks = 1;
    if (g_super.next_lba + blocks > g_super.end_lba)
        return -1;

    ata_disk_t *d = ata_slot(g_slot);
    if (!d)
        return -1;

    uint32_t start = g_super.next_lba;
    uint32_t lba = start;
    const uint8_t *in = (const uint8_t *)data;
    uint32_t left = len;
    uint8_t sector[512];
    while (left) {
        memset(sector, 0, sizeof(sector));
        uint32_t n = left < 512 ? left : 512;
        memcpy(sector, in, n);
        if (ata_write_sectors(d, lba, 1, sector) != 0)
            return -1;
        in += n;
        left -= n;
        lba++;
    }
    if (len == 0) {
        memset(sector, 0, sizeof(sector));
        if (ata_write_sectors(d, lba, 1, sector) != 0)
            return -1;
    }

    bfs_file_t *f = &g_super.files[idx];
    memset(f, 0, sizeof(*f));
    str_copy(f->name, path, BFS_NAME_LEN);
    f->start_lba = start;
    f->size = len;
    f->flags = BFS_FILE_USED | (is_html ? BFS_FILE_HTML : 0);

    g_super.next_lba = start + blocks;
    if (write_super() != 0) {
        memset(f, 0, sizeof(*f));
        return -1;
    }
    return 0;
}

int bfs_write(const char *path, const void *data, uint32_t len)
{
    if (!g_mounted)
        return -1;
    int idx = bfs_lookup(path);
    if (idx < 0)
        return bfs_add(path, data, len, bfs_is_html(path));

    uint32_t blocks = (len + 511) / 512;
    if (blocks == 0)
        blocks = 1;
    if (g_super.next_lba + blocks > g_super.end_lba)
        return -1;

    ata_disk_t *d = ata_slot(g_slot);
    if (!d)
        return -1;

    uint32_t start = g_super.next_lba;
    uint32_t lba = start;
    const uint8_t *in = (const uint8_t *)data;
    uint32_t left = len;
    uint8_t sector[512];
    while (left) {
        memset(sector, 0, sizeof(sector));
        uint32_t n = left < 512 ? left : 512;
        memcpy(sector, in, n);
        if (ata_write_sectors(d, lba, 1, sector) != 0)
            return -1;
        in += n;
        left -= n;
        lba++;
    }

    g_super.files[idx].start_lba = start;
    g_super.files[idx].size = len;
    g_super.next_lba = start + blocks;
    if (write_super() != 0)
        return -1;
    return 0;
}

uint32_t bfs_free_bytes(void)
{
    if (!g_mounted || g_super.end_lba <= g_super.next_lba)
        return 0;
    return (g_super.end_lba - g_super.next_lba) * 512u;
}

int bfs_is_html(const char *path)
{
    return ends_with_ci(path, ".html") || ends_with_ci(path, ".htm");
}

 
static int ci_prefix(const char *s, const char *prefix, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        char a = s[i];
        char b = prefix[i];
        if (a >= 'A' && a <= 'Z')
            a = (char)(a + 32);
        if (b >= 'A' && b <= 'Z')
            b = (char)(b + 32);
        if (a != b)
            return 0;
        if (!a)
            return 0;
    }
    return 1;
}

int bfs_exists(const char *path)
{
    return bfs_lookup(path) >= 0;
}

int bfs_is_dir(const char *path)
{
    if (!g_mounted)
        return 0;
    const char *p = path;
    while (*p == '/')
        p++;
    if (*p == 0)
        return 1;

    int idx = bfs_lookup(p);
    if (idx >= 0 && (g_super.files[idx].flags & BFS_FILE_DIR))
        return 1;

    size_t n = kstrlen(p);
    for (int i = 0; i < BFS_MAX_FILES; i++) {
        if (!(g_super.files[i].flags & BFS_FILE_USED))
            continue;
        const char *name = g_super.files[i].name;
        if (ci_prefix(name, p, n) && name[n] == '/')
            return 1;
    }
    return 0;
}

static int find_free_entry(void)
{
    for (int i = 0; i < BFS_MAX_FILES; i++) {
        if (!(g_super.files[i].flags & BFS_FILE_USED))
            return i;
    }
    return -1;
}

static uint32_t entry_blocks(const bfs_file_t *f)
{
    uint32_t blocks = (f->size + 511) / 512;
    return blocks ? blocks : 1;
}

int bfs_mkdir(const char *path)
{
    if (!g_mounted || !path)
        return -1;
    const char *p = path;
    while (*p == '/')
        p++;
    size_t len = kstrlen(p);
    if (len == 0 || len >= BFS_NAME_LEN)
        return -1;
    if (bfs_lookup(p) >= 0 || bfs_is_dir(p))
        return -1;

    char parent[BFS_NAME_LEN];
    str_copy(parent, p, sizeof(parent));
    char *slash = 0;
    for (char *q = parent; *q; q++) {
        if (*q == '/')
            slash = q;
    }
    if (slash) {
        *slash = 0;
        if (!bfs_is_dir(parent))
            return -1;
    }

    int idx = find_free_entry();
    if (idx < 0)
        return -1;
    bfs_file_t *f = &g_super.files[idx];
    memset(f, 0, sizeof(*f));
    str_copy(f->name, p, BFS_NAME_LEN);
    f->flags = BFS_FILE_USED | BFS_FILE_DIR;
    if (write_super() != 0) {
        memset(f, 0, sizeof(*f));
        return -1;
    }
    return 0;
}

int bfs_unlink(const char *path)
{
    if (!g_mounted || !path)
        return -1;
    int idx = bfs_lookup(path);
    if (idx < 0)
        return -1;
    bfs_file_t *f = &g_super.files[idx];
    if (f->flags & BFS_FILE_DIR)
        return -2;
    if (f->start_lba + entry_blocks(f) == g_super.next_lba)
        g_super.next_lba = f->start_lba;
    memset(f, 0, sizeof(*f));
    return write_super() == 0 ? 0 : -1;
}

int bfs_rmdir(const char *path)
{
    if (!g_mounted || !path)
        return -1;
    const char *p = path;
    while (*p == '/')
        p++;
    if (*p == 0)
        return -1;
    int idx = bfs_lookup(p);
    if (idx < 0 || !(g_super.files[idx].flags & BFS_FILE_DIR))
        return -2;

    size_t n = kstrlen(p);
    for (int i = 0; i < BFS_MAX_FILES; i++) {
        if (!(g_super.files[i].flags & BFS_FILE_USED) || i == idx)
            continue;
        if (ci_prefix(g_super.files[i].name, p, n) &&
            g_super.files[i].name[n] == '/')
            return -3;
    }

    memset(&g_super.files[idx], 0, sizeof(g_super.files[idx]));
    return write_super() == 0 ? 0 : -1;
}

static int entry_name_lt(const bfs_dir_entry_t *a, const bfs_dir_entry_t *b)
{
    if (a->is_dir != b->is_dir)
        return a->is_dir > b->is_dir;
    const char *x = a->name;
    const char *y = b->name;
    while (*x && *y) {
        char cx = *x;
        char cy = *y;
        if (cx >= 'A' && cx <= 'Z')
            cx = (char)(cx + 32);
        if (cy >= 'A' && cy <= 'Z')
            cy = (char)(cy + 32);
        if (cx != cy)
            return cx < cy;
        x++;
        y++;
    }
    return *x == 0 && *y != 0;
}

int bfs_list_dir(const char *abs_path, bfs_dir_entry_t *out, int max)
{
    if (!g_mounted || !out || max <= 0)
        return 0;

    const char *p = abs_path;
    while (*p == '/')
        p++;
    size_t base = kstrlen(p);
    while (base > 0 && p[base - 1] == '/')
        base--;

    int n = 0;
    for (int i = 0; i < BFS_MAX_FILES; i++) {
        bfs_file_t *f = &g_super.files[i];
        if (!(f->flags & BFS_FILE_USED))
            continue;

        const char *leaf;
        if (base == 0) {
            leaf = f->name;
        } else {
            if (!ci_prefix(f->name, p, base) || f->name[base] != '/')
                continue;
            leaf = f->name + base + 1;
            if (*leaf == 0)
                continue;
        }

        int is_dir = (f->flags & BFS_FILE_DIR) != 0;
        uint32_t size = f->size;
        char name[BFS_NAME_LEN];
        const char *slash = leaf;
        while (*slash && *slash != '/')
            slash++;
        if (*slash == '/') {
            size_t len = (size_t)(slash - leaf);
            if (len >= BFS_NAME_LEN)
                len = BFS_NAME_LEN - 1;
            memcpy(name, leaf, len);
            name[len] = 0;
            is_dir = 1;
            size = 0;
        } else {
            str_copy(name, leaf, sizeof(name));
        }

        int dup = 0;
        for (int k = 0; k < n; k++) {
            if (out[k].is_dir == is_dir && name_eq(out[k].name, name)) {
                dup = 1;
                break;
            }
        }
        if (dup)
            continue;

        bfs_dir_entry_t *e = &out[n++];
        memset(e, 0, sizeof(*e));
        str_copy(e->name, name, sizeof(e->name));
        e->is_dir = is_dir;
        e->size = size;
        e->html = (f->flags & BFS_FILE_HTML) != 0;
        if (n >= max)
            break;
    }

    for (int i = 1; i < n; i++) {
        bfs_dir_entry_t tmp = out[i];
        int j = i - 1;
        while (j >= 0 && entry_name_lt(&tmp, &out[j])) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = tmp;
    }
    return n;
}

uint32_t bfs_used_bytes(void)
{
    if (!g_mounted || g_super.next_lba <= g_super.data_lba)
        return 0;
    return (g_super.next_lba - g_super.data_lba) * 512u;
}

 

void path_normalize(const char *cwd, const char *input, char *out, size_t cap)
{
    char full[192];
    size_t n = 0;

    if (!out || cap == 0)
        return;
    if (!input || !input[0]) {
        input = ".";
    }

    if (input[0] != '/') {
        const char *base = (cwd && cwd[0]) ? cwd : "/";
        size_t bl = kstrlen(base);
        if (bl >= sizeof(full) - 1)
            bl = sizeof(full) - 2;
        memcpy(full, base, bl);
        n = bl;
        if (n == 0 || full[n - 1] != '/')
            full[n++] = '/';
        size_t il = kstrlen(input);
        if (il > sizeof(full) - 1 - n)
            il = sizeof(full) - 1 - n;
        memcpy(full + n, input, il);
        n += il;
    } else {
        size_t il = kstrlen(input);
        if (il > sizeof(full) - 1)
            il = sizeof(full) - 1;
        memcpy(full, input, il);
        n = il;
    }
    full[n] = 0;

    size_t pos = 0;
    out[pos++] = '/';
    out[pos] = 0;

    size_t i = 0;
    while (full[i]) {
        while (full[i] == '/')
            i++;
        if (!full[i])
            break;
        size_t start = i;
        while (full[i] && full[i] != '/')
            i++;
        size_t len = i - start;

        if (len == 1 && full[start] == '.')
            continue;
        if (len == 2 && full[start] == '.' && full[start + 1] == '.') {
            if (pos > 1) {
                pos--;
                while (pos > 0 && out[pos - 1] != '/')
                    pos--;
                if (pos == 0) {
                    out[0] = '/';
                    pos = 1;
                }
                out[pos] = 0;
            }
            continue;
        }

        if (pos > 1 && pos + 1 < cap)
            out[pos++] = '/';
        for (size_t k = 0; k < len && pos + 1 < cap; k++)
            out[pos++] = full[start + k];
        out[pos] = 0;
    }
    if (pos == 0) {
        out[0] = '/';
        out[1] = 0;
    }
}
