# BTBX Makefile
# Produces btbx.img: a 1.44MB floppy image bootable on realistically any x86.
#
# Layout:
#   Sector  0       stage1 (MBR)        512 bytes
#   Sectors 1-4     stage2 (loader)    2048 bytes
#   Sectors 5+      kernel.bin

CC      = i686-elf-gcc
CFLAGS  = -m32 -std=c99 -ffreestanding -fno-builtin -fno-stack-protector \
          -Wall -Wextra -Werror -O2 -Ikernel
LD      = i686-elf-ld
LDFLAGS = -m elf_i386 -T linker.ld
KOBJS   = kernel/entry.o kernel/kernel.o kernel/basic.o
IMAGE   = btbx.img

.PHONY: all clean run

all: $(IMAGE)

$(IMAGE): boot/stage1.bin boot/stage2.bin kernel.bin
	# Create blank 1.44MB floppy image
	dd if=/dev/zero of=$(IMAGE) bs=512 count=2880 2>/dev/null
	# Write stage1 to sector 0
	dd if=boot/stage1.bin of=$(IMAGE) bs=512 count=1 conv=notrunc 2>/dev/null
	# Write stage2 to sectors 1-4
	dd if=boot/stage2.bin of=$(IMAGE) bs=512 seek=1 count=4 conv=notrunc 2>/dev/null
	# Write kernel to sectors 5+
	dd if=kernel.bin of=$(IMAGE) bs=512 seek=5 conv=notrunc 2>/dev/null
	@echo "Built $(IMAGE)"

kernel.bin: $(KOBJS)
	$(LD) $(LDFLAGS) -o kernel.elf $(KOBJS)
	i686-elf-objcopy -O binary kernel.elf $@
	@echo "kernel.bin: $$(wc -c < kernel.bin) bytes"

boot/stage1.bin: boot/stage1.asm
	nasm -f bin -o $@ $<

boot/stage2.bin: boot/stage2.asm
	nasm -f bin -o $@ $<

kernel/entry.o: kernel/entry.asm
	nasm -f elf32 -o $@ $<

kernel/kernel.o: kernel/kernel.c
	$(CC) $(CFLAGS) -c -o $@ $<

kernel/basic.o: kernel/basic.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(KOBJS) kernel.elf kernel.bin \
	      boot/stage1.bin boot/stage2.bin $(IMAGE)

# Use -fda for floppy image
run: $(IMAGE)
	qemu-system-i386 -fda $(IMAGE) -m 32

# Alt: use as hard disk (if -fda fails on your QEMU version)
run-hd: $(IMAGE)
	qemu-system-i386 -drive format=raw,file=$(IMAGE),if=ide,index=0 -boot c -m 32
