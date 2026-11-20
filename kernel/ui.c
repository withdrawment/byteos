#include "kernel.h"

#define C_LABEL VGA_ATTR(LIGHT_CYAN, BLACK)
#define C_VALUE VGA_ATTR(WHITE, BLACK)
#define C_DIM   VGA_ATTR(LIGHT_GRAY, BLACK)
#define C_DARK  VGA_ATTR(DARK_GRAY, BLACK)
#define C_ACC   VGA_ATTR(YELLOW, BLACK)
#define C_ART   VGA_ATTR(LIGHT_MAGENTA, BLACK)
#define C_BAR   VGA_ATTR(BLACK, CYAN)
#define C_YELLOW VGA_ATTR(YELLOW, BLACK)

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
        { "#### ", "#   #", "#### ", "#   #", "#### " },  
        { "#   #", "#   #", " # # ", "  #  ", "  #  " },  
        { "#####", "  #  ", "  #  ", "  #  ", "  #  " },  
        { "#####", "#    ", "#### ", "#    ", "#####" },  
        { " ### ", "#   #", "#   #", "#   #", " ### " },  
        { " ####", "#    ", " ### ", "    #", "#### " }   
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
    kprintf("x86_64 long mode  |  %s  |  %s",
            vga_probe() ? "VGA 80x25" : "no VGA",
            (g_boot_ok && (g_boot->boot_flags & BOOT_FLAG_DISK))
                ? "BIOS MBR + ByteFS" : "BIOS El Torito/USB");
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

static void help_cell(int row, int col, const char *cmd, const char *desc)
{
    vga_puts_at(row, col, cmd, C_ACC);
    int len = (int)kstrlen(cmd);
    while (len < 14) {
        vga_put_at(row, col + len, ' ', C_DIM);
        len++;
    }
    vga_puts_at(row, col + 14, desc, C_DIM);
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

    console_set_attr(C_ACC);
    vga_puts_at(4, 2, "SYSTEM", C_ACC);
    vga_puts_at(4, 41, "FILES / SHELL", C_ACC);

    static const char *sys_cmd[16] = {
        "help", "fetch", "start-desktop", "byte-install", "specs",
        "memmap", "about", "donut", "logo", "clear",
        "reboot", "version", "lsblk", "df", "free", "tileconf"
    };
    static const char *sys_desc[16] = {
        "show this command list", "system summary", "choose a desktop",
        "install to an ATA disk", "CPU, memory report", "BIOS E820 map",
        "about ByteOS", "spinning ASCII donut", "redraw the boot screen",
        "clear the screen", "warm restart", "version banner",
        "list block devices", "filesystem usage", "memory usage",
        "ByteTile config file"
    };
    static const char *sh_cmd[15] = {
        "ls / dir", "cd", "pwd", "mkdir", "rmdir",
        "touch", "rm", "cat", "whoami / su", "passwd",
        "uname", "date", "uptime", "hostname", "echo"
    };
    static const char *sh_desc[15] = {
        "list directory", "change directory", "print working dir",
        "create a directory", "remove empty dir", "create empty file",
        "delete a file", "print a file", "account / root", "change password",
        "system name", "current date/time", "how long it is up",
        "print hostname", "print arguments"
    };

    for (int i = 0; i < 16; i++) {
        help_cell(5 + i, 2, sys_cmd[i], sys_desc[i]);
        if (i < 15)
            help_cell(5 + i, 41, sh_cmd[i], sh_desc[i]);
    }

    console_set_attr(C_DARK);
    console_goto(21, 0);
    kprintf("  Start-desktop offers ByteDesktop, ByteWin 95 or ByteTile.\n");
    kprintf("  Commands are case-insensitive. \"sysinfo\" aliases \"specs\".\n");
    console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
}

 
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
    kprintf(vga_probe() ? "VGA text 80x25, mode 3, 16 colours\n"
                        : "No VGA-compatible adapter detected\n");

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
    kprintf("  ByteDesktop, ByteWin 95 and ByteTile desktops, a\n");
    kprintf("  file explorer, an HTML editor and a local browser\n");
    kprintf("  that reads the bundled project website.\n\n");
    console_set_attr(C_ACC);
    kprintf("  The signature feature is still the spinning donut.\n");
    console_set_attr(C_DIM);
    kprintf("  Type \"donut\", \"fetch\" or \"start-desktop\".\n\n");
    kprintf("  Source: github.com/withdrawment/byteos\n");
    console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
}

 
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

 
static int g_allow_exit;
static int g_exit_requested;

void shell_set_exit_mode(int allow)
{
    g_allow_exit = allow;
    g_exit_requested = 0;
}

static char g_cwd[64] = "/";

static void shell_prompt(void)
{
    console_set_attr(VGA_ATTR(LIGHT_GREEN, BLACK));
    console_puts(auth_username());
    console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
    console_puts("@");
    console_set_attr(VGA_ATTR(LIGHT_CYAN, BLACK));
    console_puts(auth_hostname());
    console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
    console_puts(":");
    console_set_attr(VGA_ATTR(LIGHT_BLUE, BLACK));
    console_puts(str_eq(g_cwd, "/home") ? "~" : g_cwd);
    console_set_attr(VGA_ATTR(LIGHT_GREEN, BLACK));
    console_puts(auth_is_root() ? "# " : "$ ");
    console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
}

static const char *fs_path(const char *abs)
{
    return abs[0] == '/' ? abs + 1 : abs;
}

static int prefix_ci(const char *s, const char *prefix, size_t n)
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
    }
    return 1;
}

static void path_error(const char *cmd, const char *path, const char *why)
{
    console_set_attr(VGA_ATTR(LIGHT_RED, BLACK));
    kprintf("%s: %s: %s\n", cmd, path, why);
    console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
}

 

static int match_cmd(const char *p, const char *name, const char **arg)
{
    size_t n = kstrlen(name);
    for (size_t i = 0; i < n; i++) {
        char a = p[i];
        char b = name[i];
        if (a >= 'A' && a <= 'Z')
            a = (char)(a + 32);
        if (b >= 'A' && b <= 'Z')
            b = (char)(b + 32);
        if (a != b)
            return 0;
    }
    if (p[n] != 0 && p[n] != ' ')
        return 0;
    const char *a = p + n;
    while (*a == ' ')
        a++;
    if (arg)
        *arg = a;
    return 1;
}

static void ls_command(const char *arg)
{
    if (!bfs_mounted()) {
        console_set_attr(VGA_ATTR(LIGHT_RED, BLACK));
        kprintf("No ByteFS disk mounted. Boot an installed disk or run byte-install.\n");
        console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
        return;
    }

    char abs[96];
    path_normalize(g_cwd, arg, abs, sizeof(abs));
    if (!bfs_is_dir(abs)) {
        path_error("ls", abs, "Not a directory");
        return;
    }

    static bfs_dir_entry_t entries[64];
    int n = bfs_list_dir(abs, entries, 64);
    console_set_attr(C_DARK);
    kprintf("  total %d\n", n);
    for (int i = 0; i < n; i++) {
        bfs_dir_entry_t *e = &entries[i];
        console_set_attr(e->is_dir ? VGA_ATTR(LIGHT_BLUE, BLACK)
                                   : e->html ? VGA_ATTR(YELLOW, BLACK)
                                             : VGA_ATTR(LIGHT_GRAY, BLACK));
        console_puts("  ");
        console_puts(e->is_dir ? "drwx  " : "-rw-  ");
        padded(e->name, 30);
        if (e->is_dir) {
            console_set_attr(VGA_ATTR(LIGHT_BLUE, BLACK));
            console_puts("dir\n");
        } else {
            console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
            kprintf("%u bytes\n", e->size);
        }
    }
}

static void cat_command(const char *path)
{
    if (!bfs_mounted()) {
        console_set_attr(VGA_ATTR(LIGHT_RED, BLACK));
        kprintf("No ByteFS disk mounted.\n");
        console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
        return;
    }
    char abs[96];
    path_normalize(g_cwd, path, abs, sizeof(abs));
    if (bfs_is_dir(abs)) {
        path_error("cat", abs, "Is a directory");
        return;
    }
    static char buf[32768];
    uint32_t len = 0;
    if (bfs_read(fs_path(abs), buf, sizeof(buf), &len) != 0) {
        console_set_attr(VGA_ATTR(LIGHT_RED, BLACK));
        kprintf("cat: cannot read %s\n", abs);
        console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
        return;
    }
    console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
    console_puts(buf);
    if (len && buf[len - 1] != '\n')
        console_putc('\n');
}

static void cd_command(const char *arg)
{
    if (!bfs_mounted()) {
        console_set_attr(VGA_ATTR(LIGHT_RED, BLACK));
        kprintf("cd: no ByteFS disk mounted\n");
        console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
        return;
    }
    char abs[96];
    if (!arg || !*arg)
        path_normalize(g_cwd, "/home", abs, sizeof(abs));
    else if (str_eq(arg, "~"))
        path_normalize(g_cwd, "/home", abs, sizeof(abs));
    else
        path_normalize(g_cwd, arg, abs, sizeof(abs));

    if (!bfs_is_dir(abs)) {
        path_error("cd", abs, "Not a directory");
        return;
    }
    str_copy(g_cwd, abs, sizeof(g_cwd));
}

static void mkdir_command(const char *arg)
{
    if (!bfs_mounted()) {
        console_set_attr(VGA_ATTR(LIGHT_RED, BLACK));
        kprintf("mkdir: no ByteFS disk mounted\n");
        console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
        return;
    }
    if (!arg || !*arg) {
        kprintf("usage: mkdir <directory>\n");
        return;
    }
    char abs[96];
    path_normalize(g_cwd, arg, abs, sizeof(abs));
    if (str_eq(abs, "/")) {
        path_error("mkdir", abs, "File exists");
        return;
    }
    int rc = bfs_mkdir(fs_path(abs));
    if (rc != 0)
        path_error("mkdir", abs, "File exists or parent missing");
}

static void rmdir_command(const char *arg)
{
    if (!bfs_mounted()) {
        kprintf("rmdir: no ByteFS disk mounted\n");
        return;
    }
    if (!arg || !*arg) {
        kprintf("usage: rmdir <directory>\n");
        return;
    }
    char abs[96];
    path_normalize(g_cwd, arg, abs, sizeof(abs));
    int rc = bfs_rmdir(fs_path(abs));
    if (rc == -2)
        path_error("rmdir", abs, "Not a directory");
    else if (rc == -3)
        path_error("rmdir", abs, "Directory not empty");
    else if (rc != 0)
        path_error("rmdir", abs, "Cannot remove");
}

static void touch_command(const char *arg)
{
    if (!bfs_mounted()) {
        kprintf("touch: no ByteFS disk mounted\n");
        return;
    }
    if (!arg || !*arg) {
        kprintf("usage: touch <file>\n");
        return;
    }
    char abs[96];
    path_normalize(g_cwd, arg, abs, sizeof(abs));
    if (bfs_is_dir(abs)) {
        path_error("touch", abs, "Is a directory");
        return;
    }
    if (!bfs_exists(fs_path(abs))) {
        if (bfs_add(fs_path(abs), "", 0, bfs_is_html(abs)) != 0)
            path_error("touch", abs, "Cannot create file");
    }
}

static void rm_command(const char *arg)
{
    if (!bfs_mounted()) {
        kprintf("rm: no ByteFS disk mounted\n");
        return;
    }
    if (!arg || !*arg) {
        kprintf("usage: rm <file>\n");
        return;
    }
    char abs[96];
    path_normalize(g_cwd, arg, abs, sizeof(abs));
    if (!bfs_exists(fs_path(abs))) {
        path_error("rm", abs, "No such file");
        return;
    }
    int rc = bfs_unlink(fs_path(abs));
    if (rc == -2)
        path_error("rm", abs, "Is a directory (use rmdir)");
    else if (rc != 0)
        path_error("rm", abs, "Cannot remove");
}

static void lsblk_command(void)
{
    console_set_attr(C_LABEL);
    kprintf("NAME    SIZE      TYPE  MODEL\n");
    console_set_attr(C_VALUE);
    int mounted_slot = bfs_mounted() ? bfs_slot() : -1;
    int any = 0;
    for (int i = 0; i < ATA_MAX_DISKS; i++) {
        ata_disk_t *d = ata_slot(i);
        if (!d)
            continue;
        any = 1;
        char name[8];
        name[0] = 's';
        name[1] = 'd';
        name[2] = (char)('a' + i);
        name[3] = 0;
        padded(name, 8);
        print_size((uint64_t)d->sectors * 512ULL);
        for (int pad = 0; pad < 6; pad++)
            console_putc(' ');
        kprintf("disk  %s\n", d->model[0] ? d->model : "ATA disk");

        if (i == mounted_slot) {
            console_set_attr(C_DIM);
            console_puts("|-");
            padded(name, 6);
            print_size((uint64_t)(d->sectors - BFS_PART_LBA) * 512ULL);
            for (int pad = 0; pad < 6; pad++)
                console_putc(' ');
            kprintf("part  ByteFS\n");
            console_set_attr(C_VALUE);
        }
    }
    if (!any) {
        console_set_attr(C_YELLOW);
        kprintf("  no ATA disks detected\n");
    }
    console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
}

static void df_command(void)
{
    console_set_attr(C_LABEL);
    kprintf("Filesystem      1K-blocks      Used Available Mounted on\n");
    console_set_attr(C_VALUE);
    if (bfs_mounted()) {
        ata_disk_t *d = ata_slot(bfs_slot());
        uint64_t total = d ? (uint64_t)(d->sectors - BFS_PART_LBA) * 512ULL : 0;
        uint64_t used = bfs_used_bytes();
        uint64_t avail = total > used ? total - used : 0;
        kprintf("/dev/sd%c1     %10llu %9llu %9llu /\n",
                (char)('a' + bfs_slot()),
                (unsigned long long)(total / 1024ULL),
                (unsigned long long)(used / 1024ULL),
                (unsigned long long)(avail / 1024ULL));
    } else {
        kprintf("romfs                 0         0         0 / (live ISO)\n");
    }
    console_set_attr(C_DARK);
    kprintf("ByteFS stores a flat directory; \"Used\" includes block padding.\n");
    console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
}

extern uint8_t __bss_end[];
extern uint8_t __bss_start[];

static void free_command(void)
{
    uint64_t total = g_mem_usable;
    uint64_t used = (uint64_t)((uintptr_t)__bss_end - 0x10000ULL);
    if (used > total)
        used = total;
    uint64_t freeb = total - used;
    console_set_attr(C_LABEL);
    kprintf("              total        used        free      shared  buff/cache\n");
    console_set_attr(C_VALUE);
    kprintf("Mem:      %9lluK %10lluK %10lluK %9lluK %10lluK\n",
            (unsigned long long)(total / 1024ULL),
            (unsigned long long)(used / 1024ULL),
            (unsigned long long)(freeb / 1024ULL),
            0ULL,
            (unsigned long long)(used / 1024ULL));
    console_set_attr(C_DIM);
    kprintf("Swap:               0          0          0\n");
    console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
}

static void uname_command(const char *arg)
{
    if (arg && str_eq(arg, "-a")) {
        kprintf("ByteOS %s 0.2 %s #1 x86_64 GNU/ByteOS\n",
                auth_hostname(), BYTEOS_CODENAME);
    } else if (arg && str_eq(arg, "-r")) {
        kprintf("0.2\n");
    } else if (arg && str_eq(arg, "-m")) {
        kprintf("x86_64\n");
    } else if (arg && str_eq(arg, "-n")) {
        kprintf("%s\n", auth_hostname());
    } else if (arg && str_eq(arg, "-s")) {
        kprintf("ByteOS\n");
    } else {
        kprintf("ByteOS %s x86_64\n", auth_hostname());
    }
}

static int day_of_week(int y, int m, int d)
{
    static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 3)
        y -= 1;
    int w = (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
    return w < 0 ? w + 7 : w;
}

static void date_command(void)
{
    static const char *wd[7] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };
    static const char *mo[12] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    rtc_time_t tm;
    if (rtc_read(&tm) != 0) {
        kprintf("date: RTC unavailable\n");
        return;
    }
    kprintf("%s %s %2d %02d:%02d:%02d %04d\n",
            wd[day_of_week(tm.year, tm.month, tm.day)],
            mo[(tm.month - 1) % 12], tm.day,
            tm.hour, tm.minute, tm.second, tm.year);
}

static uint64_t uptime_seconds(void)
{
    if (!g_tsc_hz || read_tsc() <= g_boot_tsc)
        return 0;
    return (read_tsc() - g_boot_tsc) / g_tsc_hz;
}

static void uptime_command(void)
{
    uint64_t up = uptime_seconds();
    kprintf(" %llu:%02llu:%02llu up ",
            (unsigned long long)(up / 3600ULL),
            (unsigned long long)((up / 60ULL) % 60ULL),
            (unsigned long long)(up % 60ULL));
    uint64_t days = up / 86400ULL;
    if (days)
        kprintf("%llu day%s, ", (unsigned long long)days, days == 1 ? "" : "s");
    kprintf("%llu:%02llu, 1 user, load average: 0.00, 0.00, 0.00\n",
            (unsigned long long)((up / 3600ULL) % 24ULL),
            (unsigned long long)((up / 60ULL) % 60ULL));
}

static void echo_expand(const char *s)
{
    while (*s) {
        if (*s != '$') {
            console_putc(*s++);
            continue;
        }
        s++;
        const char *start = s;
        while ((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z') ||
               (*s >= '0' && *s <= '9') || *s == '_')
            s++;
        size_t n = (size_t)(s - start);
        if (n == 0) {
            console_putc('$');
            continue;
        }
        if (n == 4 && prefix_ci(start, "USER", 4))
            console_puts(auth_username());
        else if (n == 4 && prefix_ci(start, "HOME", 4))
            console_puts("/home");
        else if (n == 3 && prefix_ci(start, "PWD", 3))
            console_puts(g_cwd);
        else if (n == 8 && prefix_ci(start, "HOSTNAME", 8))
            console_puts(auth_hostname());
        else if (n == 5 && prefix_ci(start, "SHELL", 5))
            console_puts("bytesh");
        else {
            console_putc('$');
            for (size_t i = 0; i < n; i++)
                console_putc(start[i]);
        }
    }
    console_putc('\n');
}

static void echo_command(const char *arg)
{
    if (!arg)
        arg = "";
    char buf[128];
    str_copy(buf, arg, sizeof(buf));
    size_t len = kstrlen(buf);
    if (len >= 2 && ((buf[0] == '"' && buf[len - 1] == '"') ||
                     (buf[0] == '\'' && buf[len - 1] == '\''))) {
        buf[len - 1] = 0;
        echo_expand(buf + 1);
    } else {
        echo_expand(buf);
    }
}

 
static void option_box(int row, int col, int w, const char *title,
                       const char *l1, const char *l2, int selected)
{
    uint8_t bg = selected ? VGA_ATTR(BLACK, CYAN)
                          : VGA_ATTR(LIGHT_GRAY, BLACK);
    uint8_t fg = selected ? VGA_ATTR(WHITE, CYAN)
                          : VGA_ATTR(LIGHT_CYAN, BLACK);
    uint8_t dim = selected ? VGA_ATTR(LIGHT_GRAY, CYAN)
                           : VGA_ATTR(DARK_GRAY, BLACK);
    for (int r = 0; r < 5; r++)
        for (int c = 0; c < w; c++)
            vga_put_at(row + r, col + c, ' ', bg);
    vga_puts_at(row, col + ((w - (int)kstrlen(title)) / 2), title, fg);
    vga_puts_at(row + 2, col + 2, l1, bg);
    vga_puts_at(row + 3, col + 2, l2, dim);
    if (selected) {
        vga_put_at(row, col, '+', fg);
        vga_put_at(row, col + w - 1, '+', fg);
        vga_put_at(row + 4, col, '+', fg);
        vga_put_at(row + 4, col + w - 1, '+', fg);
    }
}

static int desktop_chooser(void)
{
    int selected = 0;
    for (;;) {
        console_clear(VGA_ATTR(LIGHT_GRAY, BLACK));
        draw_topbar();
        centered(2, "CHOOSE A DESKTOP", C_ACC);
        centered(3, "All desktops share the same ByteOS shell, files and apps.", C_DARK);

        option_box(6, 3, 24, "1  ByteDesktop",
                   "80x25 text desktop", "Keyboard driven", selected == 0);
        option_box(6, 27, 24, "2  ByteWin 95",
                   "640x480 graphical GUI", "Mouse, windows, icons", selected == 1);
        option_box(6, 51, 24, "3  ByteTile",
                   "Tiling window manager", "dwm-style, edit config", selected == 2);

        centered(13, "ByteDesktop keeps the original TUI applications.", C_DIM);
        centered(14, "ByteWin 95 is a real VGA GUI with windows and icons.", C_DIM);
        centered(15, "ByteTile tiles windows with tags and /bytetile.conf.", C_DIM);

        vga_fill_row(24, C_BAR);
        vga_puts_at(24, 2, "Up/Down or Tab select   Enter start   Esc cancel", C_BAR);
        vga_puts_at(24, 65, "desktop", C_BAR);

        int key = kbd_waitkey();
        if (key == KEY_UP)
            selected = (selected + 2) % 3;
        else if (key == KEY_DOWN || key == '\t')
            selected = (selected + 1) % 3;
        else if (key == '1')
            return 0;
        else if (key == '2')
            return 1;
        else if (key == '3')
            return 2;
        else if (key == '\n' || key == ' ')
            return selected;
        else if (key == 27)
            return -1;
    }
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

    const char *arg = 0;

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
    } else if (match_cmd(p, "start-desktop", &arg) ||
               match_cmd(p, "desktop", &arg)) {
        if (g_allow_exit) {
            console_set_attr(VGA_ATTR(YELLOW, BLACK));
            kprintf("Already inside ByteDesktop. Type exit to return.\n");
            console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
        } else {
            int choice;
            if (str_eq_ci(arg, "classic") || str_eq_ci(arg, "text"))
                choice = 0;
            else if (str_eq_ci(arg, "win95") || str_eq_ci(arg, "gui") ||
                     str_eq_ci(arg, "win"))
                choice = 1;
            else if (str_eq_ci(arg, "tile") || str_eq_ci(arg, "tiling") ||
                     str_eq_ci(arg, "dwm") || str_eq_ci(arg, "bytetile"))
                choice = 2;
            else
                choice = desktop_chooser();

            if (choice == 1) {
                if (win95_desktop() != 0) {
                    console_set_attr(VGA_ATTR(LIGHT_RED, BLACK));
                    kprintf("\nByteWin 95 could not start; using ByteDesktop instead.\n");
                    console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
                    desktop();
                }
            } else if (choice == 2) {
                if (tilewm_desktop() != 0) {
                    console_set_attr(VGA_ATTR(LIGHT_RED, BLACK));
                    kprintf("\nByteTile could not start; using ByteDesktop instead.\n");
                    console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
                    desktop();
                }
            } else if (choice == 0) {
                desktop();
            }
            home_screen();
        }
    } else if (str_eq_ci(p, "reboot")) {
        reboot();
    } else if (str_eq_ci(p, "fetch")) {
        fetch_screen();
        if (!desktop_active())
            home_screen();
    } else if (match_cmd(p, "ls", &arg) || match_cmd(p, "dir", &arg)) {
        ls_command(arg);
    } else if (match_cmd(p, "cd", &arg)) {
        cd_command(arg);
    } else if (str_eq_ci(p, "pwd")) {
        kprintf("%s\n", g_cwd);
    } else if (match_cmd(p, "mkdir", &arg)) {
        mkdir_command(arg);
    } else if (match_cmd(p, "rmdir", &arg)) {
        rmdir_command(arg);
    } else if (match_cmd(p, "touch", &arg)) {
        touch_command(arg);
    } else if (match_cmd(p, "rm", &arg)) {
        rm_command(arg);
    } else if (match_cmd(p, "cat", &arg)) {
        if (*arg)
            cat_command(arg);
        else {
            console_set_attr(VGA_ATTR(LIGHT_RED, BLACK));
            kprintf("usage: cat <path>\n");
            console_set_attr(VGA_ATTR(LIGHT_GRAY, BLACK));
        }
    } else if (str_eq_ci(p, "lsblk")) {
        lsblk_command();
    } else if (str_eq_ci(p, "df")) {
        df_command();
    } else if (str_eq_ci(p, "free")) {
        free_command();
    } else if (match_cmd(p, "uname", &arg)) {
        uname_command(arg);
    } else if (str_eq_ci(p, "date")) {
        date_command();
    } else if (str_eq_ci(p, "uptime")) {
        uptime_command();
    } else if (str_eq_ci(p, "hostname")) {
        kprintf("%s\n", auth_hostname());
    } else if (match_cmd(p, "echo", &arg)) {
        echo_command(arg);
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
    } else if (str_eq_ci(p, "tileconf")) {
        tile_config_info();
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
