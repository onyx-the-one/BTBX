#include "rtc/rtc.h"

/* BEEP */
if(kw("BEEP")){uint32_t freq=800,ms=200;sw();if(*pp&&*pp!='\''){Val f=expr();if(err)return;freq=(uint32_t)f.i;sw();if(*pp==','){pp++;Val m=expr();if(err)return;ms=(uint32_t)m.i;}}beep(freq,ms);return;}

/* SAY */
if(kw("SAY")){sw();int sayisstr=0;if(*pp=='"')sayisstr=1;else{const char *peek=pp;while(*peek>='A'&&*peek<='Z'||*peek>='a'&&*peek<='z'||*peek>='0'&&*peek<='9'||*peek=='_')peek++;sayisstr=(*peek=='$');}
if(sayisstr){Val v=strexpr();if(err)return;say_string(spool[v.si]);sfree(v.si);}
else{Val v=expr();if(err)return;char tmp[16];itos(v.i,tmp,16);say_string(tmp);}
return;}

/* DELAY ms — busy-wait via PIT, for game loop pacing */
if(kw("DELAY")){Val ms=expr();if(err)return;delay_ms((uint32_t)ms.i);return;}

/* BLOAD */
if(kw("BLOAD")){sw();Val fn=strexpr();if(err)return;sw();if(*pp!=','){berr("SYNTAX");sfree(fn.si);return;}pp++;Val addr=expr();if(err){sfree(fn.si);return;}char nm11[12];toname11(spool[fn.si],nm11);sfree(fn.si);if(!fat_ready()){berr("DISK NOT READY");return;}int n=fat_load(nm11,(void*)(uint32_t)addr.i,0x7F000);if(n<=0){berr("FILE NOT FOUND");return;}term_set_color(VGA_DARK_GREY,VGA_BLACK);term_puti((int32_t)n);term_puts(" bytes - 0x");uint32_t av=(uint32_t)addr.i;for(int s=28;s>=0;s-=4)term_putchar("0123456789ABCDEF"[(av>>s)&0xF]);term_putchar('\n');term_set_color(VGA_LIGHT_GREY,VGA_BLACK);return;}

/* SYS */
if(kw("SYS")){Val addr=expr();if(err)return;typedef void(*fnt)(void);fnt fn=(fnt)(uintptr_t)(uint32_t)addr.i;fn();return;}

/* TIME (banner-style combined date+time print) */
if(kw("TIME")){RtcTime rt;char db[16],tb[16];if(rtc_read(&rt)<0){berr("RTC");return;}rtc_format_date(&rt,db,(int)sizeof(db));rtc_format_time(&rt,tb,(int)sizeof(tb));term_puts(db);term_putchar(' ');term_puts(tb);term_putchar('\n');return;}

/* HELP */
if(kw("HELP")){term_set_color(VGA_CYAN,VGA_BLACK);term_puts("BTBX BASIC command reference\n");term_set_color(VGA_LIGHT_GREY,VGA_BLACK);
term_puts(" PRINT/? expr,expr,...  INPUT prompt$, var  LET var=expr\n");
term_puts(" REM comment  IF expr THEN .. [ELSE] [ENDIF]  GOTO n\n");
term_puts(" FOR v=n TO n [STEP n]  NEXT v  WHILE expr  WEND\n");
term_puts(" GOSUB n  RETURN  ON expr GOTO/GOSUB n,...\n");
term_puts(" DATA  READ  RESTORE  DIM name(d,d)  NEW  RUN  LIST  END\n");
term_puts(" PEEK(addr)  POKE addr,v  INKEY$  DATE$  TIME$  TIME\n");
term_puts(" AND OR XOR NOT SHL SHR  HEX$  BIN$  H  B\n");
term_puts(" SCREEN mode  PSET x,y,c  LINE x0,y0,x1,y1,c\n");
term_puts(" RECT/CIRCLE x,y,..,c,[F]  PAINT x,y,c  PALETTE i,r,g,b\n");
term_puts(" GCLS c  POINT(x,y)  DELAY ms\n");
term_puts(" SAVE f.bas  LOAD f.bas  DIR  OPEN f$ FOR INPUT/OUTPUT AS n\n");
term_puts(" CLOSE n  BEEP freq,ms  SAY strexpr  BLOAD f.bin,addr  SYS addr\n");
term_puts(" Math: SIN COS TAN ATN EXP LOG SQR ABS INT FIX SGN RND\n");
term_puts(" Str: LEFT$ RIGHT$ MID$ STR$ CHR$ VAL ASC LEN\n");
term_puts(" HALT  CLEAR  HELP\n");
return;}
