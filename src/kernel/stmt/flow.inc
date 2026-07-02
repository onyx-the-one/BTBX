    /* GOTO */
    if(kw("GOTO")){sw();Val n=expr();if(err)return;int i=findge((int)n.i);if(i>=Pn||(int)P[i].num!=(int)n.i){berr("LINE NOT FOUND");return;}pc=i;return;}

    /* GOSUB */
    if(kw("GOSUB")){sw();Val n=expr();if(err)return;int i=findge((int)n.i);if(i>=Pn||(int)P[i].num!=(int)n.i){berr("LINE NOT FOUND");return;}if(csp>=CALLDEPTH){berr("GOSUB OVERFLOW");return;}cstk[csp++]=pc;pc=i;return;}

    if(kw("RETURN")){if(csp<=0){berr("RETURN WITHOUT GOSUB");return;}pc=cstk[--csp];return;}

    /* FOR */
    if(kw("FOR")){sw();char nm5[NAMELEN];if(!readname(nm5,NAMELEN)){berr("SYNTAX");return;}sw();if(*pp!='='){berr("SYNTAX");return;}pp++;Val from=expr();if(err)return;if(!kw("TO")){berr("SYNTAX");return;}Val to=expr();if(err)return;Val step=vint(1);if(kw("STEP")){step=expr();if(err)return;}if(!step.i&&!step.f){berr("STEP 0");return;}varset(nm5,from);int go=step.f>=0?vcmp(from,to)<=0:vcmp(from,to)>=0;if(!go){int depth=0;while(pc<Pn){const char *t=P[pc].txt;const char *sv=pp;pp=t;if(kw("FOR"))depth++;else if(kw("NEXT")){if(!depth){pp=sv;pc++;return;}depth--;}pp=sv;pc++;}return;}if(fsp>=FORDEPTH){berr("FOR OVERFLOW");return;}ForFrame *f=&fstk[fsp++];bstrcpy(f->var,nm5,NAMELEN);f->lim=to;f->step=step;f->ret=pc;return;}

    /* NEXT — search the frame stack by name so a GOTO that jumps out of an
       inner loop (skipping its NEXT) doesn't corrupt an outer loop's frame.
       Any frames above the matched one are discarded (their loops are
       considered abandoned). */
    if(kw("NEXT")){
        if(fsp<=0){berr("NEXT WITHOUT FOR");return;}
        sw();char nm6[NAMELEN];int hasname=readname(nm6,NAMELEN);
        int k=fsp-1;
        if(hasname){
            k=-1;
            for(int j=fsp-1;j>=0;j--){if(bstreq(fstk[j].var,nm6)){k=j;break;}}
            if(k<0){berr("NEXT MISMATCH");return;}
        }
        fsp=k+1;
        ForFrame *f=&fstk[k];
        Var *vp=varget(f->var);if(!vp){berr("NEXT VAR LOST");return;}
        vp->val=vadd(vp->val,f->step);
        int go=f->step.f>=0?vcmp(vp->val,f->lim)<=0:vcmp(vp->val,f->lim)>=0;
        if(go)pc=f->ret;else fsp--;
        return;
    }

    /* WHILE / WEND */
    if(kw("WHILE")){Val cond=expr();if(err)return;if(cond.i||cond.f!=0.0){if(wsp>=WHILEDEPTH){berr("WHILE OVERFLOW");return;}wstk[wsp++].ret=pc-1;}else{int depth=0;while(pc<Pn){const char *t=P[pc].txt;const char *sv=pp;pp=t;if(kw("WHILE"))depth++;else if(kw("WEND")){if(!depth){pp=sv;pc++;return;}depth--;}pp=sv;pc++;}}return;}
    if(kw("WEND")){if(wsp<=0){berr("WEND WITHOUT WHILE");return;}pc=wstk[wsp-1].ret;const char *sv=pp;pp=P[pc].txt;if(!kw("WHILE")){berr("WEND MISMATCH");pp=sv;return;}Val cond=expr();pp=sv;if(!cond.i&&cond.f==0.0)wsp--;return;}

    /* IF */
    if(kw("IF")){
        Val cond;const char *save3=pp;
        char nm7[NAMELEN];
        if(readname(nm7,NAMELEN)&&isstrname(nm7)){
            Val a=strexpr();if(err)return;int op=relop();if(!op){berr("SYNTAX");sfree(a.si);return;}Val b=strexpr();if(err){sfree(a.si);return;}cond=vint(applyrel(op,a,b));sfree(a.si);sfree(b.si);
        }else{
            pp=save3;
            Val a=expr();if(err)return;
            int op=relop();
            if(op){Val b=expr();if(err)return;cond=vint(applyrel(op,a,b));}
            else cond=a;
        }
        if(!kw("THEN")){berr("SYNTAX");return;}sw();
        if(*pp>='0'&&*pp<='9'){if(!cond.i&&!cond.f)return;Val n=expr();if(err)return;int i=findge((int)n.i);if(i>=Pn||(int)P[i].num!=(int)n.i){berr("LINE NOT FOUND");return;}pc=i;return;}
        /* single-line inline IF: "IF cond THEN <stmt>" — execute stmt if true,
           otherwise this is a no-op (there is no block to skip to). */
        if(*pp&&*pp!=' '){if(cond.i||cond.f!=0.0)stmt(pp);return;}
        /* multi-line/block IF: "IF cond THEN" with nothing else on the line —
           skip forward to matching ELSE/ENDIF when false. */
        if(!cond.i&&cond.f==0.0){int depth=0;while(pc<Pn){const char *t=P[pc].txt;const char *sv=pp;pp=t;if(kw("IF"))depth++;else if(!depth&&kw("ELSE")){pp=sv;break;}else if(!depth&&kw("ENDIF")){pp=sv;break;}else if(kw("ENDIF"))depth--;pp=sv;pc++;}return;}}
    if(kw("ELSE")||kw("ENDIF")){int depth=0;while(pc<Pn){const char *t=P[pc].txt;const char *sv=pp;pp=t;if(kw("IF"))depth++;else if(kw("ENDIF")){if(!depth){pp=sv;return;}depth--;}pp=sv;pc++;}return;}

    /* ON cond GOTO/GOSUB */
    if(kw("ON")){Val cond=expr();if(err)return;int isgosub=0;if(kw("GOSUB"))isgosub=1;else if(!kw("GOTO")){berr("SYNTAX");return;}int idx=(int)cond.i,ci=0;for(;;){sw();Val n=expr();if(err)return;ci++;if(ci==idx){int i=findge((int)n.i);if(i>=Pn||(int)P[i].num!=(int)n.i){berr("LINE NOT FOUND");return;}if(isgosub){if(csp>=CALLDEPTH){berr("GOSUB OVERFLOW");return;}cstk[csp++]=pc;}pc=i;return;}sw();if(*pp!=',')break;pp++;}return;}
