#include "fat12.h"
#include <stdint.h>

#define SPT       18
#define HEADS      2
#define SECTOR_SZ 512

#define FAT1_LBA   5
#define FAT_SECS   9
#define FAT2_LBA  (FAT1_LBA + FAT_SECS)
#define ROOT_LBA  (FAT2_LBA + FAT_SECS) /* = 23 */
#define ROOT_SECS 14
#define DATA_LBA  (ROOT_LBA + ROOT_SECS) /* = 37 */
#define SECS_PER_CLUS 1
#define ROOT_ENTRIES  224

#define BOUNCE_PHYS 0x0800u
#define BOUNCE_PTR  ((uint8_t *)BOUNCE_PHYS)

static uint8_t fat_buf [FAT_SECS * SECTOR_SZ];
static uint8_t root_buf[ROOT_SECS * SECTOR_SZ];
static uint8_t io_buf  [SECTOR_SZ];

static uint8_t g_drive;
static uint8_t g_use_edd = 0;
static int fat_loaded;

extern uint8_t bios_disk_thunk(uint8_t drive, uint8_t head, uint8_t sector,
                               uint8_t cyl, uint16_t count, uint32_t buf_phys,
                               uint8_t use_edd, uint32_t lba_lo);

static void lba_to_chs(uint32_t lba, uint8_t *cyl, uint8_t *head, uint8_t *sec)
{
    *sec = (uint8_t)(lba % SPT) + 1;
    *head = (uint8_t)((lba / SPT) % HEADS);
    *cyl = (uint8_t)(lba / SPT / HEADS);
}

/* NO RETRIES. The BIOS already does 3 retries internally. Calling reset manually breaks QEMU's floppy emulator. */
static int disk_read(uint32_t lba, uint16_t count, uint32_t buf_phys)
{
    while (count) {
        uint8_t cyl = 0, head = 0, sec = 0;
        if (!g_use_edd) lba_to_chs(lba, &cyl, &head, &sec);

        uint8_t st = bios_disk_thunk(g_drive, head, sec, cyl,
                                     1, BOUNCE_PHYS, g_use_edd ? 1 : 0, lba);
        if (st) return (int)st;

        uint8_t *dst = (uint8_t *)(uintptr_t)buf_phys;
        uint8_t *src = BOUNCE_PTR;
        for (int i = 0; i < SECTOR_SZ; i++) dst[i] = src[i];

        lba++; count--; buf_phys += SECTOR_SZ;
    }
    return 0;
}

static int disk_write(uint32_t lba, uint16_t count, uint32_t buf_phys)
{
    while (count) {
        uint8_t cyl = 0, head = 0, sec = 0;
        if (!g_use_edd) lba_to_chs(lba, &cyl, &head, &sec);

        uint8_t *src = (uint8_t *)(uintptr_t)buf_phys;
        uint8_t *dst = BOUNCE_PTR;
        for (int i = 0; i < SECTOR_SZ; i++) dst[i] = src[i];

        uint8_t st = bios_disk_thunk(g_drive, head, sec, cyl,
                                     1, BOUNCE_PHYS, g_use_edd ? 3 : 2, lba);
        if (st) return (int)st;

        lba++; count--; buf_phys += SECTOR_SZ;
    }
    return 0;
}

static uint16_t fat12_get(uint16_t clus)
{
    uint32_t off = (uint32_t)clus + clus / 2;
    uint16_t v = *(uint16_t *)(fat_buf + off);
    return (clus & 1) ? (v >> 4) : (v & 0x0FFF);
}

static void fat12_set(uint16_t clus, uint16_t val)
{
    uint32_t off = (uint32_t)clus + clus / 2;
    uint16_t *p = (uint16_t *)(fat_buf + off);
    if (clus & 1)
        *p = (*p & 0x000F) | (uint16_t)(val << 4);
    else
        *p = (*p & 0xF000) | (val & 0x0FFF);
}

static uint32_t clus_to_lba(uint16_t clus)
{
    return DATA_LBA + (uint32_t)(clus - 2) * SECS_PER_CLUS;
}

#define TOTAL_CLUS ((uint16_t)((2880 - DATA_LBA) / SECS_PER_CLUS + 2))

static uint16_t fat12_alloc(void)
{
    for (uint16_t c = 2; c < TOTAL_CLUS; c++)
        if (!fat12_get(c)) return c;
    return 0;
}

static int fat_flush(void)
{
    uint32_t phys = (uint32_t)(uintptr_t)fat_buf;
    int r = 0;
    r |= disk_write(FAT1_LBA, FAT_SECS, phys);
    r |= disk_write(FAT2_LBA, FAT_SECS, phys);
    return r ? -1 : 0;
}

static int root_flush(void)
{
    uint32_t phys = (uint32_t)(uintptr_t)root_buf;
    return disk_write(ROOT_LBA, ROOT_SECS, phys) ? -1 : 0;
}

static uint8_t *root_find(const char *name11)
{
    uint8_t *p = root_buf;
    for (int i = 0; i < ROOT_ENTRIES; i++, p += 32) {
        if (!p[0] || p[0] == 0xE5) continue;
        if (p[11] & 0x08) continue;
        int match = 1;
        for (int j = 0; j < 11; j++)
            if (p[j] != (uint8_t)name11[j]) { match = 0; break; }
        if (match) return p;
    }
    return 0;
}

static uint8_t *root_alloc_slot(void)
{
    uint8_t *p = root_buf;
    for (int i = 0; i < ROOT_ENTRIES; i++, p += 32)
        if (!p[0] || p[0] == 0xE5) return p;
    return 0;
}

void fat_init(uint8_t drive)
{
    g_drive = drive;
    fat_loaded = 0;
    g_use_edd = 0;

    if (drive >= 0x80) {
        uint8_t edd_probe = bios_disk_thunk(drive, 0, 0, 0, 0, 0, 4, 0);
        if (edd_probe == 0) {
            g_use_edd = 1;
        }
    }

    if (disk_read(FAT1_LBA, FAT_SECS, (uint32_t)(uintptr_t)fat_buf)) return;
    if (disk_read(ROOT_LBA, ROOT_SECS, (uint32_t)(uintptr_t)root_buf)) return;
    fat_loaded = 1;
}

int fat_ready(void)
{
    return fat_loaded;
}

int fat_load(const char *name11, void *buf, int len)
{
    if (!fat_loaded) return -1;
    uint8_t *e = root_find(name11);
    if (!e) return -1;

    uint16_t clus = *(uint16_t *)(e + 26);
    uint32_t size = *(uint32_t *)(e + 28);
    if ((int)size < len) len = (int)size;

    uint8_t *dst = (uint8_t *)buf;
    int left = len;

    while (clus >= 2 && clus < 0xFF8 && left > 0) {
        uint32_t lba = clus_to_lba(clus);
        int take = (left < SECTOR_SZ) ? left : SECTOR_SZ;

        if (take == SECTOR_SZ) {
            if (disk_read(lba, 1, (uint32_t)(uintptr_t)dst)) return -1;
            dst += SECTOR_SZ;
            left -= SECTOR_SZ;
        } else {
            if (disk_read(lba, 1, (uint32_t)(uintptr_t)io_buf)) return -1;
            for (int i = 0; i < take; i++) *dst++ = io_buf[i];
            left -= take;
            break;
        }
        clus = fat12_get(clus);
    }
    return len - left;
}

int fat_save(const char *name11, const void *buf, int len)
{
    if (!fat_loaded) return -1;

    uint8_t *e = root_find(name11);
    if (e) {
        uint16_t c = *(uint16_t *)(e + 26);
        while (c >= 2 && c < 0xFF8) {
            uint16_t nx = fat12_get(c);
            fat12_set(c, 0);
            c = nx;
        }
    } else {
        e = root_alloc_slot();
        if (!e) return -1;
        for (int i = 0; i < 32; i++) e[i] = 0;
        for (int i = 0; i < 11; i++) e[i] = (uint8_t)name11[i];
        e[11] = 0x20;
    }

    const uint8_t *src = (const uint8_t *)buf;
    int left = len;
    uint16_t first = 0, prev = 0;

    while (left > 0) {
        uint16_t c = fat12_alloc();
        if (!c) return -1;
        fat12_set(c, 0xFF8);
        if (prev) fat12_set(prev, c);
        else first = c;
        prev = c;

        int take = (left < SECTOR_SZ) ? left : SECTOR_SZ;
        if (take < SECTOR_SZ) {
            for (int i = 0; i < SECTOR_SZ; i++)
                io_buf[i] = (i < take) ? src[i] : 0;
            if (disk_write(clus_to_lba(c), 1, (uint32_t)(uintptr_t)io_buf)) return -1;
        } else {
            if (disk_write(clus_to_lba(c), 1, (uint32_t)(uintptr_t)src)) return -1;
            src += take;
            left -= take;
        }
    }

    *(uint16_t *)(e + 26) = first;
    *(uint32_t *)(e + 28) = (uint32_t)len;

    if (fat_flush() < 0) return -1;
    if (root_flush() < 0) return -1;
    return 0;
}

void fat_dir(void (*cb)(const char *name11, uint32_t size))
{
    if (!fat_loaded) return;
    uint8_t *p = root_buf;
    char nm[12];
    for (int i = 0; i < ROOT_ENTRIES; i++, p += 32) {
        if (!p[0] || p[0] == 0xE5) continue;
        if (p[11] & 0x08) continue;
        for (int j = 0; j < 11; j++) nm[j] = (char)p[j];
        nm[11] = '\0';
        uint32_t sz = *(uint32_t *)(p + 28);
        cb(nm, sz);
    }
}
