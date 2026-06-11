# BTBX (Bare TinyBASIC eXecutor)

32-bit protected mode x86 BASIC interpreter booting directly from a 1.44MB FAT12 floppy image. 

## Build

Requires `nasm`, `i686-elf-gcc`, `mtools`, `qemu-system-i386`.

```bash
make         # builds btbx.img (1.44MB FAT12 floppy)
make run     # boot floppy in QEMU
make clean
```

For real hardware, dump to a USB drive (legacy/CSM BIOS boot required):
```bash
sudo dd if=btbx.img of=/dev/sdX bs=512 status=progress
```

Copying sources into the image:
```bash
mcopy -i btbx.img hello.bas ::HELLO.BAS
```

## Boot Sequence

POST outputs raw characters to VGA before entering protected mode. 

| Output | Meaning |
|---|---|
| `S1` | Stage 1 (MBR) initialized. |
| `>` | Jump to Stage 2 successful. |
| `S2` | Stage 2 running. |
| `A` | A20 line enabled. |
| `K` | Kernel flat binary loaded from disk. |
| `!` | Halts. Indicates a disk read failure at the preceding step. |

## Disk Layout (FAT12 Floppy)

Managed via `mkfat.py` and `fat12.c`. Must match exactly. 

- LBA 0: Boot sector (stage1)
- LBA 1-4: Stage2 loader (2048 bytes)
- LBA 5-13: FAT1
- LBA 14-22: FAT2
- LBA 23-36: Root Directory (224 entries)
- LBA 37+: Data clusters (Kernel loaded here, followed by BASIC payload files)

*Note: INT 13h operations bounce through physical 0x0800 to avoid crossing 64KB physical DMA boundaries.*

## BTBX Language Spec

Implementation in `basic.c`.

**Types:** 
Int32, Double (x87 FPU), Strings (64-slot pool, 128 chars max).
Variables: `A-Z`, `varname`, `string$` (up to 32 chars). Global scope.

**Statements:**
`LET`, `PRINT`, `INPUT`, `IF/THEN[/ELSE/ENDIF]`, `GOTO`, `GOSUB`, `RETURN`, `FOR/TO/STEP/NEXT`, `WHILE/WEND`, `REM`, `HALT`, `CLEAR`, `LIST`, `RUN`, `NEW`

**File I/O:**
- `DIR`: Lists root directory.
- `LOAD "FILE.BAS"`: Loads into program store.
- `SAVE "FILE.BAS"`: Serializes program store to disk.

**Built-in Functions:**
- Math: `SIN`, `COS`, `TAN`, `ATN`, `EXP`, `LOG`, `SQR`, `ABS`, `INT`, `FIX`, `SGN`, `RND`, `MOD`
- Casts: `CINT`, `CDBL`, `STR$`, `VAL`
- String: `LEFT$`, `RIGHT$`, `MID$`, `CHR$`, `ASC`, `LEN`
