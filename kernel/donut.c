#include "kernel.h"
#include "sin_table.h"

#define DONUT_W 80
#define DONUT_H 22
#define TAU 6.283185307179586476925286766559
#define THETA_STEPS 90
#define PHI_STEPS 314

static double g_zbuf[DONUT_W * DONUT_H];
static uint16_t g_dbuf[DONUT_W * DONUT_H];
static double g_sin_theta[THETA_STEPS];
static double g_cos_theta[THETA_STEPS];
static double g_h_theta[THETA_STEPS];
static double g_sin_phi[PHI_STEPS];
static double g_cos_phi[PHI_STEPS];

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

static inline uint64_t rdtsc(void)
{
    uint32_t lo, hi;
    __asm__ volatile("lfence; rdtsc" : "=a"(lo), "=d"(hi) : : "memory");
    return ((uint64_t)hi << 32) | lo;
}

static inline double dsin(double x)
{
    double t = x * (1024.0 / TAU);
    int i = (int)t;
    double f = t - (double)i;
    i &= 1023;
    int j = (i + 1) & 1023;
    double a = (double)sin_table[i];
    double b = (double)sin_table[j];
    return (a + (b - a) * f) * (1.0 / 32767.0);
}

static inline double dcos(double x)
{
    return dsin(x + 1.57079632679489661923);
}

static void put_centered(int row, const char *s, uint8_t attr)
{
    int col = (VGA_WIDTH - (int)kstrlen(s)) / 2;
    if (col < 0)
        col = 0;
    vga_puts_at(row, col, s, attr);
}

static void cursor_hide(void)
{
    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x20);
}

static void cursor_show(void)
{
    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x0E);
    outb(0x3D4, 0x0B);
    outb(0x3D5, 0x0F);
}

static int any_key_pressed(void)
{
    int found = 0;
    int extended = 0;

    for (int guard = 0; guard < 64 && (inb(0x64) & 0x01); guard++) {
        uint8_t sc = inb(0x60);
        if (sc == 0xE0) {
            extended = 1;
            continue;
        }
        if (extended) {
            extended = 0;
            continue;
        }
        if (!(sc & 0x80))
            found = 1;
    }
    return found;
}

void donut_mode(void)
{
    static const char ramp[13] = ".,-~:;=!*#$@";
    static const uint8_t colors[12] = {
        DARK_GRAY, DARK_GRAY, LIGHT_GRAY, LIGHT_GRAY,
        CYAN, CYAN, LIGHT_CYAN, LIGHT_CYAN,
        YELLOW, YELLOW, WHITE, WHITE
    };

    kbd_drain();
    console_clear(VGA_ATTR(LIGHT_GRAY, BLACK));
    vga_fill_row(0, VGA_ATTR(LIGHT_GRAY, BLACK));
    vga_fill_row(1, VGA_ATTR(LIGHT_GRAY, BLACK));
    vga_fill_row(24, VGA_ATTR(LIGHT_GRAY, BLACK));
    put_centered(0, "ByteOS  ::  spinning donut  ::  v" BYTEOS_VERSION,
                 VGA_ATTR(YELLOW, BLACK));
    put_centered(24, "[ any key returns to the shell ]",
                 VGA_ATTR(LIGHT_GRAY, BLACK));
    cursor_hide();

    for (int n = 0; n < THETA_STEPS; n++) {
        double j = n * 0.07;
        g_sin_theta[n] = dsin(j);
        g_cos_theta[n] = dcos(j);
        g_h_theta[n] = g_cos_theta[n] + 2.0;
    }
    for (int n = 0; n < PHI_STEPS; n++) {
        double i = n * 0.02;
        g_sin_phi[n] = dsin(i);
        g_cos_phi[n] = dcos(i);
    }

    double A = 0.0;
    double B = 0.0;
    uint64_t frame_cycles = g_tsc_hz ? g_tsc_hz / 30ULL : 1000000000ULL / 30ULL;
    if (frame_cycles < 100000ULL)
        frame_cycles = 100000ULL;

    for (;;) {
        for (int k = 0; k < DONUT_W * DONUT_H; k++) {
            g_zbuf[k] = 0.0;
            g_dbuf[k] = (uint16_t)(((uint16_t)DARK_GRAY << 8) | ' ');
        }

        double sA = dsin(A);
        double cA = dcos(A);
        double sB = dsin(B);
        double cB = dcos(B);

        for (int jj = 0; jj < THETA_STEPS; jj++) {
            double sj = g_sin_theta[jj];
            double cj = g_cos_theta[jj];
            double h = g_h_theta[jj];

            for (int ii = 0; ii < PHI_STEPS; ii++) {
                double ci = g_sin_phi[ii];
                double cosi = g_cos_phi[ii];

                double D = 1.0 / (ci * h * sA + sj * cA + 5.0);
                double t = ci * h * cA - sj * sA;
                int x = 40 + (int)(30.0 * D * (cosi * h * cB - t * sB));
                int y = 12 + (int)(15.0 * D * (cosi * h * sB + t * cB));

                if (x > 0 && x < DONUT_W && y > 0 && y < DONUT_H) {
                    int o = x + DONUT_W * y;
                    int N = (int)(8.0 * ((sj * sA - ci * cj * cA) * cB -
                                         ci * cj * sA - sj * cA - cosi * cj * sB));
                    if (D > g_zbuf[o]) {
                        if (N < 0)
                            N = 0;
                        if (N > 11)
                            N = 11;
                        g_zbuf[o] = D;
                        g_dbuf[o] = (uint16_t)(((uint16_t)colors[N] << 8) |
                                               (unsigned char)ramp[N]);
                    }
                }
            }
        }

        for (int y = 0; y < DONUT_H; y++) {
            for (int x = 0; x < DONUT_W; x++) {
                VGA_MEMORY[(y + 2) * VGA_WIDTH + x] =
                    g_dbuf[y * DONUT_W + x];
            }
        }

        A += 0.04;
        B += 0.02;

        uint64_t deadline = rdtsc() + frame_cycles;
        int leave = 0;
        for (;;) {
            if (any_key_pressed()) {
                leave = 1;
                break;
            }
            if ((int64_t)(rdtsc() - deadline) >= 0)
                break;
        }
        if (leave)
            break;
    }

    kbd_drain();
    cursor_show();
    console_clear(VGA_ATTR(LIGHT_GRAY, BLACK));
    home_screen();
}
