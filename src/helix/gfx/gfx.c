#include "gfx.h"
#include "basic.h"

/* mode 13h enter/leave goes through the BIOS thunk opcode 6, see thunk16.asm */
extern int bios_set_video_mode(int mode);

static int gfx_is_active = 0;
static volatile uint8_t *fb = (volatile uint8_t*)GFX_FB_ADDR;

int gfx_enter(void){
if(gfx_is_active) return 0;
if(bios_set_video_mode(0x13) < 0) return -1;
gfx_is_active = 1;
return 0;
}

int gfx_leave(void){
if(!gfx_is_active) return 0;
if(bios_set_video_mode(0x03) < 0) return -1;
gfx_is_active = 0;
return 0;
}

int gfx_active(void){ return gfx_is_active; }

void gfx_cls(uint8_t color){
for(int i=0;i<GFX_WIDTH*GFX_HEIGHT;i++) fb[i]=color;
}

void gfx_pset(int x, int y, uint8_t color){
if(x<0||x>=GFX_WIDTH||y<0||y>=GFX_HEIGHT) return;
fb[y*GFX_WIDTH+x]=color;
}

uint8_t gfx_getpixel(int x, int y){
if(x<0||x>=GFX_WIDTH||y<0||y>=GFX_HEIGHT) return 0;
return fb[y*GFX_WIDTH+x];
}

void gfx_line(int x0, int y0, int x1, int y1, uint8_t color){
int dx=x1-x0, dy=y1-y0;
int adx=dx<0?-dx:dx, ady=dy<0?-dy:dy;
int sx=dx<0?-1:1, sy=dy<0?-1:1;
int x=x0, y=y0;
if(adx>=ady){
int err2=0;
for(int i=0;i<=adx;i++){
gfx_pset(x,y,color);
x+=sx;
err2+=ady;
if(err2*2>=adx){ y+=sy; err2-=adx; }
}
}else{
int err2=0;
for(int i=0;i<=ady;i++){
gfx_pset(x,y,color);
y+=sy;
err2+=adx;
if(err2*2>=ady){ x+=sx; err2-=ady; }
}
}
}

void gfx_rect(int x0, int y0, int x1, int y1, uint8_t color, int fill){
if(x0>x1){int t=x0;x0=x1;x1=t;}
if(y0>y1){int t=y0;y0=y1;y1=t;}
if(fill){
for(int y=y0;y<=y1;y++)
for(int x=x0;x<=x1;x++)
gfx_pset(x,y,color);
}else{
gfx_line(x0,y0,x1,y0,color);
gfx_line(x0,y1,x1,y1,color);
gfx_line(x0,y0,x0,y1,color);
gfx_line(x1,y0,x1,y1,color);
}
}

void gfx_circle(int cx, int cy, int r, uint8_t color, int fill){
int x=r, y=0, err=0;
while(x>=y){
if(fill){
gfx_line(cx-x,cy+y,cx+x,cy+y,color);
gfx_line(cx-x,cy-y,cx+x,cy-y,color);
gfx_line(cx-y,cy+x,cx+y,cy+x,color);
gfx_line(cx-y,cy-x,cx+y,cy-x,color);
}else{
gfx_pset(cx+x,cy+y,color); gfx_pset(cx-x,cy+y,color);
gfx_pset(cx+x,cy-y,color); gfx_pset(cx-x,cy-y,color);
gfx_pset(cx+y,cy+x,color); gfx_pset(cx-y,cy+x,color);
gfx_pset(cx+y,cy-x,color); gfx_pset(cx-y,cy-x,color);
}
y++;
err+=1+2*y;
if(2*err+(1-2*x) > 0){ x--; err+=1-2*x; }
}
}

/* stack-based 4-way flood fill, fixed depth since there's no heap here.
   bails out silently on overflow rather than corrupting anything past sx/sy */
void gfx_paint(int x, int y, uint8_t color){
uint8_t target=gfx_getpixel(x,y);
if(target==color) return;
static int sx[4096], sy[4096];
int sp=0;
sx[sp]=x; sy[sp]=y; sp++;
while(sp>0){
sp--;
int cx=sx[sp], cy=sy[sp];
if(cx<0||cx>=GFX_WIDTH||cy<0||cy>=GFX_HEIGHT) continue;
if(gfx_getpixel(cx,cy)!=target) continue;
gfx_pset(cx,cy,color);
if(sp>=4092) continue;
sx[sp]=cx+1; sy[sp]=cy; sp++;
sx[sp]=cx-1; sy[sp]=cy; sp++;
sx[sp]=cx; sy[sp]=cy+1; sp++;
sx[sp]=cx; sy[sp]=cy-1; sp++;
}
}

void gfx_set_palette(uint8_t index, uint8_t r, uint8_t g, uint8_t b){
__asm__ volatile("outb %0,%1"::"a"((uint8_t)index),"Nd"((uint16_t)0x3C8));
__asm__ volatile("outb %0,%1"::"a"((uint8_t)(r&0x3F)),"Nd"((uint16_t)0x3C9));
__asm__ volatile("outb %0,%1"::"a"((uint8_t)(g&0x3F)),"Nd"((uint16_t)0x3C9));
__asm__ volatile("outb %0,%1"::"a"((uint8_t)(b&0x3F)),"Nd"((uint16_t)0x3C9));
}
