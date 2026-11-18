#ifndef BYTEOS_KERNEL_H
#define BYTEOS_KERNEL_H

#include <stdint.h>
#include <stddef.h>

#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define VGA_MEMORY ((volatile uint16_t *)0xB8000)

#define BOOT_INFO_ADDR 0x5000
#define BOOT_MAGIC     0x42595445u   /* "BYTE" */

#define BYTEOS_VERSION "0.2"
#define BYTEOS_CODENAME "Sprinkles"

#define VGA_ATTR(fg, bg) ((uint8_t)((fg) | ((bg) << 4)))

enum vga_color {
    BLACK = 0,
    BLUE,
    GREEN,
    CYAN,
    RED,
    MAGENTA,
    BROWN,
    LIGHT_GRAY,
    DARK_GRAY,
    LIGHT_BLUE,
    LIGHT_GREEN,
    LIGHT_CYAN,
    LIGHT_RED,
    LIGHT_MAGENTA,
    YELLOW,
    WHITE
};

typedef struct __attribute__((packed)) {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t acpi;
} e820_entry_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t e820_count;
    e820_entry_t entries[128];
    uint32_t boot_drive;
    uint32_t image_bytes;
    uint32_t boot_flags;
    uint32_t reserved;
} boot_info_t;

#define BOOT_FLAG_DISK 1u

typedef struct {
    uint32_t f1_edx;
    uint32_t f1_ecx;
    uint32_t f7_ebx;
    uint32_t f7_ecx;
    uint32_t ext_edx;
} cpu_features_t;

typedef struct {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
} rtc_time_t;

/* core.c */
extern boot_info_t *g_boot;
extern int g_boot_ok;
extern uint32_t g_e820_count;
extern uint64_t g_mem_usable;
extern uint64_t g_mem_top;
extern char g_cpu_vendor[13];
extern char g_cpu_brand[49];
extern uint32_t g_cpu_family;
extern uint32_t g_cpu_model;
extern uint32_t g_cpu_stepping;
extern uint32_t g_logical_cpus;
extern int g_hypervisor;
extern cpu_features_t g_features;
extern uint32_t g_max_leaf;
extern uint32_t g_max_ext;
extern uint64_t g_tsc_hz;
extern uint64_t g_boot_tsc;

#define F1_EDX(bit) ((g_features.f1_edx >> (bit)) & 1u)
#define F1_ECX(bit) ((g_features.f1_ecx >> (bit)) & 1u)
#define F7_EBX(bit) ((g_features.f7_ebx >> (bit)) & 1u)
#define F7_ECX(bit) ((g_features.f7_ecx >> (bit)) & 1u)
#define FEXT_EDX(bit) ((g_features.ext_edx >> (bit)) & 1u)

void *memset(void *dst, int c, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
int memcmp(const void *a, const void *b, size_t n);

void console_init(uint8_t attr);
void console_clear(uint8_t attr);
void console_clear_region(uint8_t attr);
void console_set_region(int top, int bottom);
void console_get_region(int *top, int *bottom);
void console_get_cursor(int *row, int *col);
void console_set_attr(uint8_t attr);
void console_putc(char c);
void console_puts(const char *s);
void console_goto(int row, int col);
void vga_fill_row(int row, uint8_t attr);
void vga_put_at(int row, int col, char c, uint8_t attr);
void vga_puts_at(int row, int col, const char *s, uint8_t attr);
void kprintf(const char *fmt, ...);
void print_size(uint64_t bytes);
size_t kstrlen(const char *s);
int str_eq(const char *a, const char *b);
int str_eq_ci(const char *a, const char *b);
void str_copy(char *dst, const char *src, size_t cap);
int rtc_read(rtc_time_t *out);
uint64_t calibrate_tsc(void);
uint64_t read_tsc(void);
void detect_cpu(void);
void scan_memory(void);
int kbd_getchar(void);
int kbd_getkey(void);
int kbd_waitkey(void);
void kbd_drain(void);
void reboot(void);

#define KEY_UP    0x100
#define KEY_DOWN  0x101
#define KEY_LEFT  0x102
#define KEY_RIGHT 0x103
#define KEY_PGUP  0x104
#define KEY_PGDN  0x105
#define KEY_HOME  0x106
#define KEY_END   0x107
#define KEY_DEL   0x108
#define KEY_F1    0x110
#define KEY_F2    0x111
#define KEY_F3    0x112
#define KEY_F4    0x113
#define KEY_F5    0x114
#define KEY_F6    0x115
#define KEY_F7    0x116
#define KEY_F8    0x117
#define KEY_F9    0x118
#define KEY_F10   0x119
#define KEY_F11   0x11A
#define KEY_F12   0x11B

/* ui.c */
void home_screen(void);
void help_screen(void);
void specs_screen(void);
void about_screen(void);
void memmap_screen(void);
void shell(void);

/* donut.c */
void donut_mode(void);

/* ata.c */
#define ATA_MAX_DISKS 4

typedef struct {
    int present;
    uint16_t iobase;
    uint16_t ctrl;
    int slave;
    uint32_t sectors;
    char model[41];
} ata_disk_t;

int ata_init(void);
ata_disk_t *ata_slot(int slot);
int ata_bios_slot(int bios_drive);
int ata_read_sectors(ata_disk_t *d, uint32_t lba, uint32_t count, void *buf);
int ata_write_sectors(ata_disk_t *d, uint32_t lba, uint32_t count,
                      const void *buf);
int ata_flush(ata_disk_t *d);

/* install.c */
void install_wizard(void);

/* fetch.c */
void fetch_screen(void);

/* desktop.c */
void desktop(void);
int desktop_active(void);
void app_screen(const char *title, const char *hint);
void app_footer(const char *text);

/* browser.c */
void browser_app(const char *path);

/* editor.c */
void editor_app(const char *path);

/* auth.c */
int auth_load(void);
int auth_configured(void);
const char *auth_username(void);
const char *auth_hostname(void);
int auth_is_root(void);
int auth_login(void);
int auth_su(void);
int auth_passwd(void);
int auth_set_credentials(const char *user, const char *password,
                         const char *root_password, const char *host);

/* ui.c */
int console_read_line(char *buf, size_t cap, int mask);
void shell_set_exit_mode(int allow);

/* bfs.c */
#define BFS_PART_LBA 2048
#define BFS_MAX_FILES 32
#define BFS_NAME_LEN 40

typedef struct __attribute__((packed)) {
    char name[BFS_NAME_LEN];
    uint32_t start_lba;
    uint32_t size;
    uint8_t flags;
    uint8_t reserved[3];
} bfs_file_t;

#define BFS_FILE_USED 1
#define BFS_FILE_HTML 2

int bfs_mount(int slot, uint32_t part_lba);
int bfs_format(int slot, uint32_t part_lba, uint32_t end_lba);
int bfs_mounted(void);
int bfs_slot(void);
int bfs_file_count(void);
bfs_file_t *bfs_file_at(int index);
int bfs_lookup(const char *path);
int bfs_read(const char *path, void *buf, uint32_t cap, uint32_t *out_len);
int bfs_add(const char *path, const void *data, uint32_t len, int is_html);
int bfs_write(const char *path, const void *data, uint32_t len);
uint32_t bfs_free_bytes(void);
int bfs_is_html(const char *path);

#endif
