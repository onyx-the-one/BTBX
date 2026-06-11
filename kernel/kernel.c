#include "basic.h"
#include "fat12.h"

/* ── I/O helpers ─────────────────────────────────────────────────────── */
static inline void outb(uint16_t p, uint8_t v)
    { __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p)); }
static inline uint8_t inb(uint16_t p)
    { uint8_t v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline void iowait(void) { outb(0x80,0); }

/* ── PIC remap: IRQ0-7 → INT 0x20-0x27, IRQ8-15 → INT 0x28-0x2F ──── */
static void pic_remap(void) {
    uint8_t a = inb(0x21), b = inb(0xA1);
    outb(0x20,0x11); iowait(); outb(0xA0,0x11); iowait();
    outb(0x21,0x20); iowait(); outb(0xA1,0x28); iowait();
    outb(0x21,0x04); iowait(); outb(0xA1,0x02); iowait();
    outb(0x21,0x01); iowait(); outb(0xA1,0x01); iowait();
    outb(0x21, a);   outb(0xA1, b);
    /* mask all IRQs — we don't have an IDT */
    outb(0x21,0xFF); outb(0xA1,0xFF);
}

/* ── VGA text terminal ───────────────────────────────────────────────── */
#define COLS 80
#define ROWS 25
#define VGA  ((volatile uint16_t *)0xB8000)

static int     col = 0, row = 0;
static uint8_t attr = 0x07;

static void cur(void) {
    uint16_t pos = (uint16_t)(row * COLS + col);
    outb(0x3D4, 0x0F); outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E); outb(0x3D5, (uint8_t)(pos >> 8));
}

void term_set_color(uint8_t fg, uint8_t bg) {
    attr = (uint8_t)((bg << 4) | (fg & 0xF));
}

void term_clear(void) {
    uint16_t blank = (uint16_t)(' ' | (attr << 8));
    for (int i = 0; i < COLS * ROWS; i++) VGA[i] = blank;
    col = row = 0;
    cur();
}

static void scroll(void) {
    for (int r = 1; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            VGA[(r-1)*COLS+c] = VGA[r*COLS+c];
    uint16_t blank = (uint16_t)(' ' | (attr << 8));
    for (int c = 0; c < COLS; c++) VGA[(ROWS-1)*COLS+c] = blank;
    row = ROWS - 1;
}

void term_putchar(char c) {
    if (c == '\n') {
        col = 0;
        if (++row >= ROWS) scroll();
    } else if (c == '\r') {
        col = 0;
    } else if (c == '\b') {
        if (col > 0) {
            col--;
            VGA[row*COLS+col] = (uint16_t)' ' | (uint16_t)(attr << 8);
        }
    } else {
        VGA[row*COLS+col] = (uint16_t)(unsigned char)c | (uint16_t)(attr << 8);
        if (++col >= COLS) { col = 0; if (++row >= ROWS) scroll(); }
    }
}

void term_puts(const char *s)  { while (*s) term_putchar(*s++); }

void term_puti(int32_t n) {
    char buf[12]; int i = 0;
    if (n < 0) { term_putchar('-'); n = -n; }
    if (!n)    { term_putchar('0'); return; }
    while (n)  { buf[i++] = '0' + (int)(n % 10); n /= 10; }
    while (i--) term_putchar(buf[i]);
}

void term_putf(double f) {
    if (f < 0.0) { term_putchar('-'); f = -f; }
    if (f != 0.0 && (f >= 1e10 || f < 1e-4)) {
        int exp = 0;
        while (f >= 10.0) { f /= 10.0; exp++; }
        while (f <  1.0)  { f *= 10.0; exp--; }
        term_putf(f);
        term_puts("E");
        if (exp < 0) { term_putchar('-'); exp = -exp; }
        term_puti((int32_t)exp);
        return;
    }
    int32_t ipart = (int32_t)f;
    double  fpart = f - (double)ipart;
    term_puti(ipart);
    char buf[10]; int n = 0;
    for (int i = 0; i < 8; i++) {
        fpart *= 10.0;
        int d = (int)fpart;
        buf[n++] = '0' + d;
        fpart -= (double)d;
    }
    while (n > 1 && buf[n-1] == '0') n--;
    if (n > 0) {
        term_putchar('.');
        for (int i = 0; i < n; i++) term_putchar(buf[i]);
    }
}

/* ── Keyboard ────────────────────────────────────────────────────────── */
static const char sc_lo[128] = {
    0,0x1B,'1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,'\\','z','x','c','v','b','n','m',',','.','/',0,
    '*',0,' ',0,0,0,0,0,0,0,0,0,0,0,0,0,
    '7','8','9','-','4','5','6','+','1','2','3','0','.', 0,0,0
};
static const char sc_hi[128] = {
    0,0x1B,'!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,'A','S','D','F','G','H','J','K','L',':','"','~',
    0,'|','Z','X','C','V','B','N','M','<','>','?',0,
    '*',0,' ',0,0,0,0,0,0,0,0,0,0,0,0,0,
    '7','8','9','-','4','5','6','+','1','2','3','0','.', 0,0,0
};
static int shift = 0, caps = 0;

static uint8_t kb_read(void) {
    while (!(inb(0x64) & 1)) {}
    return inb(0x60);
}

int term_getchar(void) {
    for (;;) {
        uint8_t sc = kb_read();
        if (sc & 0x80) {
            sc &= 0x7F;
            if (sc == 0x2A || sc == 0x36) shift = 0;
            continue;
        }
        if (sc == 0x2A || sc == 0x36) { shift = 1; continue; }
        if (sc == 0x3A) { caps ^= 1; continue; }
        if (sc >= 128)  continue;
        char c = shift ? sc_hi[sc] : sc_lo[sc];
        if (!c) continue;
        if (caps && c >= 'a' && c <= 'z') c -= 32;
        else if (caps && c >= 'A' && c <= 'Z') c += 32;
        return (unsigned char)c;
    }
}

void term_get_line(char *buf, int max) {
    int i = 0;
    for (;;) {
        int c = term_getchar();
        if (c == '\n' || c == '\r') { term_putchar('\n'); buf[i] = 0; cur(); return; }
        if (c == '\b') {
            if (i > 0) { i--; term_putchar('\b'); cur(); }
            continue;
        }
        if (i < max - 1) {
            buf[i++] = (char)c;
            term_putchar((char)c);
            cur();
        }
    }
}

/* ── KERNEL PANIC ────────────────────────────────────────────────────── */
/*
 * kpanic(msg) — prints a red "KERNEL PANIC" banner + message, then halts.
 * Safe to call at any point after VGA is usable (i.e. after pmode32 entry).
 * Saves and restores attr so it can be called mid-operation if needed.
 */
void kpanic(const char *msg)
{
    uint8_t saved_attr = attr;

    /* Red on white banner line */
    term_set_color(VGA_RED, VGA_WHITE);
    term_puts("\n\n  *** KERNEL PANIC ***  ");

    /* Yellow on red detail */
    term_set_color(VGA_YELLOW, VGA_RED);
    term_putchar(' ');
    term_puts(msg);
    term_putchar(' ');

    /* White on red "system halted" */
    term_set_color(VGA_WHITE, VGA_RED);
    term_puts("\n  System halted. Power off or reset.\n");

    (void)saved_attr;   /* no restore — we're halted */
    __asm__ volatile("cli");
    for (;;) __asm__ volatile("hlt");
}

/* ── Boot banner ─────────────────────────────────────────────────────── */
static void banner_row(const char *s, uint8_t fg, uint8_t bg) {
    term_set_color(fg, bg);
    term_putchar(' ');
    const char *p = s;
    int len = 0;
    while (*p++) len++;
    term_puts(s);
    for (int i = len + 1; i < COLS; i++) term_putchar(' ');
}

void draw_banner(void) {
    term_clear();
    banner_row("", VGA_BLACK, VGA_CYAN);
    banner_row(" BTBX / Bare (Tiny?) BASIC eXecutor", VGA_WHITE, VGA_BLUE);
    banner_row(" x86-32 ver. 1.3.36 20260611", VGA_LIGHT_GREY, VGA_BLUE);
    banner_row("", VGA_BLACK, VGA_CYAN);
    term_set_color(VGA_DARK_GREY, VGA_BLACK);
    term_puts("\n  LET  PRINT  INPUT  IF/THEN  GOTO  GOSUB  RETURN\n");
    term_puts("  FOR/NEXT  WHILE/WEND  LIST  RUN  NEW  CLEAR  HALT\n");
    term_puts("  LOAD  SAVE  DIR\n\n");
    term_set_color(VGA_DARK_GREY, VGA_BLACK);
    for (int i = 0; i < COLS; i++) term_putchar('-');
    term_putchar('\n');
    term_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

/* ── kernel_main ─────────────────────────────────────────────────────── */
void kernel_main(uint8_t boot_drive)
{
    /* BSS was already cleared by entry.asm before we were called */
    pic_remap();

    /* flush any stale keyboard data */
    while (inb(0x64) & 1) (void)inb(0x60);

    draw_banner();

    fat_init(boot_drive);

    if (!fat_ready()) {
        /* Non-fatal: BASIC still works, just no disk I/O */
        term_set_color(VGA_YELLOW, VGA_BLACK);
        term_puts("  WARNING: FAT init failed (drive=0x");
        /* print drive byte as hex */
        char h = "0123456789ABCDEF"[boot_drive >> 4];
        term_putchar(h);
        h = "0123456789ABCDEF"[boot_drive & 0xF];
        term_putchar(h);
        term_puts(") -- LOAD/SAVE/DIR unavailable\n\n");
        term_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    }

    basic_run();

    /* Should never reach here */
    kpanic("basic_run() returned unexpectedly");
}

void term_sync_cursor(void) { cur(); }
