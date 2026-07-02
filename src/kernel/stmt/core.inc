    pp=line;sw();if(!*pp)return;
    if(kw("REM")||*pp=='\'')return;
    if(kw("KPAN")){kpanic("KPAN");return;}
    if(kw("CLEAR")){draw_banner();return;}
    if(kw("HALT")){term_set_color(VGA_YELLOW,VGA_BLACK);term_puts(" halted.\n");__asm__ __volatile__("cli");for(;;)__asm__ __volatile__("hlt");}
    if(kw("NEW")){Pn=0;nvars=0;csp=0;fsp=0;wsp=0;dataline=0;datacol=0;narrays=0;aused=0;for(int i=0;i<NSTRS;i++)sused[i]=0;for(int i=0;i<MAXFILES;i++)files[i].open=0;return;}
    if(kw("END")){running=0;return;}
    if(kw("LIST")){for(int i=0;i<Pn;i++){term_set_color(VGA_DARK_GREY,VGA_BLACK);term_puti(P[i].num);term_putchar(' ');term_set_color(VGA_LIGHT_GREY,VGA_BLACK);term_puts(P[i].txt);term_putchar('\n');}return;}
    if(kw("RUN")){pc=0;running=1;csp=0;fsp=0;wsp=0;dataline=0;datacol=0;narrays=0;aused=0;for(int i=0;i<NSTRS;i++)sused[i]=0;nvars=0;for(int i=0;i<MAXFILES;i++)files[i].open=0;return;}

    /* DIM */
    if(kw("DIM")){
        for(;;){sw();char nm[NAMELEN];if(!readname(nm,NAMELEN)){berr("SYNTAX");return;}sw();if(*pp!='('){berr("SYNTAX");return;}pp++;Val d1=expr();if(err)return;Val d2=vint(-1),d3=vint(-1);if(*pp==','){pp++;d2=expr();if(err)return;}if(*pp==','){pp++;d3=expr();if(err)return;}sw();if(*pp!=')'){berr("SYNTAX");return;}pp++;
            if(narrays>=MAXARRAYS){berr("MEM");return;}Array *a=&arrays[narrays];bstrcpy(a->name,nm,NAMELEN);a->isstr=isstrname(nm);a->dims=d3.i>=0?3:d2.i>=0?2:1;a->size[0]=d1.i;a->size[1]=d2.i;a->size[2]=d3.i;
            int total;if(a->dims==1)total=d1.i+1;else if(a->dims==2)total=(d1.i+1)*(d2.i+1);else total=(d1.i+1)*(d2.i+1)*(d3.i+1);
            if(aused+total>MAXELEMS){berr("MEM");return;}a->base=aused;a->total=total;aused+=total;for(int i=a->base;i<a->base+total;i++)aelems[i]=vint(0);narrays++;
            sw();if(*pp!=',')break;pp++;}return;}

    /* POKE */
    if(kw("POKE")){Val addr=expr();if(err)return;sw();if(*pp!=','){berr("SYNTAX");return;}pp++;Val val=expr();if(err)return;*(uint8_t*)(uint32_t)addr.i=(uint8_t)val.i;return;}

    /* LET name = expr   |   LET name(i[,j[,k]]) = expr */
    if(kw("LET")){
        char nm[NAMELEN];
        if(!readname(nm,NAMELEN)){berr("SYNTAX");return;}
        sw();
        if(*pp=='('){
            Array *ar=arrget(nm);
            if(!ar){berr("SYNTAX");return;}
            pp++;Val i1=expr();if(err)return;Val i2=vint(0),i3=vint(0);
            if(*pp==','){pp++;i2=expr();if(err)return;}
            if(*pp==','){pp++;i3=expr();if(err)return;}
            sw();if(*pp!=')'){berr("SYNTAX");return;}pp++;
            sw();if(*pp!='='){berr("SYNTAX");return;}pp++;
            int idx=arridx(ar,i1.i,i2.i,i3.i);if(idx<0)return;
            if(isstrname(nm)){Val v=strexpr();if(!err){if(aelems[idx].ty==TY_STR&&aelems[idx].si>=0)sfree(aelems[idx].si);aelems[idx]=v;}}
            else{Val v=expr();if(!err)aelems[idx]=v;}
            return;
        }
        if(*pp!='='){berr("SYNTAX");return;}pp++;
        if(isstrname(nm)){Val v=strexpr();if(!err)varset(nm,v);}
        else{Val v=expr();if(!err)varset(nm,v);}
        return;
    }

    /* bare assignment: name = expr   |   name(i[,j[,k]]) = expr   (no LET keyword) */
    {
        const char *save_pp=pp;
        char nm[NAMELEN];
        if(readname(nm,NAMELEN)){
            sw();
            if(*pp=='('){
                Array *ar=arrget(nm);
                if(ar){
                    pp++;Val i1=expr();if(err)return;Val i2=vint(0),i3=vint(0);
                    if(*pp==','){pp++;i2=expr();if(err)return;}
                    if(*pp==','){pp++;i3=expr();if(err)return;}
                    sw();if(*pp!=')'){berr("SYNTAX");return;}pp++;
                    sw();if(*pp!='='){berr("SYNTAX");return;}pp++;
                    int idx=arridx(ar,i1.i,i2.i,i3.i);if(idx<0)return;
                    if(isstrname(nm)){Val v=strexpr();if(!err){if(aelems[idx].ty==TY_STR&&aelems[idx].si>=0)sfree(aelems[idx].si);aelems[idx]=v;}}
                    else{Val v=expr();if(!err)aelems[idx]=v;}
                    return;
                }
            }
            else if(*pp=='='){
                pp++;
                if(isstrname(nm)){Val v=strexpr();if(!err)varset(nm,v);}
                else{Val v=expr();if(!err)varset(nm,v);}
                return;
            }
        }
        pp=save_pp;
    }

