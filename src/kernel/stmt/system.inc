    /* BEEP */
    if(kw("BEEP")){uint32_t freq=800,ms=200;sw();if(*pp&&*pp!='\''){Val f=expr();if(err)return;freq=(uint32_t)f.i;sw();if(*pp==','){pp++;Val m=expr();if(err)return;ms=(uint32_t)m.i;}}beep(freq,ms);return;}

    /* SAY */
    if(kw("SAY")){sw();int sayisstr=(*pp=='"');if(!sayisstr){const char *peek=pp;while((*peek>='A'&&*peek<='Z')||(*peek>='a'&&*peek<='z')||(*peek>='0'&&*peek<='9'))peek++;sayisstr=(*peek=='$');}if(sayisstr){Val v=strexpr();if(err)return;say_string(spool[v.si]);sfree(v.si);}else{Val v=expr();if(err)return;char tmp[16];itos(v.i,tmp,16);say_string(tmp);}return;}

    /* BLOAD */
    if(kw("BLOAD")){sw();Val fn=strexpr();if(err)return;sw();if(*pp!=','){berr("SYNTAX");sfree(fn.si);return;}pp++;Val addr=expr();if(err){sfree(fn.si);return;}char nm11[12];toname11(spool[fn.si],nm11);sfree(fn.si);if(!fat_ready()){berr("DISK NOT READY");return;}int n=fat_load(nm11,(void*)(uint32_t)addr.i,0x7F000);if(n<=0){berr("FILE NOT FOUND");return;}term_set_color(VGA_DARK_GREY,VGA_BLACK);term_puti((int32_t)n);term_puts(" bytes -> 0x");uint32_t av=(uint32_t)addr.i;for(int s=28;s>=0;s-=4)term_putchar("0123456789ABCDEF"[(av>>s)&0xF]);term_putchar('\n');term_set_color(VGA_LIGHT_GREY,VGA_BLACK);return;}

    /* SYS */
    if(kw("SYS")){Val addr=expr();if(err)return;typedef void(*fn_t)(void);fn_t fn=(fn_t)(uintptr_t)(uint32_t)addr.i;fn();return;}

    /* HELP */
    if(kw("HELP")){term_set_color(VGA_CYAN,VGA_BLACK);term_puts("BTBX BASIC command reference\n");term_set_color(VGA_LIGHT_GREY,VGA_BLACK);term_puts(
        "  PRINT/? expr,expr,...     INPUT [\"prompt\",] var\n"
        "  LET var=expr             REM / ' comment\n"
        "  IF expr THEN .. GOTO n ELSE ENDIF\n"
        "  FOR v=n TO n [STEP n]    NEXT v\n"
        "  WHILE expr               WEND\n"
        "  GOTO n    GOSUB n    RETURN\n"
        "  ON expr GOTO/GOSUB n,...\n"
        "  DATA    READ    RESTORE\n"
        "  DIM name(d[,d[,d]])\n"
        "  NEW  RUN  LIST  END\n"
        "  PEEK(addr)  POKE addr,v  INKEY[$]\n"
        "  SAVE \"f.bas\"  LOAD \"f.bas\"  DIR\n"
        "  OPEN \"f\" FOR INPUT/OUTPUT AS n  CLOSE n\n"
        "  BEEP [freq,ms]           SAY str/expr\n"
        "  BLOAD \"f.bin\",addr       SYS addr\n"
        "  Math: SIN COS TAN ATN EXP LOG SQR ABS INT FIX SGN RND\n"
        "  Str:  LEFT$ RIGHT$ MID$ STR$ CHR$ VAL ASC LEN\n"
        "  HALT  CLEAR  HELP\n");return;}

    berr("WHAT?");
