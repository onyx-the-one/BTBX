/*
 * BTBX kernel.c
 * VGA text-mode terminal, PS/2 keyboard, PIC init, kernel entry
 */
#include <stdint.h>
#include <stddef.h>
#include "basic.h"

/* ======================================================================
 * I/O port helpers
 * ====================================================================== */
static inline void outb(uint16_t port, uint8_t val) {
	    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
	    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void io_wait(void) { outb(0x80, 0); }

/* ======================================================================
 * PIC – remap IRQs to 0x20-0x2F so they don't clash with CPU exceptions
 * ====================================================================== */
#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

static void pic_remap(void) {
	    uint8_t m1 = inb(PIC1_DATA), m2 = inb(PIC2_DATA);
    outb(PIC1_CMD,  0x11); io_wait();
    outb(PIC2_CMD,  0x11); io_wait();
    outb(PIC1_DATA, 0x20); io_wait(); /* master base = 0x20 */
    outb(PIC2_DATA, 0x28); io_wait(); /* slave  base = 0x28 */
    outb(PIC1_DATA, 0x04); io_wait(); /* master: slave at IRQ2 */
    outb(PIC2_DATA, 0x02); io_wait(); /* slave:  cascade id */
    outb(PIC1_DATA, 0x01); io_wait();
    outb(PIC2_DATA, 0x01); io_wait();
    outb(PIC1_DATA, m1);
    outb(PIC2_DATA, m2);
}

/* ======================================================================
 * VGA text-mode terminal  80×25
 * ====================================================================== */
#define VGA_COLS 80
#define VGA_ROWS 25
#define VGA_BUF  ((volatile uint16_t *)0xB8000)

static int  term_col  = 0;
static int  term_row  = 0;
static uint8_t term_attr = 0x00; /* set in term_init */

static uint16_t vga_entry(char c, uint8_t attr) {
	    return (uint16_t)c | ((uint16_t)attr << 8);
}

static void vga_update_cursor(void) {
	    uint16_t pos = (uint16_t)(term_row * VGA_COLS + term_col);
    outb(0x3D4, 0x0F); outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E); outb(0x3D5, (uint8_t)(pos >> 8));
}

void term_set_color(uint8_t fg, uint8_t bg) {
	    term_attr = (uint8_t)((bg << 4) | (fg & 0x0F));
}

void term_clear(void) {
	    for (int i = 0; i < VGA_ROWS * VGA_COLS; i++)
        VGA_BUF[i] = vga_entry(' ', term_attr);
    term_col = 0; term_row = 0;
    vga_update_cursor();
}

static void term_scroll(void) {
	    for (int r = 1; r < VGA_ROWS; r++)
        for (int c = 0; c < VGA_COLS; c++)
            VGA_BUF[(r-1)*VGA_COLS + c] = VGA_BUF[r*VGA_COLS + c];
    for (int c = 0; c < VGA_COLS; c++)
        VGA_BUF[(VGA_ROWS-1)*VGA_COLS + c] = vga_entry(' ', term_attr);
    term_row = VGA_ROWS - 1;
}

void term_putchar(char c) {
	    if (c == '\n') {
	        term_col = 0;
        if (++term_row >= VGA_ROWS) term_scroll();
    } else if (c == '\r') {
	        term_col = 0;
    } else if (c == '\b') {
	        if (term_col > 0) {
	            term_col--;
            VGA_BUF[term_row * VGA_COLS + term_col] = vga_entry(' ', term_attr);
        }
    } else {
	        VGA_BUF[term_row * VGA_COLS + term_col] = vga_entry(c, term_attr);
        if (++term_col >= VGA_COLS) {
	            term_col = 0;
            if (++term_row >= VGA_ROWS) term_scroll();
        }
    }
    vga_update_cursor();
}

void term_puts(const char *s) {
	    while (*s) term_putchar(*s++);
}

void term_puti(int32_t n) {
	    char buf[16];
    int  i = 0;
    if (n < 0) { term_putchar('-'); n = -n; }
    if (n == 0) { term_putchar('0'); return; }
    while (n) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (i--) term_putchar(buf[i]);
}

/* ======================================================================
 * PS/2 Keyboard – scancode set 1, IRQ1 polling
 * ====================================================================== */
#define KB_DATA 0x60
#define KB_STAT 0x64

/* US QWERTY scancode → ASCII (unshifted, shifted) */
static const char sc_ascii[2][128] = {
	/* unshifted */
{   0,  27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,   'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,   '\\','z','x','c','v','b','n','m',',','.','/', 0,
    '*', 0,  ' ', 0,
    0,0,0,0,0,0,0,0,0,0,  /* F1-F10 */
    0,0,  /* num lock, scroll lock */
    '7','8','9','-','4','5','6','+','1','2','3','0','.', /* keypad */
    0,0,0,
    0,0  /* F11, F12 */
},
/* shifted */
{   0,  27, '!','@','#','$','%','^','&','*','(',')','_','+', '\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,   'A','S','D','F','G','H','J','K','L',':','"','~',
    0,   '|','Z','X','C','V','B','N','M','<','>','?', 0,
    '*', 0,  ' ', 0,
    0,0,0,0,0,0,0,0,0,0,
    0,0,
    '7','8','9','-','4','5','6','+','1','2','3','0','.',
    0,0,0,
    0,0
}
};

static int  kb_shift  = 0;
static int  kb_caps   = 0;

/* Read one byte from keyboard (busy-poll) */
static uint8_t kb_read_raw(void) {
	    while (!(inb(KB_STAT) & 0x01)) { /* spin */ }
    return inb(KB_DATA);
}

int term_getchar(void) {
	    while (1) {
	        uint8_t sc = kb_read_raw();
        if (sc & 0x80) {
	            /* key release */
            sc &= 0x7F;
            if (sc == 0x2A || sc == 0x36) kb_shift = 0;
            continue;
        }
        /* key press */
        if (sc == 0x2A || sc == 0x36) { kb_shift = 1; continue; }
        if (sc == 0x3A) { kb_caps ^= 1; continue; }  /* caps lock toggle */
        if (sc >= 128) continue;

        int use_shift = kb_shift;
        char c = sc_ascii[use_shift][sc];
        if (c == 0) continue;

        /* Caps lock only flips letters */
        if (kb_caps && c >= 'a' && c <= 'z') c -= 32;
        else if (kb_caps && c >= 'A' && c <= 'Z') c += 32;

        return (int)(unsigned char)c;
    }
}

void term_get_line(char *buf, int maxlen) {
	    int i = 0;
    while (1) {
	        int c = term_getchar();
        if (c == '\n' || c == '\r') {
	            term_putchar('\n');
            buf[i] = '\0';
            return;
        }
        if (c == '\b') {
	            if (i > 0) {
	                i--;
                term_putchar('\b');
            }
            continue;
        }
        if (i < maxlen - 1) {
	            buf[i++] = (char)c;
            term_putchar((char)c);
        }
    }
}

/* ======================================================================
 * Kernel entry
 * ====================================================================== */

/* Simple string helpers (no libc) */
static int kstrlen(const char *s) {
	    int n = 0; while (*s++) n++; return n;
}

/* Draw a centered banner line in a given color */
static void banner_line(const char *s, uint8_t fg, uint8_t bg) {
	    term_set_color(fg, bg);
    int len = kstrlen(s);
    int pad = (VGA_COLS - len) / 2;
    for (int i = 0; i < pad; i++) term_putchar(' ');
    term_puts(s);
    for (int i = 0; i < VGA_COLS - pad - len; i++) term_putchar(' ');
}

void kernel_main(uint32_t magic, void *mbi) {
	    (void)magic; (void)mbi;

    pic_remap();

    /* --- splash screen ------------------------------------------------ */
    term_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    term_clear();

    /* Top decorative bar */
    banner_line("", VGA_BLACK, VGA_CYAN);
    term_putchar('\n');
    banner_line("  BTBX  --  Bare Terminal BASIC eXecutor  ", VGA_WHITE, VGA_BLUE);
    term_putchar('\n');
    banner_line("", VGA_BLACK, VGA_CYAN);
    term_putchar('\n');

    term_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    term_puts("\n");
    term_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    term_puts("  Version 1.0   32-bit Protected Mode\n\n");
    term_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    term_puts("  VARIABLES: A-Z    STATEMENTS: LET IF GOTO GOSUB RETURN FOR NEXT\n");
    term_puts("             PRINT INPUT REM LIST RUN NEW END\n");
    term_puts("  FUNCTIONS: ABS() SQR()    OPERATORS: + - * / ( )\n\n");
    term_set_color(VGA_DARK_GREY, VGA_BLACK);
    term_puts("  Type a line number + statement to store, or a statement to run.\n");
    term_puts("  Type LIST to view the program, RUN to execute, NEW to clear.\n\n");

    /* Divider */
    term_set_color(VGA_CYAN, VGA_BLACK);
    for (int i = 0; i < VGA_COLS; i++) term_putchar(196); /* box-drawing → */
    term_putchar('\n');

    term_set_color(VGA_LIGHT_GREY, VGA_BLACK);

    /* Hand off to BASIC interpreter (never returns) */
    basic_run();

    /* Unreachable */
    for (;;) { __asm__ volatile ("hlt"); }
}
