#include "kernel.h"

/* Legacy ATA PIO driver.  Four IDE slots: primary master/slave,
   secondary master/slave.  LBA28 only, one sector per command. */

static ata_disk_t g_disks[ATA_MAX_DISKS];
static int g_ata_ready;

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

static inline void outw(uint16_t port, uint16_t value)
{
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint16_t inw(uint16_t port)
{
    uint16_t value;
    __asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void io_delay(uint16_t ctrl)
{
    (void)inb(ctrl);
    (void)inb(ctrl);
    (void)inb(ctrl);
    (void)inb(ctrl);
}

static int wait_not_busy(uint16_t iobase)
{
    for (uint32_t i = 0; i < 5000000u; i++) {
        uint8_t st = inb(iobase + 7);
        if (st == 0xFF)
            return -1;
        if (!(st & 0x80))
            return 0;
    }
    return -1;
}

static int wait_data(uint16_t iobase)
{
    for (uint32_t i = 0; i < 5000000u; i++) {
        uint8_t st = inb(iobase + 7);
        if (st == 0xFF)
            return -1;
        if (st & 0x01)
            return -1;
        if (!(st & 0x80) && (st & 0x08))
            return 0;
    }
    return -1;
}

static void identify_slot(int slot)
{
    static const uint16_t bases[2] = { 0x1F0, 0x170 };
    static const uint16_t ctrls[2] = { 0x3F6, 0x376 };

    ata_disk_t *d = &g_disks[slot];
    memset(d, 0, sizeof(*d));
    d->iobase = bases[slot / 2];
    d->ctrl = ctrls[slot / 2];
    d->slave = slot & 1;

    if (inb(d->iobase + 7) == 0xFF)
        return;

    outb(d->ctrl, 0);
    outb(d->iobase + 6, d->slave ? 0xB0 : 0xA0);
    io_delay(d->ctrl);
    outb(d->iobase + 2, 0);
    outb(d->iobase + 3, 0);
    outb(d->iobase + 4, 0);
    outb(d->iobase + 5, 0);
    outb(d->iobase + 7, 0xEC);

    uint8_t st = inb(d->iobase + 7);
    if (st == 0 || st == 0xFF)
        return;
    if (wait_not_busy(d->iobase) != 0)
        return;

    st = inb(d->iobase + 7);
    if ((st & 0x01) || !(st & 0x08))
        return;

    uint16_t id[256];
    for (int i = 0; i < 256; i++)
        id[i] = inw(d->iobase);

    if (!(id[49] & 0x0200))
        return;

    uint32_t sectors = (uint32_t)id[60] | ((uint32_t)id[61] << 16);
    if (sectors == 0)
        return;

    for (int i = 0; i < 40; i++) {
        d->model[i * 2] = (char)(id[27 + i] >> 8);
        d->model[i * 2 + 1] = (char)(id[27 + i] & 0xFF);
    }
    d->model[40] = 0;
    for (int i = 39; i >= 0 && (d->model[i] == ' ' || d->model[i] == 0); i--)
        d->model[i] = 0;

    d->sectors = sectors;
    d->present = 1;
}

int ata_init(void)
{
    if (g_ata_ready)
        return 0;
    for (int i = 0; i < ATA_MAX_DISKS; i++)
        identify_slot(i);
    g_ata_ready = 1;
    return 0;
}

ata_disk_t *ata_slot(int slot)
{
    if (slot < 0 || slot >= ATA_MAX_DISKS)
        return 0;
    if (!g_ata_ready)
        ata_init();
    return g_disks[slot].present ? &g_disks[slot] : 0;
}

int ata_bios_slot(int bios_drive)
{
    if (bios_drive < 0x80 || bios_drive > 0x83)
        return -1;
    return bios_drive - 0x80;
}

static int issue_lba(ata_disk_t *d, uint32_t lba)
{
    if (!d || !d->present)
        return -1;
    if (lba > 0x0FFFFFFFu)
        return -1;
    if (wait_not_busy(d->iobase) != 0)
        return -1;
    outb(d->ctrl, 0);
    outb(d->iobase + 6, (uint8_t)(0xE0 | (d->slave ? 0x10 : 0) |
                                  ((lba >> 24) & 0x0F)));
    io_delay(d->ctrl);
    outb(d->iobase + 2, 1);
    outb(d->iobase + 3, (uint8_t)(lba & 0xFF));
    outb(d->iobase + 4, (uint8_t)((lba >> 8) & 0xFF));
    outb(d->iobase + 5, (uint8_t)((lba >> 16) & 0xFF));
    return 0;
}

int ata_read_sectors(ata_disk_t *d, uint32_t lba, uint32_t count, void *buf)
{
    if (!d || !d->present || !buf || count == 0)
        return -1;
    if (lba + count > d->sectors)
        return -1;

    uint16_t *out = (uint16_t *)buf;
    for (uint32_t s = 0; s < count; s++) {
        if (issue_lba(d, lba + s) != 0)
            return -1;
        outb(d->iobase + 7, 0x20);
        if (wait_data(d->iobase) != 0)
            return -1;
        for (int i = 0; i < 256; i++)
            out[i] = inw(d->iobase);
        out += 256;
    }
    return 0;
}

int ata_write_sectors(ata_disk_t *d, uint32_t lba, uint32_t count,
                      const void *buf)
{
    if (!d || !d->present || !buf || count == 0)
        return -1;
    if (lba + count > d->sectors)
        return -1;

    const uint16_t *in = (const uint16_t *)buf;
    for (uint32_t s = 0; s < count; s++) {
        if (issue_lba(d, lba + s) != 0)
            return -1;
        outb(d->iobase + 7, 0x30);
        if (wait_data(d->iobase) != 0)
            return -1;
        for (int i = 0; i < 256; i++)
            outw(d->iobase, in[i]);
        in += 256;
        if (wait_not_busy(d->iobase) != 0)
            return -1;
    }
    return 0;
}

int ata_flush(ata_disk_t *d)
{
    if (!d || !d->present)
        return -1;
    if (wait_not_busy(d->iobase) != 0)
        return -1;
    outb(d->iobase + 7, 0xE7);
    return wait_not_busy(d->iobase);
}
