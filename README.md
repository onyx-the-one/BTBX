# BTBX — Bare Tiny(?) BASIC eXecutor
**Version 1.6.32**

BTBX is a self-contained BASIC interpreter that boots directly from a FAT12 floppy image (or virtual floppy/HDD) with no underlying OS. it is the OS. It fits entirely in conventional memory below 640 KB, requires no external runtime, and runs on real hardware or any BIOS-based emulator (QEMU, Bochs, VirtualBox in legacy mode, etc).

It boots in BIOS real mode, switches to 32-bit protected mode, and drops straight into a line-numbered BASIC REPL with file I/O, arrays, sound, and (WIP) graphics. It is closer in spirit to a Commodore/Apple-era machine than to a modern OS.

## Table of contents
- [Features](#features)
- [Quick start](#quick-start)
- [Boot process](#boot-process)
- [Source layout](#source-layout)
- [BASIC language reference](#basic-language-reference)
- [Statements](#statements)
- [Functions](#functions)
- [Graphics](#graphics)
- [Sound](#sound)
- [File I/O](#file-io)
- [Native code (BLOAD / SYS)](#native-code-bload--sys)
- [Error messages](#error-messages)
- [Memory map](#memory-map)
- [Known issues / WIP](#known-issues--wip)
- [Extracting files from the disk image](#extracting-files-from-the-disk-image)
- [Version history](#version-history)
- [Licence](#licence)

## Features
| Area | Details |
|---|---|
| Interpreter | Line-numbered BASIC with GOTO, GOSUB/RETURN, FOR/NEXT, WHILE/WEND, IF/THEN/ELSE/ENDIF (block and inline), ON…GOTO/GOSUB |
| Data types | Integer (32-bit), Float (x87 80-bit extended), String (256-char slots) |
| Math | SIN COS TAN ATN EXP LOG SQR ABS INT FIX SGN RND CINT CDBL, all via x87 FPU |
| Bitwise | AND OR XOR NOT SHL SHR, plus HEX$/BIN$ and &H/&B literals |
| Strings | LEFT$ RIGHT$ MID$ STR$ CHR$ VAL ASC LEN, concatenation with + |
| Arrays | Up to 3 dimensions, numeric and string, DIM required |
| I/O | VGA 80×25 text, PS/2 keyboard, PRINT, INPUT, INKEY$ |
| File I/O | FAT12 LOAD/SAVE/DIR, OPEN/CLOSE/PRINT#/INPUT# |
| Memory | PEEK/POKE (raw 32-bit address) |
| Sound | BEEP (PIT channel 2 square wave), SAY (SAM formant synthesis, 1-bit PWM, WIP) |
| Graphics | SCREEN, PSET, LINE, RECT, CIRCLE, PALETTE, GCLS, POINT — mode 13h path currently semifunctional (faulting BIOS video-mode thunk) |
| Native | BLOAD "file.bin",addr — load a raw flat binary; SYS addr — jump into it |
| Boot | Two-stage loader (stage1 512B + stage2 2KB), BIOS CHS + EDD, A20 gate, real-mode-to-protected-mode handoff |
| Tooling | NASM + i686-elf-gcc cross-compiler + Python mkfat.py |

## Quick start
```sh
# Prerequisites: nasm, i686-elf-gcc (cross-compiler), python3, qemu
make
make run          # QEMU with SDL/GTK window
```

`make` assembles the boot sector and second-stage loader with NASM, compiles the kernel and BASIC interpreter with the i686-elf cross-compiler, links everything at 0x10000, and calls `mkfat.py` to stamp out a 1.44 MB FAT12 floppy image (`btbx.img`) with the kernel binary written to the boot-critical sectors. `make run` boots that image in QEMU.

To boot on real hardware or in another emulator, write `btbx.img` to a floppy or USB stick and boot from it in legacy BIOS mode (no UEFI).

## Boot process
1. **stage1.asm** (512 bytes) — the MBR/VBR boot sector. BIOS loads this at `0x7C00` and jumps to it. It sets up a stack, reads stage2 from disk using BIOS CHS/EDD, and jumps to it.
2. **stage2.asm** (2 KB) — enables the A20 line, sets up a GDT, switches the CPU into 32-bit protected mode, loads the kernel image from disk, and jumps into the kernel entry point.
3. **entry.asm** — the 32-bit kernel entry. Copies the `thunk16` real-mode trampoline blob to `0x7100` (needed so 32-bit code can still call BIOS interrupts for disk I/O), sets up the C runtime stack, and calls `kernel_main`.
4. **kernel.c** — brings up the VGA text terminal and PS/2 keyboard, prints the boot banner, and hands off to the BASIC interpreter's REPL loop.
5. **basic.c** — the interpreter itself. From here on, everything is BASIC.

The disk-access real-mode thunk (`thunk16.asm`, running at `0x7100`) is what lets 32-bit protected-mode code still issue BIOS `INT 13h` disk calls — it briefly drops back to real mode, does the BIOS call, and returns to protected mode. This is why FAT12 LOAD/SAVE/BLOAD all work reliably even though the kernel itself runs in 32-bit mode. Video mode switching does **not** have an equivalent thunk implemented yet — see [Known issues](#known-issues--wip).

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
│   ├── stmt/
│   │   ├── core.inc        startup, DIM, POKE, LET/assignment
│   │   ├── console.inc     PRINT, INPUT, DATA, READ, RESTORE
│   │   ├── flow.inc        GOTO/GOSUB/RETURN, FOR/NEXT, WHILE/WEND, IF/ELSE/ENDIF, ON
│   │   ├── file.inc        OPEN/CLOSE, PRINT#/INPUT#, DIR, LOAD, SAVE
│   │   ├── graphics.inc    SCREEN, PSET, LINE, RECT, CIRCLE, PALETTE, GCLS
│   │   └── system.inc      BEEP, SAY, BLOAD, SYS, HELP, fallback
│   ├── fs/
│   │   ├── fat12.h
│   │   ├── fat12.c         FAT12 read/write via real-mode BIOS thunk
│   │   └── thunk16.asm     Real-mode BIOS trampoline at 0x7100
│   ├── gfx/
│   │   ├── gfx.h
│   │   └── gfx.c           VGA mode 13h primitives (pixel, line, rect, circle)
│   └── sound/
│       ├── sound.h
│       └── sound.c         BEEP (PIT ch2) + SAM speech (WIP) (1-bit PWM)
├── linker.ld               Kernel linked at 0x10000
├── mkfat.py                Builds btbx.img (1.44 MB FAT12)
├── Makefile
├── LICENSE                 MIT
└── README.md
```

The statement dispatcher in `basic.c` is assembled from the `.inc` fragments above via `#include`, in the order `core.inc → flow.inc → console.inc → file.inc → graphics.inc → system.inc`. **Include order is semantically significant** — earlier fragments can shadow keywords that later fragments also try to handle (this bit us once with `PRINT#`/`INPUT#`, now fixed). Anyone adding a new statement keyword should check for collisions across all `.inc` files before picking where it lives.

## BASIC language reference

### Program structure
Every line starts with a line number. Line numbers can be entered in any order — BTBX keeps the program sorted internally.

```basic
10 PRINT "Hello, world!"
20 FOR I = 1 TO 10
30   PRINT I
40 NEXT I
50 END
```

Commands typed without a leading line number execute immediately (direct mode) instead of being stored.

### Data types
- **Integer** — 32-bit signed.
- **Float** — x87 80-bit extended precision, used automatically for any expression involving a decimal point, or the result of SIN/COS/SQR/etc.
- **String** — up to 256 characters per slot, denoted with a trailing `$` on variable and function names (e.g. `N$`, `LEFT$`).

Type is inferred by the trailing `$` on the variable name — there's no separate DIM-time type declaration beyond that.

## Statements
| Statement | Syntax | Notes |
|---|---|---|
| PRINT | `PRINT expr, expr, ...` | Comma-separated list; trailing `;` suppresses the newline (console form) |
| PRINT# | `PRINT #n, expr, ...` | Writes to open file channel n |
| INPUT | `INPUT "prompt", var` | Console input |
| INPUT# | `INPUT #n, var` | Reads from open file channel n |
| LET | `LET var = expr` or bare `var = expr` | Both forms are independent code paths — see the note in Source layout |
| DIM | `DIM name(d1)`, `DIM name(d1,d2)`, `DIM name(d1,d2,d3)` | Up to 3 dimensions, numeric or string (`name$`) |
| REM | `REM comment` | Full-line comment |
| IF | Inline: `IF expr THEN stmt` — Block: `IF expr THEN ... ELSE ... ENDIF` | Inline form does nothing on false; block form requires ENDIF |
| GOTO | `GOTO n` | Jump to line n |
| GOSUB / RETURN | `GOSUB n` ... `RETURN` | Subroutine call/return by line number |
| ON | `ON expr GOTO n1,n2,...` or `ON expr GOSUB n1,n2,...` | Computed branch |
| FOR / NEXT | `FOR v = start TO end [STEP s]` ... `NEXT v` | NEXT searches the FOR stack by variable name, so GOTO out of a nested loop is safe |
| WHILE / WEND | `WHILE expr` ... `WEND` | |
| DATA / READ / RESTORE | `DATA v1,v2,...` / `READ var` / `RESTORE` | Classic DATA-statement queue |
| POKE | `POKE addr, value` | Raw memory write |
| OPEN / CLOSE | `OPEN "file" FOR INPUT|OUTPUT AS n` / `CLOSE n` | FAT12-backed file channel |
| SAVE / LOAD | `SAVE "prog.bas"` / `LOAD "prog.bas"` | Save/load the current program |
| DIR | `DIR` | List files on disk |
| NEW | `NEW` | Clear the current program from memory |
| RUN | `RUN` | Execute the current program from the first line |
| LIST | `LIST` | Print the current program |
| END | `END` | Stop execution |
| HALT | `HALT` | Stop execution (alias) |
| CLEAR | `CLEAR` | Reset variables |
| BEEP | `BEEP freq, ms` | PIT square-wave tone |
| SAY | `SAY strexpr` | SAM formant speech synth — WIP, currently non-functional |
| SCREEN | `SCREEN mode` | Switch video mode — currently broken, see Known issues |
| PSET | `PSET x, y, c` | Plot a pixel |
| LINE | `LINE x0,y0,x1,y1,c` | Draw a line |
| RECT | `RECT x0,y0,x1,y1,c[,F]` | Draw a rectangle; `F` fills it |
| CIRCLE | `CIRCLE x,y,r,c[,F]` | Draw a circle; `F` fills it |
| PALETTE | `PALETTE i,r,g,b` | Set a VGA palette entry |
| GCLS | `GCLS c` | Clear the graphics screen to colour c |
| BLOAD | `BLOAD "file.bin", addr` | Load a raw binary into memory |
| SYS | `SYS addr` | Jump into machine code at addr |
| HELP | `HELP` | Print the built-in command reference |

## Functions

### Math
`SIN COS TAN ATN EXP LOG SQR ABS INT FIX SGN RND CINT CDBL`

### Bitwise / numeric base
`AND OR XOR NOT SHL SHR` (as binary/unary operators), `HEX$(n)`, `BIN$(n)`, and literals `&Hff`, `&B1010`

### Strings
`LEFT$(s,n) RIGHT$(s,n) MID$(s,start[,len]) STR$(n) CHR$(n) VAL(s$) ASC(s$) LEN(s$)`

### Misc
`PEEK(addr)`, `POINT(x,y)` (reads a pixel), `INKEY$` (non-blocking single-key read, returns empty string if nothing pending), `DATE$`, `TIME$`

## Graphics
BTBX targets VGA mode 13h (320×200, 256 colours) with a small primitive set: `PSET`, `LINE`, `RECT`, `CIRCLE`, `PALETTE`, `GCLS`, and the `POINT()` read-back function, all implemented in `gfx.c`/`gfx.h` and exposed via `graphics.inc`.

**Currently broken:** `SCREEN mode` calls `bios_set_video_mode()`, which needs a real-mode BIOS `INT 10h` thunk analogous to the disk-I/O thunk in `thunk16.asm`. That thunk does not exist yet, so mode switching silently does nothing (or hangs, depending on emulator). Until it's implemented, don't rely on `SCREEN`/graphics commands in shipped BASIC programs — write text-mode-only programs instead, using direct VGA text-buffer access (`0xB8000`) and BIOS keyboard ports if you need something snappier than PRINT (see the demo game for an example of this approach via BLOAD/SYS).

## Sound
`BEEP freq, ms` drives PIT channel 2 as a PC-speaker square wave and is fully working. `SAY strexpr` is a port of the SAM (Software Automatic Mouth) formant synthesizer feeding a 1-bit PWM buffer through PIT channel 0 — it's included but currently does not produce correct output; treat it as WIP.

## File I/O
FAT12 read/write goes through the real-mode BIOS disk thunk, so it works identically whether BTBX is running under QEMU or on real hardware booted from the same image.

```basic
OPEN "DATA.TXT" FOR OUTPUT AS 1
PRINT #1, "hello"
CLOSE 1

OPEN "DATA.TXT" FOR INPUT AS 1
INPUT #1, A$
CLOSE 1
```

`SAVE`/`LOAD` persist the current BASIC program itself to/from disk. `DIR` lists the FAT12 root directory.

## Native code (BLOAD / SYS)
BTBX supports a classic home-computer workflow: drop a flat binary on the disk, load it into memory, and jump into it directly from BASIC.

```basic
BLOAD "DEMO.BIN", 131072
SYS 131072
```

`BLOAD` parses the filename, converts it to FAT 8.3 format, and loads it directly to the given physical address via the FAT12 driver. The load ceiling is `0x7F000` (BLOAD refuses to load past this, to stay clear of VGA memory at `0xA0000`). `SYS` evaluates its address expression, casts it to a `void (*)(void)`, and calls it — execution transfers directly into your machine code with no sandboxing whatsoever.

Requirements for a binary to work correctly with SYS:
- It must be a flat raw binary (`objcopy -O binary`), not an ELF file.
- Its true entry point must be the very first byte at the load address — control the section order explicitly in your linker script (e.g. a dedicated `.text.start` section placed before generic `.text`), since compilers don't guarantee function emission order.
- It should not rely on a zeroed `.bss` — nothing clears memory before `SYS` jumps in. Avoid static/global variables that expect zero-initialization, or zero them yourself at the top of your entry function.
- If it returns instead of halting, control falls back into the BASIC REPL, which will look like BASIC printing a blank prompt line out of nowhere. If you don't want that, put an infinite `hlt` loop at the end of your entry function.
- 0x20000 is a reasonable scratch load address given the current kernel layout (kernel lives at 0x10000, BLOAD's ceiling is 0x7F000).

## Error messages
BTBX reports errors in light red text on the console and halts execution of the current program. Errors you may encounter:

| Error | Meaning |
|---|---|
| SYNTAX | Parser couldn't make sense of the statement |
| MEM | Out of string pool / variable table / program line slots |
| DIV0 | Division by zero |
| SUBSCRIPT | Array index out of bounds |
| TYPE MISMATCH | Mixed numeric/string operation where it isn't allowed |
| MATH | Invalid argument to a math function (e.g. SQR of a negative number, LOG of zero) |
| RTC | Real-time clock read failure |
| FILE NOT FOUND | BLOAD/LOAD target doesn't exist on disk |
| DISK NOT READY | FAT12 driver couldn't access the disk |
| UNKNOWN FUNCTION | Called a function name the interpreter doesn't recognize |

## Memory map
| Range | Contents |
|---|---|
| `0x7C00` | stage1 boot sector (loaded here by BIOS) |
| `0x7100` | thunk16 real-mode BIOS trampoline |
| `0x10000` | Kernel entry point (linked address, see linker.ld) |
| `0x20000` | Suggested scratch address for BLOAD/SYS user binaries |
| `0x7F000` | BLOAD load ceiling |
| `0xA0000` | VGA graphics memory (mode 13h framebuffer) |
| `0xB8000` | VGA text-mode buffer (80×25, 16-bit cells: attribute + char) |

## Known issues / WIP
- `SCREEN`/graphics commands don't work — no BIOS video-mode real-mode thunk exists yet, only the disk-I/O thunk is implemented. This is the top priority for the graphics subsystem.
- `SAY` (SAM speech synth) is ported but not producing correct audio output yet.
- The statement dispatcher is a long ordered chain of keyword checks across included `.inc` fragments rather than a structured dispatch table — include order matters, and new keywords must be checked against all fragments for shadowing conflicts.
- No timer/IRQ-driven scheduling — BEEP/delay loops that need real elapsed time should use the PIT-tick technique in `sound.c`'s `delaymsuint32t` rather than a CPU spin loop, since spin loops scale with CPU/emulator speed and are not portable.
- No native floppy controller driver — all disk access goes through the BIOS real-mode thunk, so it won't work in an environment without BIOS INT 13h (e.g. pure UEFI with CSM disabled).

## Extracting files from the disk image
`mkfat.py` builds a standard FAT12 floppy filesystem, so files written by BTBX (or files you want to inject before boot, like a compiled BLOAD binary) can be read/written with `mtools` without booting the image at all.

```sh
# list contents
mdir -i btbx.img

# copy a file out
mcopy -i btbx.img PRIMES.TXT ./primes_result.txt

# copy a file in
mcopy -i btbx.img PONG.BIN ::PONG.BIN
```

Because this is a raw floppy filesystem and not a partitioned disk image, always pass `-i` to point mtools at the image directly. If mtools complains about geometry, set `MTOOLS_SKIP_CHECK=1` before the command.

## Version history
| Version | Notes |
|---|---|
| 1.6.32 | BLOAD / SYS commands; SAY via SAM formant synth (WIP, not working yet); full file I/O; arrays; multi-dim; EDD disk; clean tree; interpreter stress-tested against a 1,000,000-limit prime sieve (correct result: 78,498 primes, largest 999,983); fixed bare-assignment parsing, numeric IF comparisons, NEXT-after-GOTO frame corruption, inline-IF false-branch handling, OPEN keyword-chaining whitespace bug, PRINT#/INPUT# dispatch-order conflict |
| 1.5.x | SAY, BEEP, FAT12 r/w, file I/O channels |
| 1.0.x | Initial bare-metal BASIC, single-stage loader |

## Licence
MIT — see `LICENSE`.
