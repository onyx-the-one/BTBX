#ifndef FAT12_H
#define FAT12_H

#include <stdint.h>

/* Initialise FAT layer.  drive = BIOS drive byte (0x00=floppy, 0x80=HDD).
   Must be called once before any other fat_* function.              */
void fat_init(uint8_t drive);

/* Returns 1 if fat_init succeeded, 0 otherwise.                     */
int  fat_ready(void);

/* Load a file by 8.3 name into buf (up to len bytes).
   name11 must be exactly 11 chars, space-padded, upper-case,
   e.g. "HELLO   BAS".
   Returns bytes loaded, or -1 on error.                             */
int  fat_load(const char *name11, void *buf, int len);

/* Save buf (len bytes) as name11.  Creates or overwrites.
   Returns 0 on success, -1 on error.
   NOTE: requires write support in bios_disk_thunk (AH=03h).        */
int  fat_save(const char *name11, const void *buf, int len);

/* Enumerate root directory entries.
   Calls cb(name11, size) for each valid non-volume-label entry.
   name11 is a 12-byte null-terminated "NAMEEEEEXT" string.          */
void fat_dir(void (*cb)(const char *name11, uint32_t size));

#endif /* FAT12_H */
