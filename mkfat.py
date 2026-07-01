#!/usr/bin/env python3
import struct, sys, os

# 1.44 MB floppy geometry
SECTORS = 2880
SECTOR_SZ = 512
SPT = 18
HEADS = 2
CYLS = 80
FAT_COUNT = 2
FAT_SECS = 9
ROOT_ENTRIES = 224
ROOT_SECS = ROOT_ENTRIES * 32 // SECTOR_SZ   # 14
SECS_CLUS = 1
RESERVED = 1          # only sector 0 is "reserved" in BPB terms
                         # stage2 lives in the space before FAT starts,
                         # which we handle by putting it in sectors 1-4
                         # and setting RESERVED=5 so FAT starts at LBA 5

# actual layout
RESERVED_SECS = 5        # LBA 0=stage1, 1-4=stage2, FAT starts at 5
FAT1_LBA = RESERVED_SECS
FAT2_LBA = FAT1_LBA + FAT_SECS
ROOT_LBA = FAT2_LBA + FAT_SECS
DATA_LBA = ROOT_LBA + ROOT_SECS

img = bytearray(SECTORS * SECTOR_SZ)

def write_sector(lba, data):
    assert len(data) <= SECTOR_SZ
    off = lba * SECTOR_SZ
    img[off:off+len(data)] = data
    # rest stays 0

def write_sectors(lba, data):
    for i in range(0, len(data), SECTOR_SZ):
        chunk = data[i:i+SECTOR_SZ]
        write_sector(lba + i//SECTOR_SZ, chunk)

# ── stage1: read from file, patch BPB into it ──────────────────────────────
with open("boot/stage1.bin","rb") as f:
    s1 = bytearray(f.read())
assert len(s1) == 512

# patch BPB at offset 3 (after the jmp + nop)
# OEM name
s1[3:11] = b'BTBX1.1 '
bpb  = struct.pack('<H', SECTOR_SZ)      # bytes per sector
bpb += struct.pack('<B', SECS_CLUS)      # sectors per cluster
bpb += struct.pack('<H', RESERVED_SECS)  # reserved sectors
bpb += struct.pack('<B', FAT_COUNT)      # number of FATs
bpb += struct.pack('<H', ROOT_ENTRIES)   # root dir entries
bpb += struct.pack('<H', SECTORS)        # total sectors
bpb += struct.pack('<B', 0xF0)           # media descriptor
bpb += struct.pack('<H', FAT_SECS)       # sectors per FAT
bpb += struct.pack('<H', SPT)            # sectors per track
bpb += struct.pack('<H', HEADS)          # heads
bpb += struct.pack('<I', 0)              # hidden sectors
s1[11:11+len(bpb)] = bpb
# total sectors 32-bit field at offset 32 (already 0 since we set 16-bit above)
write_sector(0, s1)

# ── stage2 ──────────────────────────────────────────────────────────────────
with open("boot/stage2.bin","rb") as f:
    s2 = f.read()
assert len(s2) <= 4 * SECTOR_SZ, f"stage2 too large: {len(s2)}"
write_sectors(1, s2)

# ── FAT tables (empty, all clusters free except 0 and 1) ────────────────────
fat = bytearray(FAT_SECS * SECTOR_SZ)
# first two entries: media byte + 0xFF
fat[0] = 0xF0; fat[1] = 0xFF; fat[2] = 0xFF
write_sectors(FAT1_LBA, fat)
write_sectors(FAT2_LBA, fat)

# ── root directory (empty) ───────────────────────────────────────────────────
# already zeroed

# ── kernel payload (optional, as a FAT file) ────────────────────────────────
# We also raw-load the kernel from fixed LBA in stage2, so we place kernel.bin
# starting at DATA_LBA (cluster 2) AND register it as a file so fat_load works.
if os.path.exists("kernel.bin"):
    with open("kernel.bin","rb") as f:
        kdata = f.read()

    # write clusters
    clus = 2
    pos  = 0
    first_clus = clus
    clus_count  = (len(kdata) + SECTOR_SZ - 1) // SECTOR_SZ

    for i in range(clus_count):
        lba = DATA_LBA + i
        chunk = kdata[pos:pos+SECTOR_SZ]
        write_sector(lba, chunk)
        pos += SECTOR_SZ

    # build FAT chain
    fat2 = bytearray(fat)  # fresh copy
    for i in range(clus_count):
        c = first_clus + i
        nxt = (first_clus + i + 1) if i < clus_count - 1 else 0xFF8
        # write 12-bit entry
        off = c + c // 2
        v   = struct.unpack_from('<H', fat2, off)[0]
        if c & 1:
            v = (v & 0x000F) | (nxt << 4)
        else:
            v = (v & 0xF000) | (nxt & 0xFFF)
        struct.pack_into('<H', fat2, off, v)

    write_sectors(FAT1_LBA, fat2)
    write_sectors(FAT2_LBA, fat2)

    # root dir entry for KERNEL.BIN
    name = b'KERNEL  BIN'
    entry = bytearray(32)
    entry[0:11]  = name
    entry[11]    = 0x20          # archive
    entry[26:28] = struct.pack('<H', first_clus)
    entry[28:32] = struct.pack('<I', len(kdata))
    # write to first root dir slot
    off = ROOT_LBA * SECTOR_SZ
    img[off:off+32] = entry

with open("btbx.img","wb") as f:
    f.write(img)

has_kernel = 'yes' if os.path.exists("kernel.bin") else 'no'
print(f"btbx.img: {SECTORS} sectors, DATA_LBA={DATA_LBA}, kernel={has_kernel}")
