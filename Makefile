# BOREALIS Makefile

CC     := i686-elf-gcc
CFLAGS := -m32 -ffreestanding -fno-pic -std=gnu99 \
           -Wall -Wextra -Wno-unused-parameter \
           -O2 -mfpmath=387 -fno-stack-protector \
           -I src/helix -I src/helix/fs -I src/helix/sound \
           -I src/helix/rtc -I src/helix/gfx
NASM   := nasm
PYTHON := python3

CSRCS := src/helix/helix.c \
         src/helix/basic.c \
         src/helix/fs/fat12.c \
         src/helix/sound/sound.c \
         src/helix/rtc/rtc.c \
         src/helix/gfx/gfx.c
COBJS := $(CSRCS:.c=.o)
ASMS  := src/helix/entry.asm
AOBJS := src/helix/entry.o

all: borealis.img

# ── Assemble 16-bit blobs ──────────────────────────────────────────────────
coil/stage1.bin: coil/stage1.asm
	$(NASM) -f bin -o $@ $<

coil/bootmeta.inc: helix.bin
	$(PYTHON) mkfat.py --bootmeta

coil/stage2.bin: coil/stage2.asm coil/bootmeta.inc
	$(NASM) -f bin -o $@ $<

src/helix/fs/thunk16.bin: src/helix/fs/thunk16.asm
	$(NASM) -f bin -o $@ $<

# ── Compile helix ─────────────────────────────────────────────────────────
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

src/helix/entry.o: src/helix/entry.asm src/helix/fs/thunk16.bin
	$(NASM) -f elf32 -o $@ $<

helix.bin: $(AOBJS) $(COBJS) linker.ld
	$(CC) -m32 -ffreestanding -nostdlib -T linker.ld \
	      -o helix.elf $(AOBJS) $(COBJS) -lgcc
	objcopy -O binary helix.elf $@

# ── Disk image ──────────────────────────────────────────────────────────────
borealis.img: coil/stage1.bin coil/stage2.bin helix.bin
	$(PYTHON) mkfat.py

# ── Run in QEMU ─────────────────────────────────────────────────────────────
run: borealis.img
	qemu-system-i386 -drive file=borealis.img,format=raw,if=floppy \
	                 -boot a -m 4 -audiodev pa,id=snd0 -machine pcspk-audiodev=snd0

run-vga: borealis.img
	qemu-system-i386 -drive file=borealis.img,format=raw,if=floppy \
	                 -boot a -m 4 -audiodev pa,id=snd0 -machine pcspk-audiodev=snd0

clean:
	rm -f $(COBJS) $(AOBJS) \
	coil/stage1.bin coil/stage2.bin coil/bootmeta.inc \
	src/helix/fs/thunk16.bin \
	helix.elf helix.bin borealis.img

pack:
	mcopy -i borealis.img pong/PONG.BIN ::PONG.BIN
	mcopy -i borealis.img basic/primes.bas ::PRIMES.BAS
	mcopy -i borealis.img basic/scrtest.bas ::SCRTEST.BAS

box:
	cd .. && tar -czvf BOREALIS-vers.tar.gz BOREALIS/*

.PHONY: all run run-vga clean
