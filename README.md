# BTBX — Bare Tiny(?) BASIC eXecutor
**Version 1.6.32**

BTBX is a self-contained BASIC interpreter that boots directly from a FAT12
floppy image (or virtual floppy) with no underlying OS (it is the OS).  It fits entirely in
the conventional memory below 640 KB and requires no external runtime.

## Features
| Area | Details |
|---|---|
| Interpreter | Line-numbered BASIC with GOTO, GOSUB, FOR/NEXT, WHILE/WEND, IF/THEN/ELSE/ENDIF, ON…GOTO/GOSUB |
| Data types | Integer (32-bit), Float (x87 80-bit extended), String (256-char slots) |
| Math | SIN COS TAN ATN EXP LOG SQR ABS INT FIX SGN RND (via x87 FPU) |
| Strings | LEFT$ RIGHT$ MID$ STR$ CHR$ VAL ASC LEN concatenation |
| Arrays | Up to 3-D, numeric and string, DIM required |
| I/O | VGA 80×25 text, PS/2 keyboard, PRINT, INPUT, INKEY$ |
| File I/O | FAT12 LOAD/SAVE/DIR, OPEN/CLOSE/PRINT#/INPUT# |
| Memory | PEEK/POKE (raw 32-bit address) |
| Sound | BEEP (PIT square wave), SAY (SAM formant synthesis, 1-bit PWM) |
| Native | BLOAD "file.bin",addr — load a raw binary; SYS addr — call machine code |
| Boot | Two-stage loader (stage1 512B + stage2 2KB), BIOS CHS + EDD, A20 gate |
| Tooling | NASM + i686-elf-gcc + Python mkfat.py |

## Quick start
```sh
# Prerequisites: nasm, i686-elf-gcc (cross-compiler), python3, qemu
make
make run          # QEMU with SDL/GTK window
```

## Source layout
```
btbx/
├── boot/
│   ├── stage1.asm          512-byte MBR/VBR boot sector
│   └── stage2.asm          2 KB second-stage loader (pmode, EDD, A20)
├── src/kernel/
│   ├── entry.asm           32-bit kernel entry; copies thunk16 blob
│   ├── basic.h             API (VGA colours, terminal, kernel)
│   ├── basic.c             BASIC interpreter (lexer, parser, evaluator, REPL)
│   ├── kernel.c            VGA terminal, PS/2 keyboard, banner, kernel_main
│   ├── fs/
│   │   ├── fat12.h
│   │   ├── fat12.c         FAT12 read/write via real-mode BIOS thunk
│   │   └── thunk16.asm     Real-mode BIOS trampoline at 0x7100
│   └── sound/
│       ├── sound.h
│       └── sound.c         BEEP (PIT ch2) + SAM speech (WIP) (1-bit PWM)
├── linker.ld               Kernel linked at 0x10000
├── mkfat.py                Builds btbx.img (1.44 MB FAT12)
├── Makefile
├── LICENSE                 MIT
└── README.md
```

## BTBX BASIC quick reference
```basic
PRINT "Hello, world!"        SAY "HELLO"
INPUT "Name? ", N$           BEEP 440, 500
FOR I = 1 TO 10              BLOAD "DEMO.BIN", 131072
  PRINT I                    SYS 131072
NEXT I
SAVE "PROG.BAS"              DIR
LOAD "PROG.BAS"              RUN
```

## Version history
| Version | Notes |
|---|---|
| 1.6.32 | BLOAD / SYS commands; SAY via SAM formant synth (actually it doesnt work yet its WIP); full file I/O; arrays; multi-dim; EDD disk; clean tree |
| 1.5.x | SAY, BEEP, FAT12 r/w, file I/O channels |
| 1.0.x | Initial bare-metal BASIC, single-stage loader |

## Licence
MIT — see `LICENSE`.
