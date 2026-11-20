#include "kernel.h"

 
static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint64_t rdtsc_ordered(void)
{
    uint32_t lo, hi;
    __asm__ volatile("lfence; rdtsc" : "=a"(lo), "=d"(hi) : : "memory");
    return ((uint64_t)hi << 32) | lo;
}

uint64_t read_tsc(void)
{
    return rdtsc_ordered();
}

static inline void cpuid(uint32_t leaf, uint32_t subleaf, uint32_t *a,
                         uint32_t *b, uint32_t *c, uint32_t *d)
{
    __asm__ volatile("cpuid"
                     : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
                     : "a"(leaf), "c"(subleaf));
}

 
static uint64_t tsc_from_cpuid(void)
{
    uint32_t a, b, c, d;

    if (g_max_leaf >= 0x15u) {
        cpuid(0x15u, 0, &a, &b, &c, &d);
        if (a && b && c) {
            uint64_t hz = (uint64_t)c * (uint64_t)b / (uint64_t)a;
            if (hz >= 100000000ULL && hz <= 20000000000ULL)
                return hz;
        }
    }
    if (g_max_leaf >= 0x16u) {
        cpuid(0x16u, 0, &a, &b, &c, &d);
        if (a) {
            uint64_t hz = (uint64_t)a * 1000000ULL;
            if (hz >= 100000000ULL && hz <= 20000000000ULL)
                return hz;
        }
    }
    return 0;
}

uint64_t calibrate_tsc(void)
{
     
    uint8_t p61 = inb(0x61);
    outb(0x61, (uint8_t)(p61 & ~0x03));       
    outb(0x43, 0xB0);                         
    outb(0x42, 0x9C);                         
    outb(0x42, 0x2E);                         
    outb(0x61, (uint8_t)((p61 & ~0x02) | 0x01));

    uint64_t t0 = rdtsc_ordered();
    uint32_t guard = 0;
    while (!(inb(0x61) & 0x20)) {
        if (++guard > 50000000u) {
            outb(0x61, p61);
            return tsc_from_cpuid();
        }
    }
    uint64_t t1 = rdtsc_ordered();
    outb(0x61, p61);

    uint64_t hz = (t1 - t0) * 100ULL;
    if (hz < 100000000ULL || hz > 20000000000ULL)
        return tsc_from_cpuid();
    return hz;
}

 
static void trim_brand(char *dst, const char *src)
{
    int end = 48;
    while (end > 0 && (src[end - 1] == ' ' || src[end - 1] == 0))
        end--;
    int start = 0;
    while (start < end && src[start] == ' ')
        start++;

    int n = end - start;
    if (n <= 0) {
        str_copy(dst, "unknown", 49);
        return;
    }
    if (n > 48)
        n = 48;
    memcpy(dst, src + start, (size_t)n);
    dst[n] = 0;
}

void detect_cpu(void)
{
    uint32_t a, b, c, d;

    cpuid(0, 0, &a, &b, &c, &d);
    g_max_leaf = a;

    memcpy(g_cpu_vendor, &b, 4);
    memcpy(g_cpu_vendor + 4, &d, 4);
    memcpy(g_cpu_vendor + 8, &c, 4);
    g_cpu_vendor[12] = 0;

    cpuid(0x80000000u, 0, &a, &b, &c, &d);
    g_max_ext = a;

    char raw_brand[49];
    for (int i = 0; i < 48; i++)
        raw_brand[i] = 0;
    if (g_max_ext >= 0x80000004u) {
        for (int i = 0; i < 3; i++) {
            cpuid(0x80000002u + (uint32_t)i, 0, &a, &b, &c, &d);
            memcpy(raw_brand + i * 16 + 0, &a, 4);
            memcpy(raw_brand + i * 16 + 4, &b, 4);
            memcpy(raw_brand + i * 16 + 8, &c, 4);
            memcpy(raw_brand + i * 16 + 12, &d, 4);
        }
    }
    raw_brand[48] = 0;
    trim_brand(g_cpu_brand, raw_brand);

    cpuid(1, 0, &a, &b, &c, &d);
    uint32_t version = a;
    uint32_t base_family = (version >> 8) & 0x0Fu;
    uint32_t base_model = (version >> 4) & 0x0Fu;
    g_cpu_family = base_family;
    if (base_family == 0x0Fu)
        g_cpu_family += (version >> 20) & 0xFFu;
    g_cpu_model = base_model;
    if (base_family == 0x06u || base_family == 0x0Fu)
        g_cpu_model += ((version >> 16) & 0x0Fu) << 4;
    g_cpu_stepping = version & 0x0Fu;

    g_features.f1_edx = d;
    g_features.f1_ecx = c;
    g_hypervisor = (int)((c >> 31) & 1u);
    uint32_t best_cpus = (b >> 16) & 0xFFu;

     

    if (g_max_leaf >= 0x1Fu) {
        cpuid(0x1Fu, 0, &a, &b, &c, &d);
        uint32_t n = b & 0xFFFFu;
        if (n > best_cpus)
            best_cpus = n;
    }
    if (g_max_leaf >= 0x0Bu) {
        cpuid(0x0Bu, 0, &a, &b, &c, &d);
        uint32_t n = b & 0xFFFFu;
        if (n > best_cpus)
            best_cpus = n;
    }

     
    if (g_max_ext >= 0x80000008u) {
        cpuid(0x80000008u, 0, &a, &b, &c, &d);
        uint32_t cores = (c & 0xFFu) + 1u;
        uint32_t threads = 1u;
        if (g_max_ext >= 0x8000001Eu) {
            cpuid(0x8000001Eu, 0, &a, &b, &c, &d);
            threads = ((b >> 8) & 0xFFu) + 1u;
        }
        uint32_t total = cores * threads;
        if (total > best_cpus)
            best_cpus = total;
    }

    g_logical_cpus = best_cpus ? best_cpus : 1u;

    if (g_max_leaf >= 7) {
        cpuid(7, 0, &a, &b, &c, &d);
        g_features.f7_ebx = b;
        g_features.f7_ecx = c;
    } else {
        g_features.f7_ebx = 0;
        g_features.f7_ecx = 0;
    }

    if (g_max_ext >= 0x80000001u) {
        cpuid(0x80000001u, 0, &a, &b, &c, &d);
        g_features.ext_edx = d;
    } else {
        g_features.ext_edx = 0;
    }
}

 
void scan_memory(void)
{
    g_e820_count = 0;
    g_mem_usable = 0;
    g_mem_top = 0;

    if (!g_boot_ok)
        return;

    uint32_t n = g_boot->e820_count;
    if (n > 128)
        n = 128;
    g_e820_count = n;

    for (uint32_t i = 0; i < n; i++) {
        const e820_entry_t *e = &g_boot->entries[i];
        if (e->length == 0)
            continue;
        uint64_t end = e->base + e->length;
        if (end < e->base)
            end = ~0ULL;
        if (end > g_mem_top)
            g_mem_top = end;
        if (e->type == 1)
            g_mem_usable += e->length;
    }
}

 
static uint8_t cmos_read(uint8_t reg)
{
    outb(0x70, reg);
    return inb(0x71);
}

static int bcd_to_bin(uint8_t v)
{
    return (int)(v & 0x0Fu) + (int)((v >> 4) * 10u);
}

int rtc_read(rtc_time_t *out)
{
    uint8_t sec = 0, min = 0, hour = 0, day = 0, mon = 0, year = 0;
    uint8_t century = 0, reg_b = 0;
    int stable = 0;

    for (int tries = 0; tries < 8; tries++) {
        while (cmos_read(0x0A) & 0x80)
            ;
        sec = cmos_read(0x00);
        min = cmos_read(0x02);
        hour = cmos_read(0x04);
        day = cmos_read(0x07);
        mon = cmos_read(0x08);
        year = cmos_read(0x09);
        century = cmos_read(0x32);
        reg_b = cmos_read(0x0B);
        while (cmos_read(0x0A) & 0x80)
            ;
        if (sec == cmos_read(0x00)) {
            stable = 1;
            break;
        }
    }
    if (!stable)
        return -1;

    int is_bcd = (reg_b & 0x04) == 0;
    int is_24h = (reg_b & 0x02) != 0;
    int pm = 0;

    if (!is_24h) {
        pm = (hour & 0x80) != 0;
        hour &= 0x7F;
    }
    if (is_bcd) {
        sec = (uint8_t)bcd_to_bin(sec);
        min = (uint8_t)bcd_to_bin(min);
        hour = (uint8_t)bcd_to_bin(hour);
        day = (uint8_t)bcd_to_bin(day);
        mon = (uint8_t)bcd_to_bin(mon);
        year = (uint8_t)bcd_to_bin(year);
        century = (uint8_t)bcd_to_bin(century);
    }
    if (!is_24h) {
        if (hour == 12)
            hour = 0;
        if (pm)
            hour = (uint8_t)(hour + 12);
    }

    int cy = century;
    if (cy < 19 || cy > 30)
        cy = (year < 70) ? 20 : 19;

    int full_year = cy * 100 + year;
    if (mon < 1 || mon > 12 || day < 1 || day > 31 ||
        hour > 23 || min > 59 || sec > 59 || full_year < 2000 ||
        full_year > 2099)
        return -1;

    out->year = full_year;
    out->month = mon;
    out->day = day;
    out->hour = hour;
    out->minute = min;
    out->second = sec;
    return 0;
}

 
static const char keymap[128] = {
    [0x01] = 27, [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
    [0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8', [0x0A] = '9',
    [0x0B] = '0', [0x0C] = '-', [0x0D] = '=', [0x0E] = '\b', [0x0F] = '\t',
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r', [0x14] = 't',
    [0x15] = 'y', [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p',
    [0x1A] = '[', [0x1B] = ']', [0x1C] = '\n',
    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g',
    [0x23] = 'h', [0x24] = 'j', [0x25] = 'k', [0x26] = 'l',
    [0x27] = ';', [0x28] = '\'', [0x29] = '`', [0x2B] = '\\',
    [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v', [0x30] = 'b',
    [0x31] = 'n', [0x32] = 'm', [0x33] = ',', [0x34] = '.', [0x35] = '/',
    [0x39] = ' '
};

static const char shiftmap[128] = {
    [0x01] = 27, [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$',
    [0x06] = '%', [0x07] = '^', [0x08] = '&', [0x09] = '*', [0x0A] = '(',
    [0x0B] = ')', [0x0C] = '_', [0x0D] = '+', [0x0E] = '\b', [0x0F] = '\t',
    [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R', [0x14] = 'T',
    [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I', [0x18] = 'O', [0x19] = 'P',
    [0x1A] = '{', [0x1B] = '}', [0x1C] = '\n',
    [0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D', [0x21] = 'F', [0x22] = 'G',
    [0x23] = 'H', [0x24] = 'J', [0x25] = 'K', [0x26] = 'L',
    [0x27] = ':', [0x28] = '"', [0x29] = '~', [0x2B] = '|',
    [0x2C] = 'Z', [0x2D] = 'X', [0x2E] = 'C', [0x2F] = 'V', [0x30] = 'B',
    [0x31] = 'N', [0x32] = 'M', [0x33] = '<', [0x34] = '>', [0x35] = '?',
    [0x39] = ' '
};

static int g_shift;
static int g_caps;
static int g_ctrl;
static int g_alt;
static int g_super;

static int read_scancode(void)
{
    for (int i = 0; i < 100000; i++) {
        uint8_t st = inb(0x64);
        if ((st & 0x21) == 0x01)
            return inb(0x60);
        if (st & 0x20)
            return -1;
    }
    return -1;
}

int kbd_getkey(void)
{
    for (;;) {
        uint8_t status = inb(0x64);
        if (!(status & 0x01))
            return -1;
        if (status & 0x20)
            return -1;

        uint8_t sc = inb(0x60);

        if (sc == 0xE0) {
            int next = read_scancode();
            if (next < 0)
                return -1;
            if (next == 0x5B || next == 0x5C) {
                g_super = 1;
                continue;
            }
            if (next == 0xDB || next == 0xDC) {
                g_super = 0;
                continue;
            }
            if (next & 0x80)
                continue;
            switch (next) {
            case 0x48: return KEY_UP;
            case 0x50: return KEY_DOWN;
            case 0x4B: return KEY_LEFT;
            case 0x4D: return KEY_RIGHT;
            case 0x49: return KEY_PGUP;
            case 0x51: return KEY_PGDN;
            case 0x47: return KEY_HOME;
            case 0x4F: return KEY_END;
            case 0x53: return KEY_DEL;
            default: continue;
            }
        }

        if (sc == 0xE1) {
            for (int i = 0; i < 5; i++) {
                uint8_t st;
                for (int g = 0; g < 100000; g++) {
                    st = inb(0x64);
                    if (st & 0x01)
                        break;
                }
                if (!(st & 0x01))
                    break;
                (void)inb(0x60);
            }
            continue;
        }

        if (sc & 0x80) {
            uint8_t make = sc & 0x7F;
            if (make == 0x2A || make == 0x36)
                g_shift = 0;
            if (make == 0x1D)
                g_ctrl = 0;
            if (make == 0x38)
                g_alt = 0;
            continue;
        }

        if (sc == 0x2A || sc == 0x36) {
            g_shift = 1;
            continue;
        }
        if (sc == 0x1D) {
            g_ctrl = 1;
            continue;
        }
        if (sc == 0x38) {
            g_alt = 1;
            continue;
        }
        if (sc == 0x3A) {
            g_caps = !g_caps;
            continue;
        }

        if (sc >= 0x3B && sc <= 0x44)
            return KEY_F1 + (sc - 0x3B);
        if (sc == 0x57)
            return KEY_F11;
        if (sc == 0x58)
            return KEY_F12;

        char c = keymap[sc];
        int use_shift = g_shift;
        if (g_caps && c >= 'a' && c <= 'z')
            use_shift = !use_shift;
        if (use_shift)
            c = shiftmap[sc];
        if (!c)
            continue;
        if (g_ctrl && c >= 'a' && c <= 'z')
            return c - 'a' + 1;
        return (unsigned char)c;
    }
}

int kbd_getchar(void)
{
    for (;;) {
        int key = kbd_getkey();
        if (key < 0)
            return -1;
        if (key < 256)
            return key;
    }
}

 

int kbd_waitkey(void)
{
    for (;;) {
        int key = kbd_getkey();
        if (key >= 0)
            return key;
        __asm__ volatile("pause");
    }
}

int kbd_mods(void)
{
    return (g_shift ? KBD_MOD_SHIFT : 0) |
           (g_ctrl ? KBD_MOD_CTRL : 0) |
           (g_alt ? KBD_MOD_ALT : 0) |
           (g_super ? KBD_MOD_SUPER : 0);
}

static int g_serial_ready;

void serial_init(void)
{
    outb(0x3F8 + 7, 0xAE);
    if (inb(0x3F8 + 7) != 0xAE) {
        g_serial_ready = 0;
        return;
    }

    outb(0x3F9, 0x00);
    outb(0x3FB, 0x80);
    outb(0x3F8, 0x01);
    outb(0x3F9, 0x00);
    outb(0x3FB, 0x03);
    outb(0x3FA, 0xC7);
    outb(0x3FC, 0x0B);
    g_serial_ready = 1;
}

int serial_ready(void)
{
    return g_serial_ready;
}

void serial_putc(char c)
{
    if (!g_serial_ready)
        return;
    if (c == '\n')
        serial_putc('\r');
    for (int i = 0; i < 100000; i++) {
        if (inb(0x3FD) & 0x20)
            break;
    }
    outb(0x3F8, (uint8_t)c);
}

void serial_puts(const char *s)
{
    while (*s)
        serial_putc(*s++);
}

void kbd_drain(void)
{
    g_shift = 0;
    g_ctrl = 0;
    g_alt = 0;
    g_super = 0;
    for (int i = 0; i < 64 && (inb(0x64) & 0x01); i++)
        (void)inb(0x60);
}

static int g_mouse_ready;
static int g_mouse_x = 320;
static int g_mouse_y = 240;
static uint8_t g_mouse_buttons;
static uint8_t g_mouse_packet[3];
static int g_mouse_index;
static int g_click_flag;
static int g_release_flag;

static int kbc_wait_write(void)
{
    for (int i = 0; i < 100000; i++) {
        if (!(inb(0x64) & 0x02))
            return 0;
    }
    return -1;
}

static int kbc_wait_read(void)
{
    for (int i = 0; i < 100000; i++) {
        if (inb(0x64) & 0x01)
            return 0;
    }
    return -1;
}

static void kbc_drain_output(void)
{
    for (int i = 0; i < 128; i++) {
        uint8_t st = inb(0x64);
        if (!(st & 0x01))
            break;
        (void)inb(0x60);
    }
}

static int g_kbd_ready;

int kbd_present(void)
{
    return g_kbd_ready;
}

static int kbd_read_byte(uint8_t *value, uint32_t spins)
{
    for (uint32_t i = 0; i < spins; i++) {
        uint8_t st = inb(0x64);
        if (st == 0xFF)
            return -1;
        if (st & 0x01) {
            uint8_t b = inb(0x60);
            if (st & 0x20)
                continue;
            *value = b;
            return 0;
        }
        __asm__ volatile("pause");
    }
    return -1;
}

static int kbd_send_byte(uint8_t value, uint8_t expect, uint32_t spins)
{
    for (int attempt = 0; attempt < 5; attempt++) {
        if (kbc_wait_write() != 0)
            return -1;
        outb(0x60, value);

        int resend = 0;
        for (uint32_t i = 0; i < spins; i++) {
            uint8_t b;
            if (kbd_read_byte(&b, 1) != 0)
                continue;
            if (b == expect)
                return 0;
            if (b == 0xFE) {
                resend = 1;
                break;
            }
        }
        if (!resend)
            return -1;
    }
    return -1;
}

void kbd_init(void)
{
    uint8_t cfg;

    if (g_kbd_ready)
        return;
    g_shift = 0;
    g_ctrl = 0;
    g_alt = 0;
    g_super = 0;

    if (inb(0x64) == 0xFF)
        return;

    kbc_drain_output();

    if (kbc_wait_write() == 0)
        outb(0x64, 0xAE);

    if (kbc_wait_write() == 0)
        outb(0x64, 0x20);
    if (kbc_wait_read() == 0) {
        cfg = inb(0x60);
        cfg |= 0x40;
        cfg &= (uint8_t)~0x10;
        cfg &= (uint8_t)~0x03;
        if (kbc_wait_write() == 0) {
            outb(0x64, 0x60);
            if (kbc_wait_write() == 0)
                outb(0x60, cfg);
        }
    }

    kbc_drain_output();

    int reset_ok = 0;
    for (int attempt = 0; attempt < 3 && !reset_ok; attempt++) {
        if (kbd_send_byte(0xFF, 0xFA, 500000) == 0)
            reset_ok = 1;
    }
    if (reset_ok) {
        for (uint32_t i = 0; i < 500000; i++) {
            uint8_t b;
            if (kbd_read_byte(&b, 1) != 0)
                continue;
            if (b == 0xAA || b == 0xFC || b == 0xFD)
                break;
        }
    }

    kbc_drain_output();

    int scanning = 0;
    for (int attempt = 0; attempt < 5 && !scanning; attempt++) {
        if (kbd_send_byte(0xF4, 0xFA, 500000) == 0)
            scanning = 1;
    }

    g_kbd_ready = scanning;
    kbc_drain_output();
}

static int aux_command(uint8_t value)
{
    if (kbc_wait_write() != 0)
        return -1;
    outb(0x64, 0xD4);
    if (kbc_wait_write() != 0)
        return -1;
    outb(0x60, value);

    for (int i = 0; i < 100000; i++) {
        uint8_t st = inb(0x64);
        if (!(st & 0x01))
            continue;
        uint8_t b = inb(0x60);
        if (st & 0x20) {
            if (b == 0xFA)
                return 0;
        }
    }
    return -1;
}

void mouse_flush(void)
{
    g_mouse_index = 0;
    g_mouse_buttons = 0;
    g_click_flag = 0;
    g_release_flag = 0;
    for (int i = 0; i < 128; i++) {
        uint8_t st = inb(0x64);
        if (!(st & 0x01))
            break;
        if (!(st & 0x20))
            break;
        (void)inb(0x60);
    }
}

void mouse_init(void)
{
    if (g_mouse_ready)
        return;

    if (kbc_wait_write() == 0)
        outb(0x64, 0xA8);

    kbc_drain_output();

    if (kbc_wait_write() != 0)
        return;
    outb(0x64, 0x20);
    if (kbc_wait_read() != 0)
        return;
    uint8_t cfg = inb(0x60);
    cfg = (uint8_t)((cfg | 0x40) & ~0xB0);

    if (kbc_wait_write() != 0)
        return;
    outb(0x64, 0x60);
    if (kbc_wait_write() != 0)
        return;
    outb(0x60, cfg);

    aux_command(0xF6);
    aux_command(0xF3);
    aux_command(0x64);
    aux_command(0xE8);
    aux_command(0x03);
    aux_command(0xF4);

    g_mouse_ready = 1;
    g_mouse_x = 320;
    g_mouse_y = 240;
    mouse_flush();
}

void mouse_poll(void)
{
    if (!g_mouse_ready)
        return;

    for (int guard = 0; guard < 64; guard++) {
        uint8_t st = inb(0x64);
        if (!(st & 0x01))
            break;
        if (!(st & 0x20))
            break;

        uint8_t b = inb(0x60);
        if (g_mouse_index == 0 && !(b & 0x08))
            continue;

        g_mouse_packet[g_mouse_index++] = b;
        if (g_mouse_index < 3)
            continue;
        g_mouse_index = 0;

        uint8_t flags = g_mouse_packet[0];
        if (flags & 0xC0)
            continue;

        int dx = g_mouse_packet[1];
        int dy = g_mouse_packet[2];
        if (flags & 0x10)
            dx -= 256;
        if (flags & 0x20)
            dy -= 256;

        g_mouse_x += dx;
        g_mouse_y -= dy;
        if (g_mouse_x < 0)
            g_mouse_x = 0;
        if (g_mouse_x > 639)
            g_mouse_x = 639;
        if (g_mouse_y < 0)
            g_mouse_y = 0;
        if (g_mouse_y > 479)
            g_mouse_y = 479;

        uint8_t buttons = (uint8_t)(flags & 0x07);
        if ((buttons & 1) && !(g_mouse_buttons & 1))
            g_click_flag = 1;
        if (!(buttons & 1) && (g_mouse_buttons & 1))
            g_release_flag = 1;
        g_mouse_buttons = buttons;
    }
}

void mouse_get_state(int *x, int *y, int *buttons)
{
    if (x)
        *x = g_mouse_x;
    if (y)
        *y = g_mouse_y;
    if (buttons)
        *buttons = g_mouse_buttons;
}

int mouse_take_click(void)
{
    int v = g_click_flag;
    g_click_flag = 0;
    return v;
}

int mouse_take_release(void)
{
    int v = g_release_flag;
    g_release_flag = 0;
    return v;
}


int vga_probe(void)
{
    uint8_t misc = inb(0x3CC);
    outb(0x3D4, 0x00);
    uint8_t htotal = inb(0x3D5);
    uint8_t status = inb(0x3DA);

    if (misc == 0xFF && htotal == 0xFF && status == 0xFF)
        return 0;
    if (htotal == 0xFF)
        return 0;
    return 1;
}

 
__attribute__((noreturn)) void reboot(void)
{
    while (inb(0x64) & 0x02)
        ;
    outb(0x64, 0xFE);

    for (volatile uint32_t i = 0; i < 1000000u; i++)
        __asm__ volatile("pause");

    outb(0xCF9, 0x02);
    outb(0xCF9, 0x06);

    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}
