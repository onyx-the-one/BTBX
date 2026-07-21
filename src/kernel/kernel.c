#include "basic.h"
#include "fs/fat12.h"

static inline void outb(uint16_t p,uint8_t v){__asm__ __volatile__("outb %0,%1"::"a"(v),"Nd"(p));}
static inline uint8_t inb(uint16_t p){uint8_t v;__asm__ __volatile__("inb %1,%0":"=a"(v):"Nd"(p));return v;}

/* ── VGA text terminal ────────────────────────────────────────────── */
#define COLS 80
#define ROWS 25
#define VGA ((volatile uint16_t *)0xB8000)
static int col=0,row=0;
static uint8_t attr=0x07;

static void cur(void){uint16_t pos=(uint16_t)(row*COLS+col);outb(0x3D4,0x0F);outb(0x3D5,(uint8_t)(pos&0xFF));outb(0x3D4,0x0E);outb(0x3D5,(uint8_t)(pos>>8));}

void term_set_color(uint8_t fg,uint8_t bg){attr=(uint8_t)((bg<<4)|(fg&0xF));}

void term_clear(void){uint16_t blank=(uint16_t)((uint16_t)attr<<8|' ');for(int i=0;i<COLS*ROWS;i++)VGA[i]=blank;col=row=0;cur();}

static void scroll(void){for(int r=1;r<ROWS;r++)for(int c=0;c<COLS;c++)VGA[(r-1)*COLS+c]=VGA[r*COLS+c];uint16_t blank=(uint16_t)((uint16_t)attr<<8|' ');for(int c=0;c<COLS;c++)VGA[(ROWS-1)*COLS+c]=blank;row=ROWS-1;}

void term_putchar(char c){
    if(c=='\n'){col=0;if(++row>=ROWS)scroll();}
    else if(c=='\r'){col=0;}
    else if(c=='\b'){if(col>0){col--;VGA[row*COLS+col]=(uint16_t)((uint16_t)attr<<8|' ');}}
    else{VGA[row*COLS+col]=(uint16_t)((uint16_t)attr<<8|(unsigned char)c);if(++col>=COLS){col=0;if(++row>=ROWS)scroll();}}}

void term_puts(const char *s){while(*s)term_putchar(*s++);}

void term_puti(int32_t n){char buf[12];int i=0;if(n<0){term_putchar('-');n=-n;}if(!n){term_putchar('0');return;}while(n){buf[i++]='0'+(int)(n%10);n/=10;}while(i--)term_putchar(buf[i]);}

void term_putf(double f){
    if(f<0.0){term_putchar('-');f=-f;}
    if(f!=0.0&&(f>=1e10||f<1e-4)){int exp=0;while(f>=10.0){f/=10.0;exp++;}while(f<1.0){f*=10.0;exp--;}term_putf(f);term_puts("E");if(exp<0){term_putchar('-');exp=-exp;}term_puti((int32_t)exp);return;}
    int32_t ipart=(int32_t)f;double fpart=f-(double)ipart;term_puti(ipart);char buf[10];int n=0;for(int i=0;i<8;i++){fpart*=10.0;int d=(int)fpart;buf[n++]='0'+d;fpart-=(double)d;}while(n>1&&buf[n-1]=='0')n--;if(n>0){term_putchar('.');for(int i=0;i<n;i++)term_putchar(buf[i]);}}

/* ── Keyboard ─────────────────────────────────────────────────────── */
static const char sclo[128]={0,0x1B,'1','2','3','4','5','6','7','8','9','0','-','=','\b','\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',0,'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\','z','x','c','v','b','n','m',',','.','/',0,'*',0,' ',0,0,0,0,0,0,0,0,0,0,0,0,0,'7','8','9','-','4','5','6','+','1','2','3','0','.',0,0,0};
static const char schi[128]={0,0x1B,'!','@','#','$','%','^','&','*','(',')','_','+','\b','\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,'A','S','D','F','G','H','J','K','L',':','"','~',0,'|','Z','X','C','V','B','N','M','<','>','?',0,'*',0,' ',0,0,0,0,0,0,0,0,0,0,0,0,0,'7','8','9','-','4','5','6','+','1','2','3','0','.',0,0,0};
static int shift=0,caps=0;
static uint8_t kb_read(void){while(!(inb(0x64)&1));return inb(0x60);}

int term_getchar(void){for(;;){uint8_t sc=kb_read();if(sc&0x80){sc&=0x7F;if(sc==0x2A||sc==0x36)shift=0;continue;}if(sc==0x2A||sc==0x36){shift=1;continue;}if(sc==0x3A){caps^=1;continue;}if(sc>=128)continue;char c=shift?schi[sc]:sclo[sc];if(!c)continue;if(caps){if(c>='a'&&c<='z')c-=32;else if(c>='A'&&c<='Z')c+=32;}return(unsigned char)c;}}

int term_peekkey(void){if(!(inb(0x64)&1))return 0;uint8_t sc=inb(0x60);if(sc&0x80){sc&=0x7F;if(sc==0x2A||sc==0x36)shift=0;return 0;}if(sc==0x2A||sc==0x36){shift=1;return 0;}if(sc==0x3A){caps^=1;return 0;}if(sc>=128)return 0;char c=shift?schi[sc]:sclo[sc];if(!c)return 0;if(caps){if(c>='a'&&c<='z')c-=32;else if(c>='A'&&c<='Z')c+=32;}return(unsigned char)c;}

void term_getline(char *buf,int max){int i=0;for(;;){int c=term_getchar();if(c=='\n'||c=='\r'){term_putchar('\n');buf[i]=0;cur();return;}if(c=='\b'){if(i>0){i--;term_putchar('\b');cur();}continue;}if(i<max-1){buf[i++]=(char)c;term_putchar((char)c);cur();}}}

void term_sync_cursor(void){cur();}

/* ── Kernel panic ─────────────────────────────────────────────────── */
void kpanic(const char *msg){term_set_color(VGA_RED,VGA_WHITE);term_puts(" KERNEL PANIC ");term_set_color(VGA_YELLOW,VGA_RED);term_putchar(' ');term_puts(msg);term_putchar('\n');term_set_color(VGA_WHITE,VGA_RED);term_puts(" System halted. Power off or reset.\n");__asm__ __volatile__("cli");for(;;)__asm__ __volatile__("hlt");}

/* ── Boot banner ──────────────────────────────────────────────────── */
static void banner_row(const char *s,uint8_t fg,uint8_t bg){term_set_color(fg,bg);term_putchar(' ');const char *p=s;int len=0;while(*p++)len++;term_puts(s);for(int i=len+1;i<COLS;i++)term_putchar(' ');}

void draw_banner(void){term_clear();banner_row("",VGA_BLACK,VGA_CYAN);banner_row("BTBX / Bare Tiny(?) BASIC eXecutor",VGA_WHITE,VGA_BLUE);banner_row("x86-32  ver. 1.7.8  20-07-2026",VGA_LIGHT_GREY,VGA_BLUE);banner_row("",VGA_BLACK,VGA_CYAN);term_set_color(VGA_DARK_GREY,VGA_BLACK);for(int i=0;i<COLS;i++)term_putchar('-');term_putchar('\n');term_set_color(VGA_LIGHT_GREY,VGA_BLACK);}

/* ── Kernel entry ─────────────────────────────────────────────────── */
void kernel_main(uint8_t boot_drive){
    while(inb(0x64)&1)(void)inb(0x60);
    draw_banner();
    fat_init(boot_drive);
    if(!fat_ready()){term_set_color(VGA_YELLOW,VGA_BLACK);term_puts(" WARNING: FAT init failed (drive=0x");char h="0123456789ABCDEF"[boot_drive>>4];term_putchar(h);h="0123456789ABCDEF"[boot_drive&0xF];term_putchar(h);term_puts(") -- LOAD/SAVE/DIR unavailable\n\n");term_set_color(VGA_LIGHT_GREY,VGA_BLACK);}
    basic_run();
    kpanic("basic_run() returned unexpectedly");}
