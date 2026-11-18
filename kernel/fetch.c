#include "kernel.h"
#include "fetch_art.h"

#define C_BAR   VGA_ATTR(BLACK, LIGHT_CYAN)
#define C_LABEL VGA_ATTR(LIGHT_CYAN, BLACK)
#define C_VALUE VGA_ATTR(WHITE, BLACK)
#define C_DIM   VGA_ATTR(LIGHT_GRAY, BLACK)
#define C_ACC   VGA_ATTR(YELLOW, BLACK)

static void put(int row, int col, const char *s, uint8_t attr)
{
    vga_puts_at(row, col, s, attr);
}

static void field(int row, const char *label)
{
    console_goto(row, 27);
    console_set_attr(C_LABEL);
    console_puts(label);
    console_set_attr(C_VALUE);
}

static uint8_t art_attr(char c)
{
    switch (c) {
    case '@': return VGA_ATTR(WHITE, BLACK);
    case '%': return VGA_ATTR(WHITE, BLACK);
    case '#': return VGA_ATTR(YELLOW, BLACK);
    case '*': return VGA_ATTR(LIGHT_RED, BLACK);
    case '+': return VGA_ATTR(LIGHT_RED, BLACK);
    case '=': return VGA_ATTR(LIGHT_MAGENTA, BLACK);
    case '-': return VGA_ATTR(MAGENTA, BLACK);
    case ':': return VGA_ATTR(LIGHT_BLUE, BLACK);
    case '.': return VGA_ATTR(BLUE, BLACK);
    default: return VGA_ATTR(DARK_GRAY, BLACK);
    }
}

static void put_art(int row, int col, const char *s)
{
    for (int i = 0; s[i]; i++) {
        if (s[i] != ' ')
            vga_put_at(row, col + i, s[i], art_attr(s[i]));
    }
}

static void truncate_copy(char *dst, const char *src, int max)
{
    int i = 0;
    while (src[i] && i < max) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

 

static uint16_t g_saved_screen[VGA_WIDTH * VGA_HEIGHT];

void fetch_screen(void)
{
    int region_top, region_bottom, saved_row, saved_col;
    console_get_region(&region_top, &region_bottom);
    console_get_cursor(&saved_row, &saved_col);
    memcpy(g_saved_screen, (const void *)VGA_MEMORY, sizeof(g_saved_screen));
    kbd_drain();
    console_clear(VGA_ATTR(LIGHT_GRAY, BLACK));
    vga_fill_row(0, C_BAR);
    put(0, 1, "ByteOS fetch", C_BAR);
    put(0, 33, "system information", C_BAR);
    put(0, 65, "v" BYTEOS_VERSION, C_BAR);

    for (int i = 0; i < FETCH_ART_ROWS; i++)
        put_art(2 + i, 2, g_fetch_art[i]);

    put(15, 3, "BYTEOS", VGA_ATTR(YELLOW, BLACK));
    put(16, 3, "0.2 Sprinkles", C_DIM);

    console_goto(2, 27);
    console_set_attr(C_ACC);
    console_puts(auth_username());
    console_set_attr(C_DIM);
    console_puts("@");
    console_puts(auth_hostname());
    console_goto(3, 27);
    console_set_attr(C_DIM);
    console_puts("------------");

    field(4, "OS          ");
    kprintf("ByteOS " BYTEOS_VERSION " \"" BYTEOS_CODENAME "\" x86_64");

    field(5, "Kernel      ");
    kprintf("ByteOS 0.2.0 (long mode)");

    field(6, "Host        ");
    kprintf("%s", g_hypervisor ? "QEMU / virtual machine" : "bare metal");

    uint64_t up = 0;
    if (g_tsc_hz && read_tsc() > g_boot_tsc)
        up = (read_tsc() - g_boot_tsc) / g_tsc_hz;
    field(7, "Uptime      ");
    kprintf("%llum %llus", (unsigned long long)(up / 60),
            (unsigned long long)(up % 60));

    field(8, "Shell       ");
    kprintf("bytesh");

    field(9, "Desktop     ");
    kprintf("ByteDesktop / ByteWin 95");

    char cpu[41];
    truncate_copy(cpu, g_cpu_brand[0] ? g_cpu_brand : "unknown", 40);
    field(10, "CPU         ");
    kprintf("%s", cpu);

    field(11, "Memory      ");
    print_size(g_mem_usable);
    kprintf(" usable / %u CPUs", g_logical_cpus);

    field(12, "Disk        ");
    if (bfs_mounted()) {
        ata_disk_t *d = ata_slot(bfs_slot());
        if (d) {
            char model[29];
            truncate_copy(model, d->model, 28);
            kprintf("%s, ", model);
            print_size((uint64_t)d->sectors * 512ULL);
        } else {
            kprintf("ByteFS");
        }
    } else {
        kprintf("none (live ISO)");
    }

    field(13, "Boot        ");
    kprintf("%s", (g_boot_ok && (g_boot->boot_flags & BOOT_FLAG_DISK))
                       ? "installed disk (BIOS MBR)"
                       : "live ISO (El Torito)");

    field(14, "User        ");
    kprintf("%s%s", auth_username(), auth_is_root() ? " (root)" : "");

    field(15, "GitHub      ");
    kprintf("github.com/withdrawment/byteos");

    console_goto(18, 27);
    console_set_attr(C_DIM);
    kprintf("E820: %u entries   VGA: 80x25   ATA PIO: %s",
            g_e820_count,
            ata_slot(0) || ata_slot(1) || ata_slot(2) || ata_slot(3)
                ? "ready" : "no disk");

    vga_fill_row(23, VGA_ATTR(BLACK, LIGHT_CYAN));
    put(23, 2, "fetch  |  ByteOS 0.2  |  press any key to return",
        VGA_ATTR(BLACK, LIGHT_CYAN));
    console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));

    (void)kbd_waitkey();
    kbd_drain();
    memcpy((void *)VGA_MEMORY, g_saved_screen, sizeof(g_saved_screen));
    console_set_region(region_top, region_bottom);
    console_goto(saved_row, saved_col);
}
