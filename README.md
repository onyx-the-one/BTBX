# BTBX  Bare TinyBASIC eXecutor

Boots on baremetal x86 and presents a TinyBASIC interpreter.

## Requirements

- `nasm`
- `i686-elf-gcc`, `i686-elf-ld`, `i686-elf-objcopy`
- `dd`, `qemu-system-i386`

## Build

```bash
make         #  btbx.img  (1.44MB floppy image)
make run     # test in QEMU as floppy
make run-hd  # test in QEMU as hard disk (alternative)
make clean
```

## Boot on real hardware

```bash
# USB drive (careful  this wipes it):
# (Theres a reason dd is called disk destroyer)
sudo dd if=btbx.img of=/dev/sd?? bs=512
```

Enable legacy/CSM BIOS boot on UEFI machines.

## Boot sequence (with checkpoints)

On screen you will see: `S1>S2AK` then the BTBX banner.

| Character | Meaning |
|-----------|---------|
| `S1` | Stage 1 (MBR) running |
| `>` | Stage 1 jumped to Stage 2 |
| `S2` | Stage 2 running |
| `A` | A20 line enabled |
| `K` | Kernel loaded from disk |
| *(banner)* | Kernel C code running |

If it stops at `S1` and shows `!`  disk read in stage 1 failed.
If it shows `S1>S2A!`  kernel load failed.

## Disk image layout

| Sectors | Content |
|---------|---------|
| 0 | stage1 (MBR, 512 bytes) |
| 14 | stage2 (loader, 2048 bytes) |
| 5+ | kernel flat binary |

## BASIC reference

Variables: `A``Z` (32-bit signed integer)

```
10 FOR I=1 TO 10
20 PRINT I;" squared = ";I*I
30 NEXT I
RUN
```

Statements: `LET` `PRINT` `INPUT` `IF/THEN` `GOTO` `GOSUB` `RETURN`
            `FOR/NEXT` `REM` `END` `LIST` `RUN` `NEW`

Functions: `ABS(x)` `SQR(x)`
