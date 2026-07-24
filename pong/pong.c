#include <stdint.h>

#define VGA ((volatile uint16_t*)0xB8000)
#define COLS 80
#define ROWS 25

#define KBD_DATA 0x60
#define KBD_STATUS 0x64

static inline uint8_t inb(uint16_t port){
    uint8_t v;
    __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void outb(uint16_t port, uint8_t val){
    __asm__ volatile("outb %0,%1" : : "a"(val), "Nd"(port));
}

static void putc_at(int x,int y,char c,uint8_t attr){
    if(x<0||x>=COLS||y<0||y>=ROWS) return;
    VGA[y*COLS+x] = ((uint16_t)attr<<8) | (uint8_t)c;
}

static void puts_at(int x,int y,const char *s,uint8_t attr){
    int i=0;
    while(s[i]){
        putc_at(x+i,y,s[i],attr);
        i++;
    }
}

static void clear_screen(uint8_t attr){
    int i;
    for(i=0;i<COLS*ROWS;i++) VGA[i] = ((uint16_t)attr<<8) | ' ';
}

static void draw_vbar(int x,int y0,int y1,char c,uint8_t attr){
    int y;
    for(y=y0;y<=y1;y++) putc_at(x,y,c,attr);
}

static void draw_hline(int y,char c,uint8_t attr){
    int x;
    for(x=0;x<COLS;x++) putc_at(x,y,c,attr);
}

static void draw_border(int top,int bottom,uint8_t attr){
    int x,y;
    for(x=0;x<COLS;x++){
        putc_at(x,top,0xC4,attr);
        putc_at(x,bottom,0xC4,attr);
    }
    for(y=top;y<=bottom;y++){
        putc_at(0,y,0xB3,attr);
        putc_at(COLS-1,y,0xB3,attr);
    }
    putc_at(0,top,0xDA,attr);
    putc_at(COLS-1,top,0xBF,attr);
    putc_at(0,bottom,0xC0,attr);
    putc_at(COLS-1,bottom,0xD9,attr);
}

static void draw_banner(void){
    const char *title = "B T B X   P O N G";
    int x = (COLS - 18) / 2;
    draw_hline(0, ' ', 0x1F);
    puts_at(x, 0, title, 0x1F);
}

/* ── PIT-based millisecond delay (hardware timer, CPU-speed independent) ── */
static void delay_ms(uint32_t ms){
    while(ms--){
        uint32_t ticks = 1193u;
        outb(0x43, 0x00);
        uint16_t t0 = (uint16_t)inb(0x40) | ((uint16_t)inb(0x40)<<8);
        while(ticks){
            outb(0x43, 0x00);
            uint16_t t1 = (uint16_t)inb(0x40) | ((uint16_t)inb(0x40)<<8);
            uint16_t diff = (uint16_t)(t0 - t1);
            if(diff >= ticks) break;
            ticks -= diff;
            t0 = t1;
        }
    }
}

/* scan set 1 make codes */
#define SC_A   0x1E
#define SC_Z   0x2C
#define SC_Q   0x10
#define SC_ESC 0x01

void app_main(void){
    const int play_top = 2;
    const int play_bottom = ROWS-2;
    const int pad_h = 6;
    const int lx = 3;
    const int rx = COLS-4;
    const int mid = COLS/2;

    int ly = (play_top+play_bottom)/2 - pad_h/2;
    int ry = (play_top+play_bottom)/2 - pad_h/2;
    int bx = mid, by = (play_top+play_bottom)/2;
    int dx = 1, dy = 1;
    int left_score = 0, right_score = 0;
    int running = 1;

    while(running){
        uint8_t status = inb(KBD_STATUS);
        if(status & 1){
            uint8_t sc = inb(KBD_DATA);
            if(sc==SC_Q || sc==SC_ESC) running = 0;
            if(sc==SC_A){ ly--; if(ly<play_top+1) ly=play_top+1; }
            if(sc==SC_Z){ ly++; if(ly>play_bottom-pad_h) ly=play_bottom-pad_h; }
        }

        if(by < ry+pad_h/2) ry--;
        if(by > ry+pad_h/2) ry++;
        if(ry<play_top+1) ry=play_top+1;
        if(ry>play_bottom-pad_h) ry=play_bottom-pad_h;

        bx += dx;
        by += dy;

        if(by<=play_top+1 || by>=play_bottom-1) dy=-dy;

        if(dx<0 && bx<=lx+1){
            if(by>=ly && by<=ly+pad_h) dx=1;
            else { right_score++; bx=mid; by=(play_top+play_bottom)/2; dx=1; dy=1; }
        }
        if(dx>0 && bx>=rx-1){
            if(by>=ry && by<=ry+pad_h) dx=-1;
            else { left_score++; bx=mid; by=(play_top+play_bottom)/2; dx=-1; dy=1; }
        }

        clear_screen(0x1B);
        draw_banner();
        draw_border(play_top, play_bottom, 0x0B);

        {
            int y;
            for(y=play_top+1;y<play_bottom;y+=2)
                putc_at(mid,y,0xB0,0x08);
        }

        draw_vbar(lx, ly, ly+pad_h, 0xDB, 0x0E);
        draw_vbar(rx, ry, ry+pad_h, 0xDB, 0x0C);
        putc_at(bx, by, 0x09, 0x0F);

        {
            char lbuf[3], rbuf[3];
            lbuf[0]='0'+((left_score/10)%10); lbuf[1]='0'+(left_score%10); lbuf[2]=0;
            rbuf[0]='0'+((right_score/10)%10); rbuf[1]='0'+(right_score%10); rbuf[2]=0;
            puts_at(mid-8, 1, lbuf, 0x0A);
            puts_at(mid+6, 1, rbuf, 0x0A);
        }

        puts_at(2, ROWS-1, "A/Z move  Q/Esc quit", 0x07);

        delay_ms(60);
    }

    clear_screen(0x1B);
    draw_banner();
    puts_at(mid-6, ROWS/2, "GAME OVER", 0x4F);
    {
        char sbuf[24];
        int i=0,l=left_score,r=right_score;
        const char *lead = "FINAL SCORE  ";
        while(lead[i]){ sbuf[i]=lead[i]; i++; }
        sbuf[i++] = '0'+((l/10)%10);
        sbuf[i++] = '0'+(l%10);
        sbuf[i++] = '-';
        sbuf[i++] = '0'+((r/10)%10);
        sbuf[i++] = '0'+(r%10);
        sbuf[i] = 0;
        puts_at(mid-12, ROWS/2+2, sbuf, 0x07);
    }
}

__attribute__((section(".text.start")))
void _start(void){
    app_main();
    for(;;){ __asm__ volatile("hlt"); }
}
