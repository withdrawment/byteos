#include "kernel.h"

#define C_LABEL VGA_ATTR(LIGHT_CYAN, BLACK)
#define C_VALUE VGA_ATTR(WHITE, BLACK)
#define C_DIM   VGA_ATTR(LIGHT_GRAY, BLACK)
#define C_DARK  VGA_ATTR(DARK_GRAY, BLACK)
#define C_ACC   VGA_ATTR(YELLOW, BLACK)
#define C_ART   VGA_ATTR(LIGHT_MAGENTA, BLACK)
#define C_BAR   VGA_ATTR(BLACK, CYAN)

static void centered(int row, const char *text, uint8_t attr)
{
    int len = (int)kstrlen(text);
    int col = (VGA_WIDTH - len) / 2;
    if (col < 0)
        col = 0;
    vga_puts_at(row, col, text, attr);
}

static void draw_topbar(void)
{
    vga_fill_row(0, C_BAR);
    vga_puts_at(0, 1, "ByteOS " BYTEOS_VERSION, C_BAR);

    const char *mid = "64-bit bare-metal kernel";
    vga_puts_at(0, (VGA_WIDTH - (int)kstrlen(mid)) / 2, mid, C_BAR);

    const char *right = "x86_64 / BIOS";
    vga_puts_at(0, VGA_WIDTH - (int)kstrlen(right) - 1, right, C_BAR);
}

static void draw_logo(void)
{
    static const char *donut_art[8] = {
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
        centered(2 + i, donut_art[i], C_ART);

    static const char *glyphs[6][5] = {
        { "#### ", "#   #", "#### ", "#   #", "#### " }, /* B */
        { "#   #", "#   #", " # # ", "  #  ", "  #  " }, /* Y */
        { "#####", "  #  ", "  #  ", "  #  ", "  #  " }, /* T */
        { "#####", "#    ", "#### ", "#    ", "#####" }, /* E */
        { " ### ", "#   #", "#   #", "#   #", " ### " }, /* O */
        { " ####", "#    ", " ### ", "    #", "#### " }  /* S */
    };

    for (int r = 0; r < 5; r++) {
        for (int g = 0; g < 6; g++)
            vga_puts_at(11 + r, 22 + g * 6, glyphs[g][r], C_ACC);
    }

    centered(16, "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~",
             VGA_ATTR(CYAN, BLACK));
    centered(17, "64-BIT BARE-METAL KERNEL   /   v" BYTEOS_VERSION,
             C_DIM);
}

static void print_specs_summary(void)
{
    console_goto(19, 0);
    console_set_attr(C_LABEL);
    kprintf("  CPU  : ");
    console_set_attr(C_VALUE);
    kprintf("%s", g_cpu_brand);
    console_putc('\n');

    console_set_attr(C_LABEL);
    kprintf("  MEM  : ");
    console_set_attr(C_VALUE);
    if (g_boot_ok) {
        print_size(g_mem_usable);
        kprintf(" usable");
    } else {
        kprintf("firmware map unavailable");
    }
    kprintf("  |  CPUs: %u  |  TSC: ", g_logical_cpus);
    if (g_tsc_hz)
        kprintf("~%llu MHz", (unsigned long long)((g_tsc_hz + 500000ULL) / 1000000ULL));
    else
        kprintf("uncalibrated");
    console_putc('\n');

    console_set_attr(C_LABEL);
    kprintf("  SYS  : ");
    console_set_attr(C_VALUE);
    kprintf("x86_64 long mode  |  VGA 80x25  |  %s",
            (g_boot_ok && (g_boot->boot_flags & BOOT_FLAG_DISK))
                ? "BIOS MBR + ByteFS" : "BIOS El Torito");
    if (g_hypervisor)
        kprintf("  |  VM");
    console_putc('\n');
}

void home_screen(void)
{
    console_clear(VGA_ATTR(LIGHT_GRAY, BLACK));
    draw_topbar();
    draw_logo();
    print_specs_summary();

    centered(23, "Type \"help\"  |  try \"fetch\", \"start-desktop\" and \"byte-install\"",
             C_DARK);

    console_goto(24, 0);
    console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
}

/* ---------------------------------------------------------------- help */
static void padded(const char *s, int width)
{
    int n = 0;
    while (*s && n < width) {
        console_putc(*s++);
        n++;
    }
    while (n < width) {
        console_putc(' ');
        n++;
    }
}

static void help_line(const char *cmd, const char *desc)
{
    console_puts("  ");
    console_set_attr(C_ACC);
    padded(cmd, 14);
    console_set_attr(C_DIM);
    kprintf("%s\n", desc);
}

void help_screen(void)
{
    console_clear(VGA_ATTR(LIGHT_GRAY, BLACK));
    draw_topbar();
    console_goto(2, 0);
    console_set_attr(C_LABEL);
    kprintf("COMMANDS\n");
    console_set_attr(C_DARK);
    kprintf("--------------------------------------------------------------------------------\n");
    help_line("help", "show this command list");
    help_line("fetch", "fastfetch-style system summary");
    help_line("start-desktop", "launch ByteDesktop");
    help_line("byte-install", "install ByteOS to an ATA disk");
    help_line("specs", "full CPU, memory and timing report");
    help_line("memmap", "BIOS E820 memory map");
    help_line("ls / cat", "list or print ByteFS files");
    help_line("whoami / su", "account and root access");
    help_line("passwd", "change your password");
    help_line("about", "what ByteOS is made of");
    help_line("donut", "spin the ASCII donut (any key returns)");
    help_line("logo", "redraw the boot screen");
    help_line("clear", "clear the screen");
    help_line("reboot", "warm restart the machine");
    console_putc('\n');
    console_set_attr(C_DARK);
    kprintf("Commands are case-insensitive. \"sysinfo\" aliases \"specs\".\n");
    console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
}

/* ---------------------------------------------------------------- specs */
static void feature(const char *name, int present)
{
    if (present)
        kprintf("%s ", name);
}

void specs_screen(void)
{
    console_clear(VGA_ATTR(LIGHT_GRAY, BLACK));
    draw_topbar();
    console_goto(2, 0);

    console_set_attr(C_LABEL);
    kprintf("SYSTEM REPORT");
    console_set_attr(C_DARK);
    kprintf("  /  ByteOS " BYTEOS_VERSION " \"" BYTEOS_CODENAME "\"\n");
    kprintf("--------------------------------------------------------------------------------\n");

    console_set_attr(C_LABEL);
    kprintf("  CPU vendor : ");
    console_set_attr(C_VALUE);
    kprintf("%s\n", g_cpu_vendor);

    console_set_attr(C_LABEL);
    kprintf("  CPU brand  : ");
    console_set_attr(C_VALUE);
    kprintf("%s\n", g_cpu_brand);

    console_set_attr(C_LABEL);
    kprintf("  Family     : ");
    console_set_attr(C_VALUE);
    kprintf("%u   Model: %u   Stepping: %u   Logical CPUs: %u\n",
            g_cpu_family, g_cpu_model, g_cpu_stepping, g_logical_cpus);

    console_set_attr(C_LABEL);
    kprintf("  TSC clock  : ");
    console_set_attr(C_VALUE);
    if (g_tsc_hz)
        kprintf("~%llu MHz (calibrated with PIT)\n",
                (unsigned long long)((g_tsc_hz + 500000ULL) / 1000000ULL));
    else
        kprintf("unavailable\n");

    console_set_attr(C_LABEL);
    kprintf("  Hypervisor : ");
    console_set_attr(C_VALUE);
    kprintf("%s\n", g_hypervisor ? "yes" : "no");

    console_set_attr(C_LABEL);
    kprintf("  Features   : ");
    console_set_attr(C_VALUE);
    feature("SSE", F1_EDX(25));
    feature("SSE2", F1_EDX(26));
    feature("SSE3", F1_ECX(0));
    feature("SSSE3", F1_ECX(9));
    feature("SSE4.1", F1_ECX(19));
    feature("SSE4.2", F1_ECX(20));
    feature("AVX", F1_ECX(28));
    feature("AVX2", F7_EBX(5));
    feature("FMA", F1_ECX(12));
    console_putc('\n');

    console_set_attr(C_LABEL);
    kprintf("             ");
    console_set_attr(C_VALUE);
    feature("AES", F1_ECX(25));
    feature("PCLMUL", F1_ECX(1));
    feature("POPCNT", F1_ECX(23));
    feature("RDRAND", F1_ECX(30));
    feature("XSAVE", F1_ECX(26));
    feature("OSXSAVE", F1_ECX(27));
    feature("NX", FEXT_EDX(20));
    feature("LM", FEXT_EDX(29));
    console_putc('\n');

    console_set_attr(C_LABEL);
    kprintf("             ");
    console_set_attr(C_VALUE);
    feature("SYSCALL", FEXT_EDX(11));
    feature("1GB-PAGES", FEXT_EDX(26));
    feature("SMEP", F7_EBX(7));
    feature("SMAP", F7_EBX(20));
    feature("FSGSBASE", F7_EBX(0));
    feature("BMI2", F7_EBX(8));
    feature("ERMS", F7_EBX(9));
    console_putc('\n');

    console_set_attr(C_LABEL);
    kprintf("  Memory     : ");
    console_set_attr(C_VALUE);
    if (g_boot_ok) {
        print_size(g_mem_usable);
        kprintf(" usable  /  top of E820: ");
        print_size(g_mem_top);
        console_putc('\n');
    } else {
        kprintf("firmware map unavailable\n");
    }

    console_set_attr(C_LABEL);
    kprintf("  E820       : ");
    console_set_attr(C_VALUE);
    kprintf("%u firmware entries\n", g_e820_count);

    console_set_attr(C_LABEL);
    kprintf("  Boot path  : ");
    console_set_attr(C_VALUE);
    kprintf("%s -> long mode (ring 0, x86_64)\n",
            (g_boot_ok && (g_boot->boot_flags & BOOT_FLAG_DISK))
                ? "BIOS MBR -> ByteFS" : "BIOS El Torito -> live ISO");
    console_set_attr(C_LABEL);
    kprintf("  Filesystem : ");
    console_set_attr(C_VALUE);
    if (bfs_mounted()) {
        ata_disk_t *d = ata_slot(bfs_slot());
        kprintf("ByteFS on %s, %u files\n", d ? d->model : "ATA disk",
                bfs_file_count());
    } else {
        kprintf("live ISO (read-only)\n");
    }

    console_set_attr(C_LABEL);
    kprintf("  Video      : ");
    console_set_attr(C_VALUE);
    kprintf("VGA text 80x25, mode 3, 16 colours\n");

    console_set_attr(C_LABEL);
    kprintf("  Timer      : ");
    console_set_attr(C_VALUE);
    kprintf("PIT channel 2 calibration / TSC frame clock\n");

    console_set_attr(C_LABEL);
    kprintf("  RTC        : ");
    console_set_attr(C_VALUE);
    rtc_time_t tm;
    if (rtc_read(&tm) == 0) {
        kprintf("%04d-%02d-%02d %02d:%02d:%02d (CMOS)\n",
                tm.year, tm.month, tm.day, tm.hour, tm.minute, tm.second);
    } else {
        kprintf("unavailable\n");
    }

    console_set_attr(C_LABEL);
    kprintf("  Build      : ");
    console_set_attr(C_VALUE);
    kprintf("%s %s\n", __DATE__, __TIME__);

    console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
}

/* ---------------------------------------------------------------- memmap */
static const char *e820_name(uint32_t type)
{
    switch (type) {
    case 1:  return "usable";
    case 2:  return "reserved";
    case 3:  return "ACPI reclaim";
    case 4:  return "ACPI NVS";
    case 5:  return "bad memory";
    case 6:  return "disabled";
    case 7:  return "persistent";
    case 8:  return "unusable";
    default: return "unknown";
    }
}

void memmap_screen(void)
{
    console_clear(VGA_ATTR(LIGHT_GRAY, BLACK));
    draw_topbar();
    console_goto(2, 0);

    console_set_attr(C_LABEL);
    kprintf("BIOS E820 MEMORY MAP");
    console_set_attr(C_DARK);
    kprintf("  /  %u entries\n", g_e820_count);
    kprintf("--------------------------------------------------------------------------------\n");

    if (!g_boot_ok) {
        console_set_attr(VGA_ATTR(LIGHT_RED, BLACK));
        kprintf("No usable firmware memory map was passed to the kernel.\n");
        return;
    }

    console_set_attr(C_LABEL);
    kprintf("  type         base               length             range end\n");
    console_set_attr(C_VALUE);
    for (uint32_t i = 0; i < g_e820_count; i++) {
        const e820_entry_t *e = &g_boot->entries[i];
        uint64_t end = e->base + e->length;
        const char *name = e820_name(e->type);
        console_puts("  ");
        console_puts(name);
        for (int pad = (int)kstrlen(name); pad < 12; pad++)
            console_putc(' ');
        kprintf(" 0x%016llx 0x%016llx 0x%016llx\n",
                (unsigned long long)e->base,
                (unsigned long long)e->length,
                (unsigned long long)end);
    }
    console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
}

/* ---------------------------------------------------------------- about */
void about_screen(void)
{
    console_clear(VGA_ATTR(LIGHT_GRAY, BLACK));
    draw_topbar();
    console_goto(2, 0);

    console_set_attr(C_LABEL);
    kprintf("ABOUT BYTEOS\n");
    console_set_attr(C_DARK);
    kprintf("--------------------------------------------------------------------------------\n");

    static const char *mascot[6] = {
        "   .-\"\"\"-.",
        " .'  .-.  '.",
        "/   (   )   \\",
        "\\    '-'    /",
        " '.       .'",
        "   '-...-'"
    };
    for (int i = 0; i < 6; i++)
        vga_puts_at(5 + i, 66, mascot[i], C_ART);

    console_set_attr(C_VALUE);
    kprintf("  ByteOS " BYTEOS_VERSION " \"" BYTEOS_CODENAME "\"\n\n");
    kprintf("  A tiny 64-bit bare-metal system, built from scratch.\n");
    kprintf("  It boots from a hand-made El Torito ISO or its own MBR,\n");
    kprintf("  brings the CPU into long mode with identity-mapped\n");
    kprintf("  2 MiB pages, and talks directly to VGA and PS/2.\n\n");
    kprintf("  ByteOS 0.2 adds ByteFS, an ATA installer, fetch,\n");
    kprintf("  ByteDesktop, a file explorer, an HTML editor and a\n");
    kprintf("  local browser that reads the bundled project website.\n\n");
    console_set_attr(C_ACC);
    kprintf("  The signature feature is still the spinning donut.\n");
    console_set_attr(C_DIM);
    kprintf("  Type \"donut\", \"fetch\" or \"start-desktop\".\n\n");
    kprintf("  Source: github.com/withdrawment/byteos\n");
    console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
}

/* ------------------------------------------------------------ line input */
int console_read_line(char *buf, size_t cap, int mask)
{
    size_t len = 0;
    if (cap == 0)
        return 0;

    for (;;) {
        int c = kbd_getchar();
        if (c < 0)
            continue;
        if (c == '\n') {
            console_putc('\n');
            break;
        }
        if (c == '\b') {
            if (len > 0) {
                len--;
                console_putc('\b');
            }
            continue;
        }
        if (c == 27) {
            while (len > 0) {
                len--;
                console_putc('\b');
            }
            continue;
        }
        if (c >= 32 && c < 127 && len + 1 < cap) {
            buf[len++] = (char)c;
            console_putc(mask ? '*' : (char)c);
        }
    }
    buf[len] = 0;
    return (int)len;
}

/* ---------------------------------------------------------------- shell */
static int g_allow_exit;
static int g_exit_requested;

void shell_set_exit_mode(int allow)
{
    g_allow_exit = allow;
    g_exit_requested = 0;
}

static void shell_prompt(void)
{
    console_set_attr(VGA_ATTR(LIGHT_GREEN, BLACK));
    console_puts(auth_username());
    console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
    console_puts("@");
    console_set_attr(VGA_ATTR(LIGHT_CYAN, BLACK));
    console_puts(auth_hostname());
    console_set_attr(VGA_ATTR(LIGHT_GREEN, BLACK));
    console_puts(auth_is_root() ? "# " : "> ");
    console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
}

static void ls_command(void)
{
    if (!bfs_mounted()) {
        console_set_attr(VGA_ATTR(LIGHT_RED, BLACK));
        kprintf("No ByteFS disk mounted. Boot an installed disk or run byte-install.\n");
        console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
        return;
    }
    console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
    int n = 0;
    for (int i = 0; i < BFS_MAX_FILES; i++) {
        bfs_file_t *f = bfs_file_at(i);
        if (!f)
            continue;
        console_set_attr(f->flags & BFS_FILE_HTML ? VGA_ATTR(YELLOW, BLACK)
                                                  : VGA_ATTR(LIGHT_CYAN, BLACK));
        console_puts("  ");
        padded(f->name, 32);
        console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
        kprintf("%u bytes\n", f->size);
        n++;
    }
    if (n == 0)
        kprintf("  (empty filesystem)\n");
}

static void cat_command(const char *path)
{
    if (!bfs_mounted()) {
        console_set_attr(VGA_ATTR(LIGHT_RED, BLACK));
        kprintf("No ByteFS disk mounted.\n");
        console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
        return;
    }
    static char buf[32768];
    uint32_t len = 0;
    if (bfs_read(path, buf, sizeof(buf), &len) != 0) {
        console_set_attr(VGA_ATTR(LIGHT_RED, BLACK));
        kprintf("cat: cannot read %s\n", path);
        console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
        return;
    }
    console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
    console_puts(buf);
    if (len && buf[len - 1] != '\n')
        console_putc('\n');
}

static void run_command(const char *raw)
{
    char line[80];
    str_copy(line, raw, sizeof(line));

    char *p = line;
    while (*p == ' ')
        p++;

    int end = (int)kstrlen(p);
    while (end > 0 && p[end - 1] == ' ')
        p[--end] = 0;

    if (*p == 0)
        return;

    if (str_eq_ci(p, "help") || str_eq_ci(p, "?")) {
        help_screen();
    } else if (str_eq_ci(p, "specs") || str_eq_ci(p, "sysinfo")) {
        specs_screen();
    } else if (str_eq_ci(p, "memmap") || str_eq_ci(p, "e820")) {
        memmap_screen();
    } else if (str_eq_ci(p, "about")) {
        about_screen();
    } else if (str_eq_ci(p, "donut")) {
        donut_mode();
    } else if (str_eq_ci(p, "logo") || str_eq_ci(p, "home")) {
        home_screen();
    } else if (str_eq_ci(p, "clear") || str_eq_ci(p, "cls")) {
        console_clear_region(VGA_ATTR(LIGHT_GRAY, BLACK));
    } else if (str_eq_ci(p, "start-desktop") || str_eq_ci(p, "desktop")) {
        if (g_allow_exit) {
            console_set_attr(VGA_ATTR(YELLOW, BLACK));
            kprintf("Already inside ByteDesktop. Type exit to return.\n");
            console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
        } else {
            desktop();
            home_screen();
        }
    } else if (str_eq_ci(p, "reboot")) {
        reboot();
    } else if (str_eq_ci(p, "fetch")) {
        fetch_screen();
        if (!desktop_active())
            home_screen();
    } else if (str_eq_ci(p, "ls") || str_eq_ci(p, "dir")) {
        ls_command();
    } else if (str_eq_ci(p, "cat")) {
        console_set_attr(VGA_ATTR(LIGHT_RED, BLACK));
        kprintf("usage: cat <path>\n");
        console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
    } else if (p[0] == 'c' && p[1] == 'a' && p[2] == 't' && p[3] == ' ') {
        const char *path = p + 4;
        while (*path == ' ')
            path++;
        if (*path)
            cat_command(path);
    } else if (str_eq_ci(p, "whoami")) {
        console_set_attr(VGA_ATTR(LIGHT_CYAN, BLACK));
        kprintf("%s%s\n", auth_username(), auth_is_root() ? " (root)" : "");
        console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
    } else if (str_eq_ci(p, "su")) {
        auth_su();
    } else if (str_eq_ci(p, "passwd")) {
        auth_passwd();
    } else if (str_eq_ci(p, "byte-install")) {
        install_wizard();
    } else if (str_eq_ci(p, "exit")) {
        if (g_allow_exit)
            g_exit_requested = 1;
        else {
            console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
            kprintf("There is no parent session to return to.\n");
        }
    } else if (str_eq_ci(p, "version")) {
        console_set_attr(C_ACC);
        kprintf("ByteOS " BYTEOS_VERSION " \"" BYTEOS_CODENAME "\"  /  x86_64\n");
        console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
    } else {
        console_set_attr(VGA_ATTR(LIGHT_RED, BLACK));
        kprintf("unknown command: %s\n", p);
        console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
        console_puts("type \"help\" for a list of commands\n");
    }
}

void shell(void)
{
    char line[72];
    int len = 0;

    shell_prompt();

    for (;;) {
        int c = kbd_getchar();
        if (c < 0)
            continue;

        if (c == '\n') {
            line[len] = 0;
            console_putc('\n');
            run_command(line);
            len = 0;
            if (g_exit_requested)
                break;
            shell_prompt();
        } else if (c == '\b') {
            if (len > 0) {
                len--;
                console_putc('\b');
            }
        } else if (c == 27) {
            while (len > 0) {
                len--;
                console_putc('\b');
            }
        } else if (c >= 32 && c < 127) {
            if (len < (int)sizeof(line) - 1) {
                line[len++] = (char)c;
                console_putc((char)c);
            }
        }
    }
}
