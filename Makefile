# BTBX Makefile — ver. 1.6.32

CC := i686-elf-gcc
OBJCOPY := objcopy
CFLAGS := -m32 -ffreestanding -fno-pic -std=gnu99 \
-Wall -Wextra -Wno-unused-parameter \
-O2 -mfpmath=387 -fno-stack-protector \
-I src/kernel -I src/kernel/fs -I src/kernel/sound
NASM := nasm
PYTHON := python3

CSRCS := src/kernel/kernel.c \
src/kernel/basic.c \
src/kernel/fs/fat12.c \
src/kernel/sound/sound.c
COBJS := $(CSRCS:.c=.o)
ASMS := src/kernel/entry.asm
AOBJS := src/kernel/entry.o
BOOTMETA := boot/bootmeta.inc

all: btbx.img

boot/stage1.bin: boot/stage1.asm
	$(NASM) -f bin -o $@ $<

boot/stage2.bin: boot/stage2.asm $(BOOTMETA)
	$(NASM) -I boot/ -f bin -o $@ $<

src/kernel/fs/thunk16.bin: src/kernel/fs/thunk16.asm
	$(NASM) -f bin -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

src/kernel/entry.o: src/kernel/entry.asm src/kernel/fs/thunk16.bin
	$(NASM) -f elf32 -o $@ $<

kernel.bin: $(AOBJS) $(COBJS) linker.ld
	$(CC) -m32 -ffreestanding -nostdlib -T linker.ld \
	-o kernel.elf $(AOBJS) $(COBJS) -lgcc
	$(OBJCOPY) -O binary kernel.elf $@

$(BOOTMETA): kernel.bin mkfat.py
	$(PYTHON) mkfat.py --bootmeta

btbx.img: boot/stage1.bin boot/stage2.bin kernel.bin mkfat.py
	$(PYTHON) mkfat.py

run: btbx.img
	qemu-system-i386 -drive file=btbx.img,format=raw,if=floppy \
	-boot a -m 4 -display curses -audiodev pa,id=snd0 -machine pcspk-audiodev=snd0

run-vga: btbx.img
	qemu-system-i386 -drive file=btbx.img,format=raw,if=floppy \
	-boot a -m 4 -audiodev pa,id=snd0 -machine pcspk-audiodev=snd0

clean:
	rm -f $(COBJS) $(AOBJS) \
	boot/stage1.bin boot/stage2.bin boot/bootmeta.inc \
	src/kernel/fs/thunk16.bin \
	kernel.elf kernel.bin btbx.img

.PHONY: all run run-vga clean
