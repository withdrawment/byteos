#include "kernel.h"
#include <stdarg.h>

 
boot_info_t *g_boot;
int g_boot_ok;
uint32_t g_e820_count;
uint64_t g_mem_usable;
uint64_t g_mem_top;
char g_cpu_vendor[13];
char g_cpu_brand[49];
uint32_t g_cpu_family;
uint32_t g_cpu_model;
uint32_t g_cpu_stepping;
uint32_t g_logical_cpus;
int g_hypervisor;
cpu_features_t g_features;
uint32_t g_max_leaf;
uint32_t g_max_ext;
uint64_t g_tsc_hz;
uint64_t g_boot_tsc;

static int g_row;
static int g_col;
static int g_top;
static int g_bottom = VGA_HEIGHT - 1;
static uint8_t g_attr = VGA_ATTR(LIGHT_GRAY, BLACK);

 
void *memset(void *dst, int c, size_t n)
{
    void *ret = dst;
    __asm__ volatile("cld; rep stosb"
                     : "+D"(dst), "+c"(n)
                     : "a"((unsigned char)c)
                     : "memory");
    return ret;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    void *ret = dst;
    __asm__ volatile("cld; rep movsb"
                     : "+D"(dst), "+S"(src), "+c"(n)
                     :
                     : "memory");
    return ret;
}

void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d == s || n == 0)
        return dst;
    if ((uintptr_t)d < (uintptr_t)s) {
        while (n--)
            *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--)
            *--d = *--s;
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *pa = (const unsigned char *)a;
    const unsigned char *pb = (const unsigned char *)b;
    while (n--) {
        if (*pa != *pb)
            return (int)*pa - (int)*pb;
        pa++;
        pb++;
    }
    return 0;
}

size_t kstrlen(const char *s)
{
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}

void str_copy(char *dst, const char *src, size_t cap)
{
    size_t i = 0;
    if (cap == 0)
        return;
    while (i + 1 < cap && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static char lower_ascii(char c)
{
    if (c >= 'A' && c <= 'Z')
        return (char)(c + ('a' - 'A'));
    return c;
}

int str_eq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b)
            return 0;
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

int str_eq_ci(const char *a, const char *b)
{
    while (*a && *b) {
        if (lower_ascii(*a) != lower_ascii(*b))
            return 0;
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

 
static void update_cursor(void)
{
    uint16_t pos = (uint16_t)(g_row * VGA_WIDTH + g_col);
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0x0F), "Nd"((uint16_t)0x3D4));
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)(pos & 0xFF)), "Nd"((uint16_t)0x3D5));
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0x0E), "Nd"((uint16_t)0x3D4));
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)(pos >> 8)), "Nd"((uint16_t)0x3D5));
}

void console_set_attr(uint8_t attr)
{
    g_attr = attr;
}

void console_set_region(int top, int bottom)
{
    if (top < 0)
        top = 0;
    if (bottom >= VGA_HEIGHT)
        bottom = VGA_HEIGHT - 1;
    if (bottom < top)
        bottom = top;
    g_top = top;
    g_bottom = bottom;
    g_row = top;
    g_col = 0;
    update_cursor();
}

void console_get_region(int *top, int *bottom)
{
    if (top)
        *top = g_top;
    if (bottom)
        *bottom = g_bottom;
}

void console_get_cursor(int *row, int *col)
{
    if (row)
        *row = g_row;
    if (col)
        *col = g_col;
}

void console_clear_region(uint8_t attr)
{
    g_attr = attr;
    uint16_t cell = (uint16_t)(((uint16_t)attr << 8) | ' ');
    for (int row = g_top; row <= g_bottom; row++) {
        for (int col = 0; col < VGA_WIDTH; col++)
            VGA_MEMORY[row * VGA_WIDTH + col] = cell;
    }
    g_row = g_top;
    g_col = 0;
    update_cursor();
}

void console_clear(uint8_t attr)
{
    g_top = 0;
    g_bottom = VGA_HEIGHT - 1;
    console_clear_region(attr);
}

void console_init(uint8_t attr)
{
    console_clear(attr);
     
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0x0A), "Nd"((uint16_t)0x3D4));
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0x0E), "Nd"((uint16_t)0x3D5));
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0x0B), "Nd"((uint16_t)0x3D4));
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0x0F), "Nd"((uint16_t)0x3D5));
    update_cursor();
}

void console_goto(int row, int col)
{
    if (row < g_top)
        row = g_top;
    if (row > g_bottom)
        row = g_bottom;
    if (col < 0)
        col = 0;
    if (col >= VGA_WIDTH)
        col = VGA_WIDTH - 1;
    g_row = row;
    g_col = col;
    update_cursor();
}

static void scroll_up(void)
{
    for (int row = g_top; row < g_bottom; row++) {
        for (int col = 0; col < VGA_WIDTH; col++)
            VGA_MEMORY[row * VGA_WIDTH + col] =
                VGA_MEMORY[(row + 1) * VGA_WIDTH + col];
    }
    uint16_t blank = (uint16_t)(((uint16_t)g_attr << 8) | ' ');
    for (int col = 0; col < VGA_WIDTH; col++)
        VGA_MEMORY[g_bottom * VGA_WIDTH + col] = blank;
    g_row = g_bottom;
}

void console_putc(char c)
{
    uint16_t cell = (uint16_t)(((uint16_t)g_attr << 8) | (unsigned char)c);

    serial_putc(c);

    if (c == '\n') {
        g_col = 0;
        g_row++;
    } else if (c == '\r') {
        g_col = 0;
    } else if (c == '\b') {
        if (g_col > 0) {
            g_col--;
            VGA_MEMORY[g_row * VGA_WIDTH + g_col] =
                (uint16_t)(((uint16_t)g_attr << 8) | ' ');
        }
    } else if (c == '\t') {
        int next = (g_col + 8) & ~7;
        while (g_col < next && g_col < VGA_WIDTH) {
            VGA_MEMORY[g_row * VGA_WIDTH + g_col] = cell;
            g_col++;
        }
        if (g_col >= VGA_WIDTH) {
            g_col = 0;
            g_row++;
        }
    } else if ((unsigned char)c >= 32) {
        VGA_MEMORY[g_row * VGA_WIDTH + g_col] = cell;
        g_col++;
        if (g_col >= VGA_WIDTH) {
            g_col = 0;
            g_row++;
        }
    }

    if (g_row > g_bottom)
        scroll_up();
    update_cursor();
}

void console_puts(const char *s)
{
    while (*s)
        console_putc(*s++);
}

void vga_fill_row(int row, uint8_t attr)
{
    if (row < 0 || row >= VGA_HEIGHT)
        return;
    uint16_t cell = (uint16_t)(((uint16_t)attr << 8) | ' ');
    for (int i = 0; i < VGA_WIDTH; i++)
        VGA_MEMORY[row * VGA_WIDTH + i] = cell;
}

void vga_put_at(int row, int col, char c, uint8_t attr)
{
    if (row < 0 || row >= VGA_HEIGHT || col < 0 || col >= VGA_WIDTH)
        return;
    VGA_MEMORY[row * VGA_WIDTH + col] =
        (uint16_t)(((uint16_t)attr << 8) | (unsigned char)c);
}

void vga_puts_at(int row, int col, const char *s, uint8_t attr)
{
    while (*s && col < VGA_WIDTH) {
        vga_put_at(row, col++, *s++, attr);
    }
}

 
static void print_number(uint64_t value, unsigned base, int width, char pad,
                         int upper, int negative)
{
    char tmp[32];
    int n = 0;
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";

    if (value == 0)
        tmp[n++] = '0';
    while (value) {
        tmp[n++] = digits[value % base];
        value /= base;
    }

    int len = n + (negative ? 1 : 0);
    if (pad == ' ') {
        while (len < width) {
            console_putc(' ');
            width--;
        }
    }
    if (negative)
        console_putc('-');
    if (pad == '0') {
        while (len < width) {
            console_putc('0');
            width--;
        }
    }
    for (int i = n - 1; i >= 0; i--)
        console_putc(tmp[i]);
}

void kprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    while (*fmt) {
        if (*fmt != '%') {
            console_putc(*fmt++);
            continue;
        }
        fmt++;
        if (*fmt == '%') {
            console_putc('%');
            fmt++;
            continue;
        }

        char pad = ' ';
        int width = 0;
        int len = 0;

        if (*fmt == '0') {
            pad = '0';
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }
        while (*fmt == 'l') {
            len++;
            fmt++;
        }
        char spec = *fmt;
        if (spec)
            fmt++;

        switch (spec) {
        case 'c':
            console_putc((char)va_arg(ap, int));
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            console_puts(s ? s : "(null)");
            break;
        }
        case 'd':
        case 'i': {
            int64_t sv;
            if (len == 0)
                sv = va_arg(ap, int);
            else if (len == 1)
                sv = va_arg(ap, long);
            else
                sv = va_arg(ap, long long);
            int neg = sv < 0;
            uint64_t mag = neg ? (uint64_t)(-(sv + 1)) + 1u : (uint64_t)sv;
            print_number(mag, 10, width, pad, 0, neg);
            break;
        }
        case 'u':
        case 'x':
        case 'X': {
            uint64_t uv;
            if (len == 0)
                uv = va_arg(ap, unsigned int);
            else if (len == 1)
                uv = va_arg(ap, unsigned long);
            else
                uv = va_arg(ap, unsigned long long);
            print_number(uv, spec == 'u' ? 10 : 16, width, pad, spec == 'X', 0);
            break;
        }
        case 'p': {
            void *p = va_arg(ap, void *);
            console_puts("0x");
            print_number((uint64_t)(uintptr_t)p, 16, 16, '0', 0, 0);
            break;
        }
        default:
            console_putc('%');
            if (spec)
                console_putc(spec);
            break;
        }
    }
    va_end(ap);
}

void print_size(uint64_t bytes)
{
    const uint64_t mib = 1024ULL * 1024ULL;
    const uint64_t gib = 1024ULL * 1024ULL * 1024ULL;

    if (bytes >= gib) {
        uint64_t whole = bytes / gib;
        uint64_t frac = ((bytes % gib) * 10) / gib;
        if (frac)
            kprintf("%llu.%llu GiB", (unsigned long long)whole,
                    (unsigned long long)frac);
        else
            kprintf("%llu GiB", (unsigned long long)whole);
    } else if (bytes >= mib) {
        kprintf("%llu MiB", (unsigned long long)(bytes / mib));
    } else if (bytes >= 1024) {
        kprintf("%llu KiB", (unsigned long long)(bytes / 1024));
    } else {
        kprintf("%llu B", (unsigned long long)bytes);
    }
}

 
extern uint8_t __bss_start[];
extern uint8_t __bss_end[];

static void kmain(uint64_t boot_info_addr)
{
    serial_init();
    serial_puts("\nByteOS 0.2 boot\n");
    console_init(VGA_ATTR(LIGHT_GRAY, BLACK));

    kbd_init();
    serial_puts(vga_probe() ? "vga: ok\n" : "vga: absent\n");
    serial_puts(kbd_present() ? "kbd: ok\n" : "kbd: absent\n");

    g_boot = (boot_info_t *)(uintptr_t)boot_info_addr;
    g_boot_ok = g_boot->magic == BOOT_MAGIC && g_boot->e820_count <= 128;

    scan_memory();
    detect_cpu();
    g_tsc_hz = calibrate_tsc();
    g_boot_tsc = read_tsc();

    serial_puts("cpu: ");
    serial_puts(g_cpu_brand);
    serial_putc('\n');
    serial_puts("memory: ");
    serial_puts(g_boot_ok ? "e820 ok" : "missing");
    serial_putc('\n');

    ata_init();

    for (int i = 0; i < ATA_MAX_DISKS; i++) {
        ata_disk_t *d = ata_slot(i);
        if (!d)
            continue;
        serial_puts(d->kind == ATA_KIND_AHCI ? "disk ahci: " : "disk pio: ");
        serial_puts(d->model);
        serial_puts(" (slot ");
        if (i >= 10)
            serial_putc((char)('0' + i / 10));
        serial_putc((char)('0' + i % 10));
        serial_puts(")\n");
    }

    if (g_boot_ok && (g_boot->boot_flags & BOOT_FLAG_DISK)) {
        int slot = ata_bios_slot((int)g_boot->boot_drive);
        int mounted = 0;

        if (slot >= 0 && bfs_mount(slot, BFS_PART_LBA) == 0 &&
            auth_load() == 0) {
            mounted = 1;
        } else {
            for (int i = 0; i < ATA_MAX_DISKS; i++) {
                if (i == slot || !ata_slot(i))
                    continue;
                if (bfs_mount(i, BFS_PART_LBA) == 0 && auth_load() == 0) {
                    slot = i;
                    mounted = 1;
                    break;
                }
            }
        }

        if (mounted) {
            serial_puts("root: slot ");
            serial_putc((char)('0' + slot % 10));
            serial_putc('\n');
            auth_login();
        } else {
            serial_puts("root: not mounted\n");
        }
    }

    home_screen();
    shell();
}

__attribute__((noreturn, section(".text.entry")))
void _start(uint64_t boot_info_addr)
{
    for (uint8_t *p = __bss_start; p < __bss_end; p++)
        *p = 0;

    kmain(boot_info_addr);

    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}
