#include "gfx/gfx.h"

/* SCREEN */
if(kw("SCREEN")){
Val m=expr(); if(err)return;
if(m.i==13||m.i==1){
if(gfx_enter()<0){ berr("NO VIDEO THUNK"); return; }
}else if(m.i==0){
if(gfx_leave()<0){ berr("NO VIDEO THUNK"); return; }
}else{
berr("BAD MODE");
}
return;
}

/* CLS graphics-aware: if in graphics mode, clear framebuffer instead */
if(kw("GCLS")){
Val c=vint(0);
sw();
if(*pp && *pp!='\''){
c=expr(); if(err)return;
}
if(!gfx_active()){ berr("NOT IN GRAPHICS MODE"); return; }
gfx_cls((uint8_t)c.i);
return;
}

/* PSET x,y,color */
if(kw("PSET")){
if(!gfx_active()){ berr("NOT IN GRAPHICS MODE"); return; }
Val x=expr(); if(err)return;
sw(); if(*pp!=','){ berr("SYNTAX"); return; } pp++;
Val y=expr(); if(err)return;
Val c=vint(15);
sw();
if(*pp==','){
pp++;
c=expr(); if(err)return;
}
gfx_pset(x.i,y.i,(uint8_t)c.i);
return;
}

/* LINE x0,y0,x1,y1,color */
if(kw("LINE")){
if(!gfx_active()){ berr("NOT IN GRAPHICS MODE"); return; }
Val x0=expr(); if(err)return;
sw(); if(*pp!=','){ berr("SYNTAX"); return; } pp++;
Val y0=expr(); if(err)return;
sw(); if(*pp!=','){ berr("SYNTAX"); return; } pp++;
Val x1=expr(); if(err)return;
sw(); if(*pp!=','){ berr("SYNTAX"); return; } pp++;
Val y1=expr(); if(err)return;
Val c=vint(15);
sw();
if(*pp==','){
pp++;
c=expr(); if(err)return;
}
gfx_line(x0.i,y0.i,x1.i,y1.i,(uint8_t)c.i);
return;
}

/* RECT x0,y0,x1,y1,color[,F] */
if(kw("RECT")){
if(!gfx_active()){ berr("NOT IN GRAPHICS MODE"); return; }
Val x0=expr(); if(err)return;
sw(); if(*pp!=','){ berr("SYNTAX"); return; } pp++;
Val y0=expr(); if(err)return;
sw(); if(*pp!=','){ berr("SYNTAX"); return; } pp++;
Val x1=expr(); if(err)return;
sw(); if(*pp!=','){ berr("SYNTAX"); return; } pp++;
Val y1=expr(); if(err)return;
Val c=vint(15);
int fill=0;
sw();
if(*pp==','){
pp++;
c=expr(); if(err)return;
sw();
if(*pp==','){
pp++;
sw();
if(kw("F")) fill=1;
}
}
gfx_rect(x0.i,y0.i,x1.i,y1.i,(uint8_t)c.i,fill);
return;
}

/* CIRCLE cx,cy,r,color[,F] */
if(kw("CIRCLE")){
if(!gfx_active()){ berr("NOT IN GRAPHICS MODE"); return; }
Val cx=expr(); if(err)return;
sw(); if(*pp!=','){ berr("SYNTAX"); return; } pp++;
Val cy=expr(); if(err)return;
sw(); if(*pp!=','){ berr("SYNTAX"); return; } pp++;
Val r=expr(); if(err)return;
Val c=vint(15);
int fill=0;
sw();
if(*pp==','){
pp++;
c=expr(); if(err)return;
sw();
if(*pp==','){
pp++;
sw();
if(kw("F")) fill=1;
}
}
gfx_circle(cx.i,cy.i,r.i,(uint8_t)c.i,fill);
return;
}

/* PAINT x,y,color — flood fill from (x,y) */
if(kw("PAINT")){
if(!gfx_active()){ berr("NOT IN GRAPHICS MODE"); return; }
Val x=expr(); if(err)return;
sw(); if(*pp!=','){ berr("SYNTAX"); return; } pp++;
Val y=expr(); if(err)return;
Val c=vint(15);
sw();
if(*pp==','){
pp++;
c=expr(); if(err)return;
}
gfx_paint(x.i,y.i,(uint8_t)c.i);
return;
}

/* PALETTE index,r,g,b (each component 0-63, VGA 6-bit DAC) */
if(kw("PALETTE")){
Val idx=expr(); if(err)return;
sw(); if(*pp!=','){ berr("SYNTAX"); return; } pp++;
Val r=expr(); if(err)return;
sw(); if(*pp!=','){ berr("SYNTAX"); return; } pp++;
Val g=expr(); if(err)return;
sw(); if(*pp!=','){ berr("SYNTAX"); return; } pp++;
Val b=expr(); if(err)return;
gfx_set_palette((uint8_t)idx.i,(uint8_t)r.i,(uint8_t)g.i,(uint8_t)b.i);
return;
}
