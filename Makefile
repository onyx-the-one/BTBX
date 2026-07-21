# BTBX Makefile — ver. 1.6.32

CC     := i686-elf-gcc
CFLAGS := -m32 -ffreestanding -fno-pic -std=gnu99 \
           -Wall -Wextra -Wno-unused-parameter \
           -O2 -mfpmath=387 -fno-stack-protector \
           -I src/kernel -I src/kernel/fs -I src/kernel/sound \
           -I src/kernel/rtc -I src/kernel/gfx
NASM   := nasm
PYTHON := python3

CSRCS := src/kernel/kernel.c \
         src/kernel/basic.c \
         src/kernel/fs/fat12.c \
         src/kernel/sound/sound.c \
         src/kernel/rtc/rtc.c \
         src/kernel/gfx/gfx.c
COBJS := $(CSRCS:.c=.o)
ASMS  := src/kernel/entry.asm
AOBJS := src/kernel/entry.o

all: btbx.img

# ── Assemble 16-bit blobs ──────────────────────────────────────────────────
boot/stage1.bin: boot/stage1.asm
	$(NASM) -f bin -o $@ $<

boot/bootmeta.inc: kernel.bin
	$(PYTHON) mkfat.py --bootmeta

boot/stage2.bin: boot/stage2.asm boot/bootmeta.inc
	$(NASM) -f bin -o $@ $<

src/kernel/fs/thunk16.bin: src/kernel/fs/thunk16.asm
	$(NASM) -f bin -o $@ $<

# ── Compile kernel ─────────────────────────────────────────────────────────
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

src/kernel/entry.o: src/kernel/entry.asm src/kernel/fs/thunk16.bin
	$(NASM) -f elf32 -o $@ $<

kernel.bin: $(AOBJS) $(COBJS) linker.ld
	$(CC) -m32 -ffreestanding -nostdlib -T linker.ld \
	      -o kernel.elf $(AOBJS) $(COBJS) -lgcc
	objcopy -O binary kernel.elf $@

# ── Disk image ──────────────────────────────────────────────────────────────
btbx.img: boot/stage1.bin boot/stage2.bin kernel.bin
	$(PYTHON) mkfat.py

# ── Run in QEMU ─────────────────────────────────────────────────────────────
run: btbx.img
	qemu-system-i386 -drive file=btbx.img,format=raw,if=floppy \
	                 -boot a -m 4 -audiodev pa,id=snd0 -machine pcspk-audiodev=snd0

run-vga: btbx.img
	qemu-system-i386 -drive file=btbx.img,format=raw,if=floppy \
	                 -boot a -m 4 -audiodev pa,id=snd0 -machine pcspk-audiodev=snd0

clean:
	rm -f $(COBJS) $(AOBJS) \
	boot/stage1.bin boot/stage2.bin boot/bootmeta.inc \
	src/kernel/fs/thunk16.bin \
	kernel.elf kernel.bin btbx.img

box:
	cd .. && tar -czvf BTBX-vers.tar.gz BTBX

.PHONY: all run run-vga clean
