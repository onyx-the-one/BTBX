#ifndef FAT12_H
#define FAT12_H
#include <stdint.h>

void fat_init(uint8_t drive);
int  fat_ready(void);
int  fat_load(const char *name11, void *buf, int len);
int  fat_save(const char *name11, const void *buf, int len);
void fat_dir(void (*cb)(const char *name11, uint32_t size));

#endif
