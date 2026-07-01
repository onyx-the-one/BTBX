CC      = i686-elf-gcc
CFLAGS  = -m32 -std=c99 -ffreestanding -fno-builtin -fno-stack-protector \
          -Wall -Wextra -Werror -O2 -mfpmath=387 -mno-sse -Ikernel
LD      = i686-elf-ld
LDFLAGS = -m elf_i386 -T linker.ld
KOBJS   = kernel/entry.o kernel/kernel.o kernel/basic.o kernel/fat12.o \
          kernel/sound.o
IMAGE   = btbx.img

.PHONY: all clean run run-hd

all: $(IMAGE)

$(IMAGE): boot/stage1.bin boot/stage2.bin kernel.bin
	python3 mkfat.py
	@echo "Built $(IMAGE)"

kernel.bin: kernel/thunk16.bin $(KOBJS)
	$(LD) $(LDFLAGS) -o kernel.elf $(KOBJS)
	i686-elf-objcopy -O binary kernel.elf kernel.bin

kernel/thunk16.bin: kernel/thunk16.asm
	nasm -f bin -o $@ $<

boot/stage1.bin: boot/stage1.asm
	nasm -f bin -o $@ $<

boot/stage2.bin: boot/stage2.asm
	nasm -f bin -o $@ $<

kernel/entry.o: kernel/entry.asm kernel/thunk16.bin
	nasm -f elf32 -o $@ $<

kernel/kernel.o: kernel/kernel.c
	$(CC) $(CFLAGS) -c -o $@ $<

kernel/basic.o: kernel/basic.c
	$(CC) $(CFLAGS) -c -o $@ $<

kernel/fat12.o: kernel/fat12.c
	$(CC) $(CFLAGS) -c -o $@ $<

kernel/sound.o: kernel/sound.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(KOBJS) kernel.elf kernel.bin kernel/thunk16.bin \
	      boot/stage1.bin boot/stage2.bin $(IMAGE)

run: $(IMAGE)
	qemu-system-i386 -fda $(IMAGE) -m 32 -audiodev pa,id=snd0 -machine pcspk-audiodev=snd0

run-hd: $(IMAGE)
	qemu-system-i386 -drive format=raw,file=$(IMAGE),if=ide,index=0 -boot c -m 32 -audiodev pa,id=snd0 -machine pcspk-audiodev=snd0
