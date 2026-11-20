#include "kernel.h"

#define ATA_PRIMARY_IO    0x1F0
#define ATA_PRIMARY_CTRL  0x3F6
#define ATA_SECONDARY_IO  0x170
#define ATA_SECONDARY_CTRL 0x376

#define AHCI_PCI_CLASS 0x010601u
#define AHCI_CAP       0x00
#define AHCI_GHC       0x04
#define AHCI_PI        0x0C
#define AHCI_PxCLB     0x00
#define AHCI_PxCLBU    0x04
#define AHCI_PxFB      0x08
#define AHCI_PxFBU     0x0C
#define AHCI_PxIS      0x10
#define AHCI_PxIE      0x14
#define AHCI_PxCMD     0x18
#define AHCI_PxTFD     0x20
#define AHCI_PxSIG     0x24
#define AHCI_PxSSTS    0x28
#define AHCI_PxSCTL    0x2C
#define AHCI_PxSERR    0x30
#define AHCI_PxSACT    0x34
#define AHCI_PxCI      0x38

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

static inline void outl(uint16_t port, uint32_t value)
{
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint32_t inl(uint16_t port)
{
    uint32_t value;
    __asm__ volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void io_delay(uint16_t ctrl)
{
    (void)inb(ctrl);
    (void)inb(ctrl);
    (void)inb(ctrl);
    (void)inb(ctrl);
}

static void delay_us(uint32_t us)
{
    if (g_tsc_hz) {
        uint64_t end = read_tsc() + g_tsc_hz * us / 1000000ULL;
        while (read_tsc() < end)
            __asm__ volatile("pause");
        return;
    }
    for (volatile uint32_t i = 0; i < us * 100u; i++)
        ;
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

static int pio_reset_channel(uint16_t ctrl)
{
    outb(ctrl, 0x04);
    io_delay(ctrl);
    io_delay(ctrl);
    outb(ctrl, 0x02);
    for (uint32_t i = 0; i < 5000000u; i++) {
        uint8_t st = inb(ctrl);
        if (st == 0xFF)
            return -1;
        if (!(st & 0x80))
            return 0;
    }
    return -1;
}

static void identify_slot(int slot)
{
    static const uint16_t bases[2] = { ATA_PRIMARY_IO, ATA_SECONDARY_IO };
    static const uint16_t ctrls[2] = { ATA_PRIMARY_CTRL, ATA_SECONDARY_CTRL };

    ata_disk_t *d = &g_disks[slot];
    memset(d, 0, sizeof(*d));
    d->kind = ATA_KIND_PIO;
    d->iobase = bases[slot / 2];
    d->ctrl = ctrls[slot / 2];
    d->slave = slot & 1;

    if (inb(d->iobase + 7) == 0xFF)
        return;

    outb(d->ctrl, 0x02);
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

    if (id[0] == 0 || id[0] == 0xFFFF || id[1] == 0 || id[1] == 0xFFFF)
        return;
    if (!(id[49] & 0x0200))
        return;

    uint64_t sectors = (uint64_t)id[60] | ((uint64_t)id[61] << 16);
    uint64_t lba48 = (uint64_t)id[100] | ((uint64_t)id[101] << 16) |
                     ((uint64_t)id[102] << 32) | ((uint64_t)id[103] << 48);
    if (lba48 > sectors)
        sectors = lba48;
    if (sectors == 0)
        return;
    if (sectors > 0xFFFFFFFFULL)
        sectors = 0xFFFFFFFFULL;

    for (int i = 0; i < 20; i++) {
        d->model[i * 2] = (char)(id[27 + i] >> 8);
        d->model[i * 2 + 1] = (char)(id[27 + i] & 0xFF);
    }
    d->model[40] = 0;
    for (int i = 39; i >= 0 && (d->model[i] == ' ' || d->model[i] == 0); i--)
        d->model[i] = 0;

    int named = 0;
    for (int i = 0; i < 40 && d->model[i]; i++) {
        if (d->model[i] > ' ' && d->model[i] < 127)
            named = 1;
    }
    if (!named) {
        memset(d, 0, sizeof(*d));
        return;
    }

    d->sectors = (uint32_t)sectors;
    d->present = 1;
}

static uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off){
    uint32_t addr = 0x80000000u | ((uint32_t)bus << 16) |
                    ((uint32_t)dev << 11) | ((uint32_t)fn << 8) |
                    (off & 0xFCu);
    outl(0xCF8, addr);
    return inl(0xCFC);
}

static void pci_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off,
                        uint32_t value)
{
    uint32_t addr = 0x80000000u | ((uint32_t)bus << 16) |
                    ((uint32_t)dev << 11) | ((uint32_t)fn << 8) |
                    (off & 0xFCu);
    outl(0xCF8, addr);
    outl(0xCFC, value);
}

static uint8_t g_mmio_pd[4][4096] __attribute__((aligned(4096)));
static int g_mmio_pd_used;

static void log_hex(uint64_t value)
{
    static const char digits[] = "0123456789abcdef";
    char buf[19];

    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 16; i++)
        buf[2 + i] = digits[(value >> ((15 - i) * 4)) & 0x0F];
    buf[18] = 0;
    serial_puts(buf);
}

static void log_num(const char *label, uint64_t value)
{
    serial_puts(label);
    log_hex(value);
    serial_putc('\n');
}

static int map_mmio(uintptr_t addr)
{
    uint64_t *pdpt = (uint64_t *)0x2000;
    uint32_t idx = (uint32_t)(addr >> 30);

    if (idx == 0 || (pdpt[idx] & 1))
        return idx < 512 ? 0 : -1;
    if (idx >= 512 || g_mmio_pd_used >= 4)
        return -1;

    uint64_t *pd = (uint64_t *)(void *)g_mmio_pd[g_mmio_pd_used++];
    for (int i = 0; i < 512; i++)
        pd[i] = ((uint64_t)idx << 30) + ((uint64_t)i << 21) | 0x83ULL;
    pdpt[idx] = (uint64_t)(uintptr_t)pd | 3ULL;
    __asm__ volatile("mov %%cr3, %%rax; mov %%rax, %%cr3" ::: "rax", "memory");
    return 0;
}

static uint8_t g_ahci_cl[1024] __attribute__((aligned(1024)));
static uint8_t g_ahci_fis[256] __attribute__((aligned(256)));
static uint8_t g_ahci_ct[256] __attribute__((aligned(128)));
static uint8_t g_ahci_id[512] __attribute__((aligned(16)));
static uint8_t g_ahci_buf[512] __attribute__((aligned(16)));

static uint32_t ahci_rd(volatile uint32_t *abar, int port, uint32_t off)
{
    return abar[(0x100u + (uint32_t)port * 0x80u + off) / 4u];
}

static void ahci_wr(volatile uint32_t *abar, int port, uint32_t off,
                    uint32_t value)
{
    abar[(0x100u + (uint32_t)port * 0x80u + off) / 4u] = value;
}

static int ahci_wait_cmd(volatile uint32_t *abar, int port, uint32_t mask,
                         uint32_t want, uint32_t spins)
{
    for (uint32_t i = 0; i < spins; i++) {
        if ((ahci_rd(abar, port, AHCI_PxCMD) & mask) == want)
            return 0;
        __asm__ volatile("pause");
    }
    return -1;
}

static int ahci_port_reset(volatile uint32_t *abar, int port)
{
    uint32_t sctl = ahci_rd(abar, port, AHCI_PxSCTL);
    ahci_wr(abar, port, AHCI_PxSCTL, (sctl & ~0x0Fu) | 0x01u);
    delay_us(1000);
    ahci_wr(abar, port, AHCI_PxSCTL, sctl & ~0x0Fu);

    for (int i = 0; i < 500; i++) {
        uint32_t ssts = ahci_rd(abar, port, AHCI_PxSSTS);
        if ((ssts & 0x0Fu) == 3u && ((ssts >> 8) & 0x0Fu) == 1u)
            return 0;
        delay_us(1000);
    }
    return -1;
}

static int ahci_port_start(volatile uint32_t *abar, int port)
{
    uint32_t cmd = ahci_rd(abar, port, AHCI_PxCMD);

    if (cmd & 0x01u) {
        cmd &= ~0x01u;
        ahci_wr(abar, port, AHCI_PxCMD, cmd);
        if (ahci_wait_cmd(abar, port, 1u << 15, 0, 5000000u) != 0)
            return -1;
    }
    cmd = ahci_rd(abar, port, AHCI_PxCMD);
    if (cmd & (1u << 4)) {
        cmd &= ~(1u << 4);
        ahci_wr(abar, port, AHCI_PxCMD, cmd);
        if (ahci_wait_cmd(abar, port, 1u << 14, 0, 5000000u) != 0)
            return -1;
    }

    ahci_wr(abar, port, AHCI_PxCLB, (uint32_t)(uintptr_t)g_ahci_cl);
    ahci_wr(abar, port, AHCI_PxCLBU,
            (uint32_t)((uint64_t)(uintptr_t)g_ahci_cl >> 32));
    ahci_wr(abar, port, AHCI_PxFB, (uint32_t)(uintptr_t)g_ahci_fis);
    ahci_wr(abar, port, AHCI_PxFBU,
            (uint32_t)((uint64_t)(uintptr_t)g_ahci_fis >> 32));
    ahci_wr(abar, port, AHCI_PxIE, 0);
    ahci_wr(abar, port, AHCI_PxIS, 0xFFFFFFFFu);
    ahci_wr(abar, port, AHCI_PxSERR, 0xFFFFFFFFu);
    ahci_wr(abar, port, AHCI_PxSACT, 0);

    cmd = ahci_rd(abar, port, AHCI_PxCMD);
    cmd |= (1u << 1) | (1u << 2);
    ahci_wr(abar, port, AHCI_PxCMD, cmd);

    cmd = ahci_rd(abar, port, AHCI_PxCMD);
    cmd |= (1u << 4) | 0x01u;
    ahci_wr(abar, port, AHCI_PxCMD, cmd);

    return ahci_wait_cmd(abar, port, (1u << 14) | (1u << 15),
                         (1u << 14) | (1u << 15), 5000000u);
}

static int ahci_command(volatile uint32_t *abar, int port, uint8_t command,
                        uint32_t lba, uint16_t count, void *buf,
                        uint32_t bytes, int write)
{
    for (uint32_t i = 0; i < 5000000u; i++) {
        if (!(ahci_rd(abar, port, AHCI_PxCI) & 1u))
            break;
        if (i == 4999999u)
            return -1;
    }

    memset(g_ahci_ct, 0, sizeof(g_ahci_ct));
    uint8_t *fis = g_ahci_ct;
    fis[0] = 0x27;
    fis[1] = 0x80;
    fis[2] = command;
    fis[3] = 0;
    fis[4] = (uint8_t)(lba & 0xFF);
    fis[5] = (uint8_t)((lba >> 8) & 0xFF);
    fis[6] = (uint8_t)((lba >> 16) & 0xFF);
    fis[7] = 0x40;
    fis[8] = (uint8_t)((lba >> 24) & 0xFF);
    fis[9] = 0;
    fis[10] = 0;
    fis[11] = 0;
    fis[12] = (uint8_t)(count & 0xFF);
    fis[13] = (uint8_t)((count >> 8) & 0xFF);
    fis[14] = 0;
    fis[15] = 0;

    uint32_t *hdr = (uint32_t *)(void *)g_ahci_cl;
    hdr[0] = 5u | (write ? (1u << 6) : 0);
    hdr[1] = 0;
    hdr[2] = (uint32_t)(uintptr_t)g_ahci_ct;
    hdr[3] = (uint32_t)((uint64_t)(uintptr_t)g_ahci_ct >> 32);
    hdr[4] = 0;
    hdr[5] = 0;
    hdr[6] = 0;
    hdr[7] = 0;

    if (buf && bytes) {
        uintptr_t phys = (uintptr_t)buf;
        uint32_t *prdt = (uint32_t *)(void *)(g_ahci_ct + 0x80);
        prdt[0] = (uint32_t)phys;
        prdt[1] = (uint32_t)((uint64_t)phys >> 32);
        prdt[2] = 0;
        prdt[3] = (bytes - 1u) | (1u << 31);
        hdr[0] |= 1u << 16;
    }

    ahci_wr(abar, port, AHCI_PxIS, 0xFFFFFFFFu);
    ahci_wr(abar, port, AHCI_PxSERR, 0xFFFFFFFFu);
    ahci_wr(abar, port, AHCI_PxCI, 1u);

    for (uint32_t i = 0; i < 20000000u; i++) {
        if (!(ahci_rd(abar, port, AHCI_PxCI) & 1u)) {
            uint32_t tfd = ahci_rd(abar, port, AHCI_PxTFD);
            uint32_t is = ahci_rd(abar, port, AHCI_PxIS);
            if ((tfd & 0x01u) || (is & (1u << 30)))
                return -1;
            return 0;
        }
        __asm__ volatile("pause");
    }
    return -1;
}

static int ahci_identify(volatile uint32_t *abar, int port, ata_disk_t *d)
{
    int rc = ahci_command(abar, port, 0xEC, 0, 1, g_ahci_id, 512, 0);
    if (rc != 0) {
        log_num("ahci: tfd ", ahci_rd(abar, port, AHCI_PxTFD));
        log_num("ahci: is ", ahci_rd(abar, port, AHCI_PxIS));
        return -1;
    }

    uint16_t *id = (uint16_t *)(void *)g_ahci_id;
    if (id[0] == 0 || id[0] == 0xFFFF || id[1] == 0 || id[1] == 0xFFFF)
        return -1;
    if (!(id[49] & 0x0200))
        return -1;

    uint64_t sectors = (uint64_t)id[60] | ((uint64_t)id[61] << 16);
    uint64_t lba48 = (uint64_t)id[100] | ((uint64_t)id[101] << 16) |
                     ((uint64_t)id[102] << 32) | ((uint64_t)id[103] << 48);
    if (lba48 > sectors)
        sectors = lba48;
    if (sectors == 0)
        return -1;
    if (sectors > 0xFFFFFFFFULL)
        sectors = 0xFFFFFFFFULL;

    for (int i = 0; i < 20; i++) {
        d->model[i * 2] = (char)(id[27 + i] >> 8);
        d->model[i * 2 + 1] = (char)(id[27 + i] & 0xFF);
    }
    d->model[40] = 0;
    for (int i = 39; i >= 0 && (d->model[i] == ' ' || d->model[i] == 0); i--)
        d->model[i] = 0;

    d->sectors = (uint32_t)sectors;
    return 0;
}

static void ahci_add_disk(volatile uint32_t *abar, int port)
{
    for (int i = 4; i < ATA_MAX_DISKS; i++) {
        ata_disk_t *d = &g_disks[i];
        if (d->present)
            continue;
        memset(d, 0, sizeof(*d));
        d->kind = ATA_KIND_AHCI;
        d->abar = abar;
        d->port = port;
        if (ahci_identify(abar, port, d) == 0) {
            d->present = 1;
            return;
        }
        memset(d, 0, sizeof(*d));
        return;
    }
}

static void ahci_probe(uintptr_t addr)
{
    volatile uint32_t *abar = (volatile uint32_t *)addr;

    serial_puts("ahci: controller\n");
    log_num("ahci: bar ", addr);
    if (map_mmio(addr) != 0)
        return;
    log_num("ahci: cap ", abar[AHCI_CAP / 4]);
    if (abar[AHCI_CAP / 4] == 0xFFFFFFFFu)
        return;

    abar[AHCI_GHC / 4] |= 1u << 31;

    uint32_t ports = abar[AHCI_PI / 4];
    log_num("ahci: pi ", ports);
    for (int port = 0; port < 32; port++) {
        if (!(ports & (1u << port)))
            continue;

        uint32_t ssts = ahci_rd(abar, port, AHCI_PxSSTS);
        if ((ssts & 0x0Fu) != 3u || ((ssts >> 8) & 0x0Fu) != 1u) {
            if (ahci_port_reset(abar, port) != 0)
                continue;
            ssts = ahci_rd(abar, port, AHCI_PxSSTS);
            if ((ssts & 0x0Fu) != 3u || ((ssts >> 8) & 0x0Fu) != 1u)
                continue;
        }

        uint32_t sig = ahci_rd(abar, port, AHCI_PxSIG);
        if (sig != 0x00000101u)
            continue;
        log_num("ahci: disk port ", (uint32_t)port);
        if (ahci_port_start(abar, port) != 0) {
            serial_puts("ahci: port start failed\n");
            continue;
        }
        ahci_add_disk(abar, port);
    }
}

static void ahci_init(void)
{
    for (int bus = 0; bus < 256; bus++) {
        for (int dev = 0; dev < 32; dev++) {
            for (int fn = 0; fn < 8; fn++) {
                uint32_t vendor = pci_read32((uint8_t)bus, (uint8_t)dev,
                                             (uint8_t)fn, 0x00);
                if (vendor == 0xFFFFFFFFu || vendor == 0)
                    continue;
                uint32_t cls = pci_read32((uint8_t)bus, (uint8_t)dev,
                                          (uint8_t)fn, 0x08);
                if ((cls >> 8) != AHCI_PCI_CLASS)
                    continue;

                serial_puts("ahci: pci match\n");
                log_num("ahci: class ", cls);

                uint32_t cmd = pci_read32((uint8_t)bus, (uint8_t)dev,
                                          (uint8_t)fn, 0x04);
                pci_write32((uint8_t)bus, (uint8_t)dev, (uint8_t)fn, 0x04,
                            cmd | 0x06u);

                uint32_t bar = pci_read32((uint8_t)bus, (uint8_t)dev,
                                          (uint8_t)fn, 0x24);
                if (bar & 0x01u)
                    continue;
                uintptr_t addr = (uintptr_t)(bar & ~0x0Fu);
                if ((bar & 0x06u) == 0x04u) {
                    uint32_t hi = pci_read32((uint8_t)bus, (uint8_t)dev,
                                             (uint8_t)fn, 0x28);
                    addr |= (uintptr_t)hi << 32;
                }
                if (!addr)
                    continue;
                ahci_probe(addr);
            }
        }
    }
}

static int ahci_rw(ata_disk_t *d, uint32_t lba, uint32_t count, void *buf,
                   int write)
{
    uint8_t *p = (uint8_t *)buf;

    for (uint32_t i = 0; i < count; i++) {
        if (write)
            memcpy(g_ahci_buf, p + (size_t)i * 512, 512);
        if (ahci_command(d->abar, d->port, write ? 0x35 : 0x25, lba + i, 1,
                         g_ahci_buf, 512, write) != 0)
            return -1;
        if (!write)
            memcpy(p + (size_t)i * 512, g_ahci_buf, 512);
    }
    return 0;
}

int ata_init(void)
{
    if (g_ata_ready)
        return 0;

    if (inb(ATA_PRIMARY_IO + 7) != 0xFF)
        (void)pio_reset_channel(ATA_PRIMARY_CTRL);
    if (inb(ATA_SECONDARY_IO + 7) != 0xFF)
        (void)pio_reset_channel(ATA_SECONDARY_CTRL);

    for (int i = 0; i < 4; i++)
        identify_slot(i);

    ahci_init();

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
    if (bios_drive < 0x80)
        return -1;
    if (!g_ata_ready)
        ata_init();

    int want = bios_drive - 0x80;
    for (int i = 0; i < ATA_MAX_DISKS; i++) {
        if (!g_disks[i].present)
            continue;
        if (want == 0)
            return i;
        want--;
    }
    return -1;
}

static int pio_issue(ata_disk_t *d, uint32_t lba, uint8_t count, int write)
{
    if (wait_not_busy(d->iobase) != 0)
        return -1;

    int lba48 = lba >= 0x10000000u;
    uint8_t dev = 0x40u | (d->slave ? 0x10u : 0);
    if (!lba48)
        dev |= 0xE0u | (uint8_t)((lba >> 24) & 0x0Fu);

    outb(d->ctrl, 0x02);
    outb(d->iobase + 6, dev);
    io_delay(d->ctrl);

    if (lba48) {
        outb(d->iobase + 2, 0);
        outb(d->iobase + 3, (uint8_t)((lba >> 24) & 0xFF));
        outb(d->iobase + 4, 0);
        outb(d->iobase + 5, 0);
    }
    outb(d->iobase + 2, count);
    outb(d->iobase + 3, (uint8_t)(lba & 0xFF));
    outb(d->iobase + 4, (uint8_t)((lba >> 8) & 0xFF));
    outb(d->iobase + 5, (uint8_t)((lba >> 16) & 0xFF));

    uint8_t cmd;
    if (write)
        cmd = lba48 ? 0x34 : 0x30;
    else
        cmd = lba48 ? 0x24 : 0x20;
    outb(d->iobase + 7, cmd);
    return 0;
}

static int pio_read_sector(ata_disk_t *d, uint32_t lba, void *buf)
{
    for (int tries = 0; tries < 2; tries++) {
        if (pio_issue(d, lba, 1, 0) == 0 && wait_data(d->iobase) == 0) {
            uint16_t *out = (uint16_t *)buf;
            for (int i = 0; i < 256; i++)
                out[i] = inw(d->iobase);
            return 0;
        }
        (void)pio_reset_channel(d->ctrl);
    }
    return -1;
}

static int pio_write_sector(ata_disk_t *d, uint32_t lba, const void *buf)
{
    for (int tries = 0; tries < 2; tries++) {
        if (pio_issue(d, lba, 1, 1) == 0 && wait_data(d->iobase) == 0) {
            const uint16_t *in = (const uint16_t *)buf;
            for (int i = 0; i < 256; i++)
                outw(d->iobase, in[i]);
            if (wait_not_busy(d->iobase) == 0)
                return 0;
        }
        (void)pio_reset_channel(d->ctrl);
    }
    return -1;
}

int ata_read_sectors(ata_disk_t *d, uint32_t lba, uint32_t count, void *buf)
{
    if (!d || !d->present || !buf || count == 0)
        return -1;
    if ((uint64_t)lba + count > d->sectors)
        return -1;
    if (d->kind == ATA_KIND_AHCI)
        return ahci_rw(d, lba, count, buf, 0);

    uint8_t *p = (uint8_t *)buf;
    for (uint32_t i = 0; i < count; i++) {
        if (pio_read_sector(d, lba + i, p + (size_t)i * 512) != 0)
            return -1;
    }
    return 0;
}

int ata_write_sectors(ata_disk_t *d, uint32_t lba, uint32_t count,
                      const void *buf)
{
    if (!d || !d->present || !buf || count == 0)
        return -1;
    if ((uint64_t)lba + count > d->sectors)
        return -1;
    if (d->kind == ATA_KIND_AHCI)
        return ahci_rw(d, lba, count, (void *)buf, 1);

    const uint8_t *p = (const uint8_t *)buf;
    for (uint32_t i = 0; i < count; i++) {
        if (pio_write_sector(d, lba + i, p + (size_t)i * 512) != 0)
            return -1;
    }
    return 0;
}

int ata_flush(ata_disk_t *d)
{
    if (!d || !d->present)
        return -1;
    if (d->kind == ATA_KIND_AHCI)
        return ahci_command(d->abar, d->port, 0xEA, 0, 0, 0, 0, 0);
    if (wait_not_busy(d->iobase) != 0)
        return -1;
    outb(d->iobase + 7, 0xE7);
    return wait_not_busy(d->iobase);
}
