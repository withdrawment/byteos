#include "kernel.h"

/* ByteFS: a deliberately tiny contiguous-file filesystem.
   Sector 0 of the partition holds the superblock and 32 directory entries.
   File data starts eight sectors later and grows upward. */

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

/* The superblock and directory are 1712 bytes, so they span four
   512-byte ATA sectors even though one ISO/ByteFS logical block is bigger. */
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
