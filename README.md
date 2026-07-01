# BTBX - Bare Tiny(?) BASIC eXecutor

Boots on bare-metal x86 and presents a TinyBASIC interpreter.

## Requirements

- nasm
- i686-elf-gcc, i686-elf-ld, i686-elf-objcopy
- dd, qemu-system-i386

## Build

    make          # 1.44 MB floppy image
    make run      # test in QEMU as floppy
    make run-hd   # test in QEMU as hard disk
    make clean

## Boot on real hardware

    sudo dd if=btbx.img of=/dev/sd?? bs=512

Enable legacy/CSM BIOS boot on UEFI machines.

## Boot sequence

| Char   | Meaning                    |
|--------|----------------------------|
| S1     | Stage 1 MBR running        |
|        | Stage 1 jumped to Stage 2  |
| S2     | Stage 2 running            |
| A      | A20 line enabled           |
| K      | Kernel loaded from disk    |
| banner | Kernel C code running      |

If it stops at S1 and shows !: disk read in stage 1 failed.
On screen: S1->S2->A->K then the BTBX banner.

## Disk image layout

| Sectors | Content              |
|---------|----------------------|
| 0       | stage1 MBR, 512 B    |
| 1-4     | stage2 loader        |
| 5+      | kernel flat binary   |

## BASIC reference

### Variables

- A-Z, arbitrary names up to 31 chars (integer or float)
- Append $ for string variables: A$, NAME$

### Statements

    LET var = expr          bare assignment also works: A = 5
    PRINT expr [; | ,] ...  semicolon = no newline, comma = tab
    ? expr ...              alias for PRINT
    INPUT ["prompt",] var [, var ...]
    IF cond THEN line|stmt
    IF cond THEN / ELSE / ENDIF  (multi-line form)
    FOR var = from TO to [STEP n]
    NEXT [var]
    WHILE cond / WEND
    GOTO line
    GOSUB line / RETURN
    DATA val [, val ...]
    READ var [, var ...]
    RESTORE
    REM comment  (or ' comment)
    END / NEW / LIST / RUN
    CLEAR    redraw banner
    HALT     freeze (CLI + HLT)

### Arrays

    DIM name(n)            1-D, indices 0..n
    DIM name(n, m)         2-D, indices 0..n, 0..m
    DIM name(n, m, p)      3-D
    DIM A$(10)             string array
    A(3) = 42
    PRINT A(3)

Up to 32 arrays, 512 total elements across all arrays.

### Memory access (inline-assembly backed)

    PEEK(addr)      returns byte at 32-bit physical address
    POKE addr, val  writes byte to 32-bit physical address

VGA text buffer at 0xB8000. Each cell = 2 bytes (ASCII low, attribute high).

    10 POKE 0xB8000, 65    ' write 'A'
    20 POKE 0xB8001, 0x1F  ' bright white on blue

### Non-blocking keyboard

    INKEY$    one-char string if a key is waiting, "" if none

    10 K$ = INKEY$
    20 IF K$ = "" THEN GOTO 10
    30 PRINT "You pressed: "; K$

### Data file I/O

Files are stored on the FAT12 filesystem alongside the kernel.
File numbers 1-4. Output buffers (8 KB each) are written to disk on CLOSE.

    OPEN "file.ext" FOR INPUT  AS #n
    OPEN "file.ext" FOR OUTPUT AS #n
    CLOSE [#n]              omit #n to close all open files
    PRINT #n, expr [;|,] ...
    INPUT #n, var [, var ...]

Write example:

    10 OPEN "DATA.TXT" FOR OUTPUT AS #1
    20 FOR I = 1 TO 5 : PRINT #1, I; ","; I*I : NEXT I
    30 CLOSE #1

Read example:

    10 OPEN "DATA.TXT" FOR INPUT AS #1
    20 INPUT #1, N, SQ
    30 PRINT N; " squared = "; SQ
    40 IF N < 5 THEN GOTO 20
    50 CLOSE #1

### Program file I/O

    LOAD "file.bas"
    SAVE "file.bas"
    DIR

    mcopy -i btbx.img hello.bas ::HELLO.BAS

### Functions

| Function        | Description                              |
|-----------------|------------------------------------------|
| ABS(x)          | absolute value                           |
| SQR(x)          | square root                              |
| SIN(x)          | sine (radians)                           |
| COS(x)          | cosine                                   |
| TAN(x)          | tangent                                  |
| ATN(x)          | arctangent                               |
| EXP(x)          | e^x                                      |
| LOG(x)          | natural log                              |
| INT(x)          | floor (toward -inf)                      |
| FIX(x)          | truncate toward zero                     |
| SGN(x)          | -1 / 0 / 1                               |
| RND             | random float 0-1                         |
| CINT(x)         | round to integer                         |
| CDBL(x)         | convert to float                         |
| MOD(a,b)        | integer modulo                           |
| PEEK(addr)      | read byte from physical address          |
| LEN(s$)         | string length                            |
| CHR$(n)         | character from ASCII code                |
| ASC(s$)         | ASCII code of first character            |
| STR$(n)         | number to string                         |
| VAL(s$)         | string to number                         |
| LEFT$(s$,n)     | left n characters                        |
| RIGHT$(s$,n)    | right n characters                       |
| MID$(s$,p[,n])  | substring from position p (1-based)      |
| INKEY$          | non-blocking key read; "" if no key      |
