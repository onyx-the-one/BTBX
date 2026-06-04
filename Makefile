# BTBX Makefile
# Requires: nasm, i686-elf-gcc (or i686-linux-gnu-gcc), grub-mkrescue, xorriso

CC      = i686-elf-gcc
# If using system cross-compiler:
# CC    = i686-linux-gnu-gcc

CFLAGS  = -m32 -std=c99 -ffreestanding -fno-builtin -fno-stack-protector \
          -Wall -Wextra -O2 -Ikernel

AS      = nasm
ASFLAGS = -f elf32

LD      = i686-elf-ld
# If using system cross-linker:
# LD    = i686-linux-gnu-ld

LDFLAGS = -m elf_i386 -T linker.ld

TARGET  = btbx.elf
ISO     = btbx.iso

OBJS    = boot/boot.o kernel/kernel.o kernel/basic.o

.PHONY: all iso clean run

all: $(TARGET)

$(TARGET): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

boot/boot.o: boot/boot.asm
	$(AS) $(ASFLAGS) -o $@ $<

kernel/kernel.o: kernel/kernel.c
	$(CC) $(CFLAGS) -c -o $@ $<

kernel/basic.o: kernel/basic.c
	$(CC) $(CFLAGS) -c -o $@ $<

iso: $(TARGET)
	mkdir -p iso/boot/grub
	cp $(TARGET) iso/boot/
	echo 'set timeout=0'                          > iso/boot/grub/grub.cfg
	echo 'set default=0'                         >> iso/boot/grub/grub.cfg
	echo 'menuentry "BTBX" {'                    >> iso/boot/grub/grub.cfg
	echo '  multiboot /boot/btbx.elf'            >> iso/boot/grub/grub.cfg
	echo '}'                                     >> iso/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO) iso

run: iso
	qemu-system-i386 -cdrom $(ISO) -m 32 -nographic 2>/dev/null || \
	qemu-system-i386 -cdrom $(ISO) -m 32

clean:
	rm -f $(OBJS) $(TARGET) $(ISO)
	rm -rf iso/
