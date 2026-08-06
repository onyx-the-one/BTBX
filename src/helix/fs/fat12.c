#include "fat12.h"
#include "basic.h"   /* DEBUG: needed for term_puts()/term_puti() instrumentation below */
#include <stdint.h>

/* DEBUG: helper to print an unsigned byte/dword as 2/8 hex digits.
 * Used everywhere below to report drive numbers, LBAs, sector counts
 * and error codes exactly as the BIOS reported them -- so a failure
 * report always carries enough detail to reproduce/diagnose it
 * without needing to re-instrument anything. Kept static/local to
 * this file to avoid touching the public terminal API in basic.h. */
static void dbg_hex8(uint8_t v){
    const char *hexd = "0123456789ABCDEF";
    term_putchar(hexd[(v >> 4) & 0xF]);
    term_putchar(hexd[v & 0xF]);
}

static void dbg_hex32(uint32_t v){
    const char *hexd = "0123456789ABCDEF";
    for(int shift = 28; shift >= 0; shift -= 4){
        term_putchar(hexd[(v >> shift) & 0xF]);
    }
}

#define SPT            18
#define HEADS          2
#define SECTOR_SZ      512
#define FAT1_LBA       5
#define FAT_SECS       9
#define FAT2_LBA       (FAT1_LBA + FAT_SECS)
#define ROOT_LBA       (FAT2_LBA + FAT_SECS)
#define ROOT_SECS      14
#define DATA_LBA       (ROOT_LBA + ROOT_SECS)
#define SECS_PER_CLUS  1
#define ROOT_ENTRIES   224
#define TOTAL_SECTORS  2880
#define BOUNCE_PHYS    0x0800u
#define BOUNCE_PTR     ((uint8_t *)BOUNCE_PHYS)

enum {
    BIOS_CHS_READ  = 0,
    BIOS_EDD_READ  = 1,
    BIOS_CHS_WRITE = 2,
    BIOS_EDD_WRITE = 3,
    BIOS_EDD_PROBE = 4
};

typedef struct {
    uint8_t drive;
    uint8_t use_edd;
    uint8_t spt;
    uint8_t heads;
} DiskState;

static DiskState g_disk;
static uint8_t fat_buf[FAT_SECS * SECTOR_SZ];
static uint8_t root_buf[ROOT_SECS * SECTOR_SZ];
static uint8_t io_buf[SECTOR_SZ];
static int fat_loaded;

extern uint8_t bios_disk_thunk(uint8_t drive, uint8_t head, uint8_t sector,
                               uint8_t cyl, uint16_t count, uint32_t buf_phys,
                               uint8_t use_edd, uint32_t lba_lo);
extern uint8_t bios_get_geometry(uint8_t drive, uint8_t *spt_out, uint8_t *heads_out);

/* NOTE: EDD (int 13h ah=41h presence-check AND ah=42h extended read)
 * has been confirmed to hang indefinitely on at least one real BIOS
 * (Core 2 Duo era laptop), even though plain legacy CHS (ah=02h/03h)
 * works fine on that same machine. EDD is therefore disabled
 * permanently here rather than probed for -- we always use CHS, but
 * we ask the BIOS for its *real* geometry (ah=08h) instead of
 * assuming floppy geometry (18 SPT / 2 heads), since that assumption
 * is wrong for USB/HDD-style boot media and would silently read the
 * wrong sectors. If the geometry query itself fails, we fall back to
 * the floppy defaults as a last resort. */
static void disk_init_backend(uint8_t drive){
    g_disk.drive = drive;
    g_disk.use_edd = 0;
    g_disk.spt = SPT;
    g_disk.heads = HEADS;

    /* DEBUG: entering disk backend init */
    term_puts("[DEBUG fat12: disk_init_backend drive=0x");
    dbg_hex8(drive);
    term_puts("]\n");

    if(drive >= 0x80){
        uint8_t spt = 0, heads = 0;
        uint8_t st = bios_get_geometry(drive, &spt, &heads);

        /* DEBUG: report exactly what the BIOS gave us (or didn't) */
        term_puts("[DEBUG fat12: bios_get_geometry status=0x");
        dbg_hex8(st);
        term_puts(" spt=");
        term_puti(spt);
        term_puts(" heads=");
        term_puti(heads);
        term_puts("]\n");

        if(!st && spt && heads){
            g_disk.spt = spt;
            g_disk.heads = heads;
            term_puts("[DEBUG fat12: using BIOS-reported CHS geometry]\n");
        } else {
            term_puts("[DEBUG fat12: WARNING geometry query failed or returned zero -- falling back to floppy defaults SPT=18 HEADS=2, disk reads WILL be wrong if this drive is not a real floppy]\n");
        }
    } else {
        term_puts("[DEBUG fat12: drive < 0x80, assuming floppy geometry SPT=18 HEADS=2]\n");
    }
}

static void disk_lba_to_chs(uint32_t lba, uint8_t *cyl, uint8_t *head, uint8_t *sec){
    *sec  = (uint8_t)(lba % g_disk.spt) + 1;
    *head = (uint8_t)((lba / g_disk.spt) % g_disk.heads);
    *cyl  = (uint8_t)(lba / g_disk.spt / g_disk.heads);
}

static int disk_xfer_one(int is_write, uint32_t lba, uint32_t buf_phys){
    uint8_t cyl = 0, head = 0, sec = 0;
    uint8_t op;
    uint8_t *bounce = BOUNCE_PTR;
    uint8_t *user = (uint8_t *)(uintptr_t)buf_phys;

    if(!g_disk.use_edd) disk_lba_to_chs(lba, &cyl, &head, &sec);
    op = is_write ? (g_disk.use_edd ? BIOS_EDD_WRITE : BIOS_CHS_WRITE)
                  : (g_disk.use_edd ? BIOS_EDD_READ  : BIOS_CHS_READ);

    if(is_write){
        for(int i=0;i<SECTOR_SZ;i++) bounce[i] = user[i];
    }

    uint8_t st = bios_disk_thunk(g_disk.drive, head, sec, cyl, 1, BOUNCE_PHYS, op, lba);
    if(st){
        /* DEBUG: this is the single most important error message in
         * the whole disk stack -- every failed sector read/write
         * bottoms out here. Always report LBA + CHS + status so a
         * failure can be traced back to the exact sector involved. */
        term_puts("[DEBUG fat12: DISK ");
        term_puts(is_write ? "WRITE" : "READ");
        term_puts(" FAILED lba=0x");
        dbg_hex32(lba);
        term_puts(" chs=(c=");
        term_puti(cyl);
        term_puts(",h=");
        term_puti(head);
        term_puts(",s=");
        term_puti(sec);
        term_puts(") status=0x");
        dbg_hex8(st);
        term_puts("]\n");
        return -1;
    }

    if(!is_write){
        for(int i=0;i<SECTOR_SZ;i++) user[i] = bounce[i];
    }

    return 0;
}

static int disk_read_lba(uint32_t lba, uint16_t count, uint32_t buf_phys){
    while(count){
        if(disk_xfer_one(0, lba, buf_phys)){
            /* DEBUG: propagate exactly where in a multi-sector read it broke */
            term_puts("[DEBUG fat12: disk_read_lba aborted, sectors_remaining=");
            term_puti(count);
            term_puts("]\n");
            return -1;
        }
        lba++;
        count--;
        buf_phys += SECTOR_SZ;
    }
    return 0;
}

static int disk_write_lba(uint32_t lba, uint16_t count, uint32_t buf_phys){
    while(count){
        if(disk_xfer_one(1, lba, buf_phys)){
            /* DEBUG: propagate exactly where in a multi-sector write it broke */
            term_puts("[DEBUG fat12: disk_write_lba aborted, sectors_remaining=");
            term_puti(count);
            term_puts("]\n");
            return -1;
        }
        lba++;
        count--;
        buf_phys += SECTOR_SZ;
    }
    return 0;
}

static uint16_t fat12_get(uint16_t clus){
    uint32_t off = (uint32_t)clus + clus / 2;
    uint16_t v = *(uint16_t *)(fat_buf + off);
    return (clus & 1) ? (uint16_t)(v >> 4) : (uint16_t)(v & 0x0FFF);
}

static void fat12_set(uint16_t clus, uint16_t val){
    uint32_t off = (uint32_t)clus + clus / 2;
    uint16_t *p = (uint16_t *)(fat_buf + off);
    if(clus & 1) *p = (uint16_t)((*p & 0x000F) | (val << 4));
    else         *p = (uint16_t)((*p & 0xF000) | (val & 0x0FFF));
}

static uint32_t clus_to_lba(uint16_t clus){
    return DATA_LBA + (uint32_t)(clus - 2) * SECS_PER_CLUS;
}

#define TOTAL_CLUS ((uint16_t)((TOTAL_SECTORS - DATA_LBA) / SECS_PER_CLUS + 2))

static uint16_t fat12_alloc(void){
    for(uint16_t c=2;c<TOTAL_CLUS;c++){
        if(!fat12_get(c)) return c;
    }
    /* DEBUG: FAT is completely full -- worth knowing loudly, since a
     * silent 0 return here just looks like "save failed" otherwise. */
    term_puts("[DEBUG fat12: fat12_alloc FAILED -- no free clusters left, disk is full]\n");
    return 0;
}

static int fat_flush(void){
    term_puts("[DEBUG fat12: fat_flush -- writing FAT1+FAT2]\n");
    uint32_t phys = (uint32_t)(uintptr_t)fat_buf;
    if(disk_write_lba(FAT1_LBA, FAT_SECS, phys)){
        term_puts("[DEBUG fat12: fat_flush FAILED writing FAT1]\n");
        return -1;
    }
    if(disk_write_lba(FAT2_LBA, FAT_SECS, phys)){
        term_puts("[DEBUG fat12: fat_flush FAILED writing FAT2]\n");
        return -1;
    }
    term_puts("[DEBUG fat12: fat_flush ok]\n");
    return 0;
}

static int root_flush(void){
    term_puts("[DEBUG fat12: root_flush -- writing root directory]\n");
    int st = disk_write_lba(ROOT_LBA, ROOT_SECS, (uint32_t)(uintptr_t)root_buf);
    if(st) term_puts("[DEBUG fat12: root_flush FAILED]\n");
    else   term_puts("[DEBUG fat12: root_flush ok]\n");
    return st;
}

static uint8_t *root_find(const char *name11){
    uint8_t *p = root_buf;
    for(int i=0;i<ROOT_ENTRIES;i++,p+=32){
        if(!p[0] || p[0] == 0xE5) continue;
        if(p[11] & 0x08) continue;
        int match = 1;
        for(int j=0;j<11;j++){
            if(p[j] != (uint8_t)name11[j]){
                match = 0;
                break;
            }
        }
        if(match) return p;
    }
    return 0;
}

static uint8_t *root_alloc_slot(void){
    uint8_t *p = root_buf;
    for(int i=0;i<ROOT_ENTRIES;i++,p+=32){
        if(!p[0] || p[0] == 0xE5) return p;
    }
    /* DEBUG: root directory is completely full (224 entries used up) */
    term_puts("[DEBUG fat12: root_alloc_slot FAILED -- root directory full]\n");
    return 0;
}

static int fat_cache_load(void){
    term_puts("[DEBUG fat12: fat_cache_load -- reading FAT + root directory into RAM]\n");
    if(disk_read_lba(FAT1_LBA, FAT_SECS, (uint32_t)(uintptr_t)fat_buf)){
        term_puts("[DEBUG fat12: fat_cache_load FAILED reading FAT table]\n");
        return -1;
    }
    if(disk_read_lba(ROOT_LBA, ROOT_SECS, (uint32_t)(uintptr_t)root_buf)){
        term_puts("[DEBUG fat12: fat_cache_load FAILED reading root directory]\n");
        return -1;
    }
    fat_loaded = 1;
    term_puts("[DEBUG fat12: fat_cache_load ok, filesystem is ready]\n");
    return 0;
}

void fat_init(uint8_t drive){
    /* DEBUG: entry point for the whole filesystem subsystem -- every
     * boot will print this exactly once, right before disk I/O
     * starts. If this never prints, the hang is before fat_init() is
     * even called (i.e. in kernel_main() itself, before this call). */
    term_puts("[DEBUG fat12: fat_init ENTER drive=0x");
    dbg_hex8(drive);
    term_puts("]\n");

    fat_loaded = 0;
    disk_init_backend(drive);
    if(fat_cache_load()){
        term_puts("[DEBUG fat12: fat_init FAILED -- filesystem will be unavailable]\n");
        return;
    }
    term_puts("[DEBUG fat12: fat_init EXIT ok]\n");
}

int fat_ready(void){
    return fat_loaded;
}

int fat_load(const char *name11, void *buf, int len){
    term_puts("[DEBUG fat12: fat_load ENTER]\n");

    if(!fat_loaded){
        term_puts("[DEBUG fat12: fat_load FAILED -- filesystem not ready]\n");
        return -1;
    }

    uint8_t *e = root_find(name11);
    if(!e){
        term_puts("[DEBUG fat12: fat_load FAILED -- file not found]\n");
        return -1;
    }

    uint16_t clus = *(uint16_t *)(e + 26);
    uint32_t size = *(uint32_t *)(e + 28);

    if((int)size < len) len = (int)size;

    uint8_t *dst = (uint8_t *)buf;
    int left = len;

    while(clus >= 2 && clus < 0xFF8 && left > 0){
        uint32_t lba = clus_to_lba(clus);
        int take = (left < SECTOR_SZ) ? left : SECTOR_SZ;

        if(take == SECTOR_SZ){
            if(disk_read_lba(lba, 1, (uint32_t)(uintptr_t)dst)){
                term_puts("[DEBUG fat12: fat_load FAILED mid-file at cluster ");
                term_puti(clus);
                term_puts("]\n");
                return -1;
            }
            dst += SECTOR_SZ;
            left -= SECTOR_SZ;
        } else {
            if(disk_read_lba(lba, 1, (uint32_t)(uintptr_t)io_buf)){
                term_puts("[DEBUG fat12: fat_load FAILED on final partial sector, cluster ");
                term_puti(clus);
                term_puts("]\n");
                return -1;
            }
            for(int i=0;i<take;i++) dst[i] = io_buf[i];
            left = 0;
        }

        clus = fat12_get(clus);
    }

    term_puts("[DEBUG fat12: fat_load EXIT ok, bytes=");
    term_puti(len - left);
    term_puts("]\n");
    return len - left;
}

int fat_save(const char *name11, const void *buf, int len){
    term_puts("[DEBUG fat12: fat_save ENTER len=");
    term_puti(len);
    term_puts("]\n");

    if(!fat_loaded){
        term_puts("[DEBUG fat12: fat_save FAILED -- filesystem not ready]\n");
        return -1;
    }

    uint8_t *e = root_find(name11);
    if(e){
        uint16_t c = *(uint16_t *)(e + 26);
        while(c >= 2 && c < 0xFF8){
            uint16_t nx = fat12_get(c);
            fat12_set(c, 0);
            c = nx;
        }
    } else {
        e = root_alloc_slot();
        if(!e){
            term_puts("[DEBUG fat12: fat_save FAILED -- could not allocate directory entry]\n");
            return -1;
        }
        for(int i=0;i<32;i++) e[i] = 0;
        for(int i=0;i<11;i++) e[i] = (uint8_t)name11[i];
        e[11] = 0x20;
    }

    const uint8_t *src = (const uint8_t *)buf;
    int left = len;
    uint16_t first = 0, prev = 0;

    while(left > 0){
        uint16_t c = fat12_alloc();
        if(!c){
            term_puts("[DEBUG fat12: fat_save FAILED -- disk full mid-write]\n");
            return -1;
        }

        fat12_set(c, 0xFF8);
        if(prev) fat12_set(prev, c);
        else first = c;
        prev = c;

        int take = (left < SECTOR_SZ) ? left : SECTOR_SZ;
        if(take < SECTOR_SZ){
            for(int i=0;i<SECTOR_SZ;i++) io_buf[i] = (i < take) ? src[i] : 0;
            if(disk_write_lba(clus_to_lba(c), 1, (uint32_t)(uintptr_t)io_buf)){
                term_puts("[DEBUG fat12: fat_save FAILED writing final partial sector, cluster ");
                term_puti(c);
                term_puts("]\n");
                return -1;
            }
        } else {
            if(disk_write_lba(clus_to_lba(c), 1, (uint32_t)(uintptr_t)src)){
                term_puts("[DEBUG fat12: fat_save FAILED writing cluster ");
                term_puti(c);
                term_puts("]\n");
                return -1;
            }
        }

        src += take;
        left -= take;
    }

    *(uint16_t *)(e + 26) = first;
    *(uint32_t *)(e + 28) = (uint32_t)len;

    if(fat_flush()){
        term_puts("[DEBUG fat12: fat_save FAILED -- could not flush FAT table]\n");
        return -1;
    }
    if(root_flush()){
        term_puts("[DEBUG fat12: fat_save FAILED -- could not flush root directory]\n");
        return -1;
    }

    term_puts("[DEBUG fat12: fat_save EXIT ok]\n");
    return 0;
}

void fat_dir(void (*cb)(const char *name11, uint32_t size)){
    if(!fat_loaded){
        term_puts("[DEBUG fat12: fat_dir called but filesystem not ready]\n");
        return;
    }

    uint8_t *p = root_buf;
    char nm[12];

    for(int i=0;i<ROOT_ENTRIES;i++,p+=32){
        if(!p[0] || p[0] == 0xE5) continue;
        if(p[11] & 0x08) continue;

        for(int j=0;j<11;j++) nm[j] = (char)p[j];
        nm[11] = 0;

        cb(nm, *(uint32_t *)(p + 28));
    }
}
