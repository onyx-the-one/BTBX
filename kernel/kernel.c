#include "basic.h"

static inline void outb(uint16_t p, uint8_t v) { __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p)); }
static inline uint8_t inb(uint16_t p) { uint8_t v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline void iowait(void) { outb(0x80,0); }

static void pic_remap(void) {
	    uint8_t a=inb(0x21), b=inb(0xA1);
    outb(0x20,0x11); iowait(); outb(0xA0,0x11); iowait();
    outb(0x21,0x20); iowait(); outb(0xA1,0x28); iowait();
    outb(0x21,0x04); iowait(); outb(0xA1,0x02); iowait();
    outb(0x21,0x01); iowait(); outb(0xA1,0x01); iowait();
    outb(0x21,a); outb(0xA1,b);
    outb(0x21,0xFF); outb(0xA1,0xFF);
}

#define COLS 80
#define ROWS 25
#define VGA  ((volatile uint16_t*)0xB8000)

static int col=0, row=0;
static uint8_t attr=0x07;

static void cur(void) {
	    uint16_t p=(uint16_t)(row*COLS+col);
    outb(0x3D4,0x0F); outb(0x3D5,p&0xFF);
    outb(0x3D4,0x0E); outb(0x3D5,p>>8);
}

void term_set_color(uint8_t fg, uint8_t bg) { attr=(uint8_t)((bg<<4)|(fg&0xF)); }

void term_clear(void) {
	    for(int i=0;i<COLS*ROWS;i++) VGA[i]=(uint16_t)' '|(0x07<<8);
    col=0; row=0; cur();
}

static void scroll(void) {
	    for(int r=1;r<ROWS;r++)
        for(int c=0;c<COLS;c++)
            VGA[(r-1)*COLS+c]=VGA[r*COLS+c];
    for(int c=0;c<COLS;c++) VGA[(ROWS-1)*COLS+c]=(uint16_t)' '|(attr<<8);
    row=ROWS-1;
}

/* no cur() call here - caller decides when to sync */
void term_putchar(char c) {
	    if(c=='\n') { col=0; if(++row>=ROWS) scroll(); }
    else if(c=='\r') { col=0; }
    else if(c=='\b') { if(col>0) { col--; VGA[row*COLS+col]=(uint16_t)' '|(attr<<8); } }
    else {
	        VGA[row*COLS+col]=(uint16_t)(unsigned char)c|(attr<<8);
        if(++col>=COLS) { col=0; if(++row>=ROWS) scroll(); }
    }
}

void term_puts(const char *s) { while(*s) term_putchar(*s++); }

void term_puti(int32_t n) {
	    char b[12]; int i=0;
    if(n<0) { term_putchar('-'); n=-n; }
    if(!n) { term_putchar('0'); return; }
    while(n) { b[i++]='0'+(n%10); n/=10; }
    while(i--) term_putchar(b[i]);
}

static const char sc_lo[128] = {
	    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=', 8,
    9, 'q','w','e','r','t','y','u','i','o','p','[',']', 13,
    0, 'a','s','d','f','g','h','j','k','l',';','\'','`',
    0, '\\','z','x','c','v','b','n','m',',','.','/', 0,
    '*', 0, ' '
};
static const char sc_hi[128] = {
	    0,  27, '!','@','#','$','%','^','&','*','(',')','_','+', 8,
    9, 'Q','W','E','R','T','Y','U','I','O','P','{','}', 13,
    0, 'A','S','D','F','G','H','J','K','L',':','"','~',
    0, '|','Z','X','C','V','B','N','M','<','>','?', 0,
    '*', 0, ' '
};

static int shift=0, caps=0;

static uint8_t kb_read(void) {
	    while(!(inb(0x64)&1)) {}
    return inb(0x60);
}

int term_getchar(void) {
	    for(;;) {
	        uint8_t sc=kb_read();
        if(sc&0x80) { sc&=0x7F; if(sc==0x2A||sc==0x36) shift=0; continue; }
        if(sc==0x2A||sc==0x36) { shift=1; continue; }
        if(sc==0x3A) { caps^=1; continue; }
        if(sc>=128) continue;
        char c=shift ? sc_hi[sc] : sc_lo[sc];
        if(!c) continue;
        if(caps&&c>='a'&&c<='z') c-=32;
        else if(caps&&c>='A'&&c<='Z') c+=32;
        return (unsigned char)c;
    }
}

void term_get_line(char *buf, int max) {
	    int i=0;
    for(;;) {
	        int c=term_getchar();
        if(c=='\n'||c=='\r') { term_putchar('\n'); buf[i]=0; cur(); return; }
        if(c=='\b') { if(i>0) { i--; term_putchar('\b'); cur(); } continue; }
        if(i<max-1) { buf[i++]=(char)c; term_putchar((char)c); cur(); }
    }
}

static int klen(const char *s) { int n=0; while(*s++) n++; return n; }

/* fills exactly one terminal row with bg color, text centered */
static void banner_row(const char *s, uint8_t fg, uint8_t bg) {
	    term_set_color(fg, bg);
    int l=klen(s), pad=(COLS-l)/2;
    for(int i=0;i<pad;i++) term_putchar(' ');
    term_puts(s);
    /* fill remainder without triggering a newline - write directly */
    int written=pad+l;
    for(int i=written;i<COLS;i++) {
	        VGA[row*COLS+i]=(uint16_t)' '|(attr<<8);
    }
    col=0; if(++row>=ROWS) scroll();
}

void kernel_main(void) {
	    extern char _bss_start[], _bss_end[];
    for(char *p=_bss_start; p<_bss_end; p++) *p=0;

    pic_remap();
    while(inb(0x64)&1) inb(0x60);

    term_clear();

    banner_row("",                               VGA_BLACK,      VGA_CYAN);
    banner_row(" BTBX  /  Bare TinyBASIC eXecutor", VGA_WHITE,   VGA_BLUE);
    banner_row(" x86-32  /  2026",               VGA_LIGHT_GREY, VGA_BLUE);
    banner_row("",                               VGA_BLACK,      VGA_CYAN);

    term_set_color(VGA_DARK_GREY, VGA_BLACK);
    term_puts("\n  vars A-Z   LET PRINT INPUT IF/THEN GOTO GOSUB RETURN\n");
    term_puts("            FOR/NEXT REM END LIST RUN NEW   ABS() SQR()\n\n");

    term_set_color(VGA_DARK_GREY, VGA_BLACK);
    for(int i=0;i<COLS;i++) term_putchar('-');
    term_putchar('\n');

    term_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    cur();

    basic_run();
    for(;;) __asm__ volatile("cli; hlt");
}
