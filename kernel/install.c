#include "kernel.h"
#include "site_data.h"

extern const unsigned char g_mbr_image[];
extern const unsigned int g_mbr_image_size;

#define C_BAR   VGA_ATTR(BLACK, LIGHT_CYAN)
#define C_LABEL VGA_ATTR(LIGHT_CYAN, BLACK)
#define C_VALUE VGA_ATTR(WHITE, BLACK)
#define C_DIM   VGA_ATTR(LIGHT_GRAY, BLACK)
#define C_WARN  VGA_ATTR(LIGHT_RED, BLACK)
#define C_GOOD  VGA_ATTR(LIGHT_GREEN, BLACK)
#define C_ACC   VGA_ATTR(YELLOW, BLACK)
#define STAGE2_SECTORS 65u
#define KERNEL_LBA 66u
#define KERNEL_IMAGE_OFS 0x8400u

static void bar(const char *step)
{
    vga_fill_row(0, C_BAR);
    vga_puts_at(0, 1, "ByteOS 0.2 installer", C_BAR);
    if (step)
        vga_puts_at(0, 24, step, C_BAR);
    vga_puts_at(0, 64, "BIOS / ATA", C_BAR);
}

static int valid_name(const char *s)
{
    if (!s[0] || kstrlen(s) >= 32)
        return 0;
    for (const char *p = s; *p; p++) {
        char c = *p;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')
            continue;
        return 0;
    }
    return 1;
}

static void print_disk_size(uint32_t sectors)
{
    uint64_t mib = (uint64_t)sectors / 2048u;
    if (mib >= 1024)
        kprintf("%llu GiB", (unsigned long long)(mib / 1024));
    else
        kprintf("%llu MiB", (unsigned long long)mib);
}

static int parse_number(const char *s, int max)
{
    int value = 0;
    if (!*s)
        return -1;
    while (*s) {
        if (*s < '0' || *s > '9')
            return -1;
        value = value * 10 + (*s++ - '0');
    }
    if (value < 1 || value > max)
        return -1;
    return value;
}

static void put32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

static size_t append(char *dst, size_t cap, size_t pos, const char *s)
{
    while (*s) {
        if (pos + 1 >= cap)
            break;
        dst[pos++] = *s++;
    }
    dst[pos] = 0;
    return pos;
}

static int fail(const char *what)
{
    console_set_attr(C_WARN);
    kprintf("\n  Installation failed: %s\n", what);
    console_set_attr(C_DIM);
    console_puts("  The disk may be left in an inconsistent state.\n");
    console_puts("  Press Enter to return to the shell.\n");
    console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
    for (;;) {
        int c = kbd_getchar();
        if (c == '\n')
            break;
    }
    return -1;
}

#define INSTALL_FAIL(msg) do { fail(msg); return; } while (0)

static int write_home_pages(const char *user)
{
    char page[1400];
    size_t n = 0;
    page[0] = 0;
    n = append(page, sizeof(page), n,
        "<!DOCTYPE html><html><head><title>Welcome to ByteOS</title></head><body>");
    n = append(page, sizeof(page), n, "<h1>Welcome, ");
    n = append(page, sizeof(page), n, user);
    n = append(page, sizeof(page), n, "</h1><p>This is your ByteOS home folder. ");
    n = append(page, sizeof(page), n,
        "Files live on your ByteFS disk and survive a reboot.</p>");
    n = append(page, sizeof(page), n,
        "<h2>Try this</h2><ul><li>Open the project website at www/index.html</li>");
    n = append(page, sizeof(page), n,
        "<li>Edit home/notes.html and save with Ctrl+S</li>");
    n = append(page, sizeof(page), n,
        "<li>Run fetch in the terminal</li></ul>");
    n = append(page, sizeof(page), n,
        "<p><a href=\"www/index.html\">Project website</a></p></body></html>");
    if (bfs_add("home/welcome.html", page, (uint32_t)n, 1) != 0)
        return -1;

    static const char notes[] =
        "<!DOCTYPE html><html><head><title>Notes</title></head><body>\n"
        "<h1>My notes</h1>\n"
        "<p>This is a real HTML file on your ByteOS disk.</p>\n"
        "<h2>Todo</h2>\n"
        "<ul>\n"
        "  <li>Try the local web browser</li>\n"
        "  <li>Open the file explorer</li>\n"
        "  <li>Edit this page and press Ctrl+S</li>\n"
        "</ul>\n"
        "</body></html>\n";
    return bfs_add("home/notes.html", notes, (uint32_t)(sizeof(notes) - 1), 1);
}

void install_wizard(void)
{
    console_clear(VGA_ATTR(LIGHT_GRAY, BLACK));
    bar("guided disk installation");
    console_goto(2, 0);

    console_set_attr(C_ACC);
    kprintf("BYTEOS INSTALLER\n");
    console_set_attr(C_DIM);
    kprintf("--------------------------------------------------------------------------------\n");
    console_set_attr(C_VALUE);
    kprintf("This writes a complete ByteOS system to a raw ATA disk.\n");
    console_set_attr(C_WARN);
    kprintf("Every sector on the selected disk will be erased.\n");
    console_set_attr(C_DIM);
    kprintf("Use a spare disk or a QEMU image, never your main system disk.\n\n");

    ata_init();
    int slots[ATA_MAX_DISKS];
    int count = 0;
    console_set_attr(C_LABEL);
    kprintf("Available ATA disks:\n");
    console_set_attr(C_VALUE);
    for (int i = 0; i < ATA_MAX_DISKS; i++) {
        ata_disk_t *d = ata_slot(i);
        if (!d)
            continue;
        slots[count++] = i;
        const char *model = d->model[0] ? d->model : "(unnamed)";
        kprintf("  %d) %s", count, model);
        int pad = 40 - (int)kstrlen(model);
        while (pad-- > 0)
            console_putc(' ');
        print_disk_size(d->sectors);
        kprintf("  (slot %d)\n", i);
    }
    if (count == 0) {
        console_set_attr(C_WARN);
        kprintf("\nNo ATA disks found.\n");
        console_set_attr(C_DIM);
        kprintf("Attach a disk with if=ide in QEMU, or enable IDE mode in firmware.\n");
        console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
        return;
    }

    console_putc('\n');
    char line[64];
    console_set_attr(C_LABEL);
    kprintf("Installation disk [1-%d]: ", count);
    console_set_attr(C_VALUE);
    console_read_line(line, sizeof(line), 0);
    int choice = parse_number(line, count);
    if (choice < 0) {
        console_set_attr(C_WARN);
        kprintf("Invalid disk selection.\n");
        console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
        return;
    }

    int slot = slots[choice - 1];
    ata_disk_t *disk = ata_slot(slot);
    if (!disk)
        INSTALL_FAIL("disk disappeared");

    if (g_boot->boot_flags & BOOT_FLAG_DISK) {
        int boot_slot = ata_bios_slot((int)g_boot->boot_drive);
        if (bfs_mounted())
            boot_slot = bfs_slot();
        if (boot_slot == slot) {
            console_set_attr(C_WARN);
            kprintf("\nRefusing to install onto the disk ByteOS is running from.\n");
            console_set_attr(C_DIM);
            kprintf("Boot the ISO and select a different disk.\n");
            console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
            return;
        }
    }

    if (disk->sectors < BFS_PART_LBA + 128u) {
        console_set_attr(C_WARN);
        kprintf("\nDisk is too small. At least 32 MiB is required.\n");
        console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
        return;
    }

    kprintf("\nTarget: %s, ", disk->model);
    print_disk_size(disk->sectors);
    kprintf(" (%u sectors)\n\n", disk->sectors);

    char user[32], host[32];
    char pass[64], pass2[64], root[64], root2[64];

    console_set_attr(C_LABEL);
    kprintf("Username: ");
    console_set_attr(C_VALUE);
    console_read_line(user, sizeof(user), 0);
    if (!valid_name(user)) {
        console_set_attr(C_WARN);
        kprintf("Invalid username. Use 1-31 letters, digits, dot, dash or underscore.\n");
        console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
        return;
    }

    console_set_attr(C_LABEL);
    kprintf("Password: ");
    console_set_attr(C_VALUE);
    console_read_line(pass, sizeof(pass), 1);
    console_set_attr(C_LABEL);
    kprintf("Repeat password: ");
    console_set_attr(C_VALUE);
    console_read_line(pass2, sizeof(pass2), 1);
    if (!pass[0] || !str_eq(pass, pass2)) {
        console_set_attr(C_WARN);
        kprintf("Passwords are empty or do not match.\n");
        console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
        return;
    }

    console_set_attr(C_LABEL);
    kprintf("Root password: ");
    console_set_attr(C_VALUE);
    console_read_line(root, sizeof(root), 1);
    console_set_attr(C_LABEL);
    kprintf("Repeat root password: ");
    console_set_attr(C_VALUE);
    console_read_line(root2, sizeof(root2), 1);
    if (!root[0] || !str_eq(root, root2)) {
        console_set_attr(C_WARN);
        kprintf("Root passwords are empty or do not match.\n");
        console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
        return;
    }

    console_set_attr(C_LABEL);
    kprintf("Hostname [byteos]: ");
    console_set_attr(C_VALUE);
    host[0] = 0;
    console_read_line(host, sizeof(host), 0);
    if (!host[0])
        str_copy(host, "byteos", sizeof(host));
    if (!valid_name(host)) {
        console_set_attr(C_WARN);
        kprintf("Invalid hostname.\n");
        console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
        return;
    }

    console_set_attr(C_WARN);
    kprintf("\nType INSTALL to erase %s: ", disk->model);
    console_set_attr(C_VALUE);
    console_read_line(line, sizeof(line), 0);
    if (!str_eq_ci(line, "INSTALL")) {
        console_set_attr(C_DIM);
        kprintf("Installation cancelled.\n");
        console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
        return;
    }

    if (!g_mbr_image_size || g_mbr_image_size < 512)
        INSTALL_FAIL("embedded MBR is missing");
    if (g_boot->image_bytes <= KERNEL_IMAGE_OFS)
        INSTALL_FAIL("boot image size is invalid");

    uint32_t kernel_bytes = g_boot->image_bytes - KERNEL_IMAGE_OFS;
    uint32_t kernel_sectors = (kernel_bytes + 511u) / 512u;

    console_putc('\n');
    console_set_attr(C_DIM);
    kprintf("  [1/5] Writing MBR and partition table...\n");
    unsigned char mbr[512];
    memcpy(mbr, g_mbr_image, 512);
    mbr[446] = 0x80;
    mbr[447] = 0xFE;
    mbr[448] = 0xFF;
    mbr[449] = 0xFF;
    mbr[450] = 0x42;
    mbr[451] = 0xFE;
    mbr[452] = 0xFF;
    mbr[453] = 0xFF;
    put32(mbr + 454, BFS_PART_LBA);
    put32(mbr + 458, disk->sectors - BFS_PART_LBA);
    mbr[510] = 0x55;
    mbr[511] = 0xAA;
    for (int i = 462; i < 510; i++)
        mbr[i] = 0;
    if (ata_write_sectors(disk, 0, 1, mbr) != 0)
        INSTALL_FAIL("could not write the MBR");

    kprintf("  [2/5] Writing stage 2 (%u sectors)...\n", STAGE2_SECTORS);
    if (ata_write_sectors(disk, 1, STAGE2_SECTORS,
                          (const void *)(uintptr_t)0x7E00) != 0)
        INSTALL_FAIL("could not write stage 2");

    kprintf("  [3/5] Writing kernel (%u sectors)...\n", kernel_sectors);
    if (ata_write_sectors(disk, KERNEL_LBA, kernel_sectors,
                          (const void *)(uintptr_t)0x10000) != 0)
        INSTALL_FAIL("could not write the kernel");

    kprintf("  [4/5] Formatting ByteFS at LBA %u...\n", BFS_PART_LBA);
    if (bfs_format(slot, BFS_PART_LBA, disk->sectors) != 0)
        INSTALL_FAIL("could not format the filesystem");

    kprintf("  [5/5] Installing files and accounts...\n");
    if (auth_set_credentials(user, pass, root, host) != 0)
        INSTALL_FAIL("could not write the account database");

    for (unsigned int i = 0; i < g_site_file_count; i++) {
        const site_file_t *f = &g_site_files[i];
        if (bfs_add(f->path, f->data, f->size, (int)f->html) != 0)
            INSTALL_FAIL("could not write the bundled website");
    }
    if (write_home_pages(user) != 0)
        INSTALL_FAIL("could not write the home pages");

    ata_flush(disk);
    console_set_attr(C_GOOD);
    kprintf("\n  ByteOS is installed on %s.\n", disk->model);
    console_set_attr(C_VALUE);
    kprintf("  User: %s   Host: %s   Filesystem: ByteFS\n", user, host);
    console_set_attr(C_DIM);
    kprintf("  Remove the ISO/USB, boot from the disk, and sign in.\n\n");
    console_set_attr(C_LABEL);
    kprintf("  Press R to reboot, any other key to stay in the live shell.\n");
    console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));

    for (;;) {
        int c = kbd_getchar();
        if (c == 'r' || c == 'R') {
            reboot();
        } else if (c >= 0) {
            break;
        }
    }
    console_clear(VGA_ATTR(LIGHT_GRAY, BLACK));
}
