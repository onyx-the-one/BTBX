    /* PRINT / ? / PRINT #n */
    if(kw("PRINT")||*pp=='?'){
        if(*pp=='?')pp++;
        sw();
        if(*pp=='#'){
            pp++;Val fno=expr();if(err)return;sw();if(*pp==',')pp++;
            int fi=(int)fno.i-1;
            if(fi<0||fi>=MAXFILES||!files[fi].open||!files[fi].forwrite){berr("FILE NOT OPEN");return;}
            FileSlot *fs=&files[fi];int nlf=1;
            for(;;){sw();if(!*pp)break;int issf=0;const char *s2f=pp;if(*pp=='"')issf=1;else{char nmf[NAMELEN];if(readname(nmf,NAMELEN)&&isstrname(nmf))issf=1;pp=s2f;}char tmpf[SLEN];tmpf[0]=0;
                if(issf){Val v=strexpr();if(!err){bstrcpy(tmpf,spool[v.si],SLEN);sfree(v.si);}}
                else{Val v=expr();if(!err){if(v.ty==TY_STR){bstrcpy(tmpf,spool[v.si],SLEN);}else if(v.ty==TY_INT)itos(v.i,tmpf,(int)sizeof(tmpf));else ftos(v.f,tmpf,(int)sizeof(tmpf));}}
                int tlf=bstrlen(tmpf);if(fs->len+tlf<FBUFSZ-2){for(int k=0;k<tlf;k++)fs->buf[fs->len++]=tmpf[k];fs->dirty=1;}
                sw();nlf=1;
                if(*pp==';'){pp++;nlf=0;}
                else if(*pp==','){pp++;if(fs->len<FBUFSZ-2){fs->buf[fs->len++]='\t';fs->dirty=1;}nlf=0;}
                else break;
            }
            if(nlf&&fs->len<FBUFSZ-2){fs->buf[fs->len++]='\n';fs->dirty=1;}
            return;
        }
        int nl=1;
        if(!*pp){term_putchar('\n');return;}
        for(;;){sw();if(!*pp||*pp=='\n')break;int iss=0;const char *s2=pp;if(*pp=='"')iss=1;else{char nm2[NAMELEN];if(readname(nm2,NAMELEN)&&isstrname(nm2))iss=1;else if(bstreq(nm2,"INKEY"))iss=1;pp=s2;}if(iss){Val v=strexpr();if(!err){term_puts(spool[v.si]);sfree(v.si);}}else{Val v=expr();if(!err)vprint(v);}if(err)return;sw();nl=1;if(*pp==';'){pp++;nl=0;}else if(*pp==','){pp++;term_putchar('\t');nl=0;}else break;}
        if(nl&&!err)term_putchar('\n');
        return;
    }

    /* INPUT / INPUT #n */
    if(kw("INPUT")){
        sw();
        if(*pp=='#'){
            pp++;Val fno=expr();if(err)return;sw();if(*pp==',')pp++;
            int fi=(int)fno.i-1;
            if(fi<0||fi>=MAXFILES||!files[fi].open||files[fi].forwrite){berr("FILE NOT OPEN");return;}
            FileSlot *fs=&files[fi];
            for(;;){
                char nm9[NAMELEN];if(!readname(nm9,NAMELEN)){berr("SYNTAX");return;}
                while(fs->pos<fs->len&&(fs->buf[fs->pos]=='\n'||fs->buf[fs->pos]=='\r'))fs->pos++;
                if(isstrname(nm9)){
                    int r=snew();if(r<0){berr("MEM");return;}int k=0;
                    if(fs->pos<fs->len&&fs->buf[fs->pos]=='"'){fs->pos++;while(fs->pos<fs->len&&fs->buf[fs->pos]!='"'&&k<SLEN-1)spool[r][k++]=fs->buf[fs->pos++];if(fs->pos<fs->len&&fs->buf[fs->pos]=='"')fs->pos++;}
                    else{while(fs->pos<fs->len&&fs->buf[fs->pos]!=','&&fs->buf[fs->pos]!='\n'&&k<SLEN-1)spool[r][k++]=fs->buf[fs->pos++];}
                    spool[r][k]=0;varset(nm9,vstr(r));
                }else{
                    char tmp2[64];int k=0;
                    while(fs->pos<fs->len&&fs->buf[fs->pos]!=','&&fs->buf[fs->pos]!='\n'&&k<63)tmp2[k++]=fs->buf[fs->pos++];
                    tmp2[k]=0;const char *sv=pp;pp=tmp2;double n2=0;pnum(&n2);pp=sv;
                    varset(nm9,n2!=(double)(int32_t)n2?vflt(n2):vint((int32_t)n2));
                }
                if(fs->pos<fs->len&&fs->buf[fs->pos]==',')fs->pos++;
                sw();if(*pp!=',')break;pp++;
            }
            return;
        }
        if(*pp=='"'){pp++;while(*pp&&*pp!='"')term_putchar(*pp++);if(*pp=='"')pp++;sw();if(*pp==',')pp++;else{term_putchar('?');term_putchar(' ');}}
        else{term_putchar('?');term_putchar(' ');}
        for(;;){
            char nm3[NAMELEN];if(!readname(nm3,NAMELEN)){berr("SYNTAX");return;}
            char lb[IBUF];term_getline(lb,IBUF);
            if(isstrname(nm3)){int r=snew();if(r<0){berr("MEM");return;}bstrcpy(spool[r],lb,SLEN);varset(nm3,vstr(r));}
            else{upr(lb);const char *sv=pp;pp=lb;double n=0;pnum(&n);pp=sv;varset(nm3,n!=(double)(int32_t)n?vflt(n):vint((int32_t)n));}
            sw();if(*pp!=',')break;pp++;term_puts("? ");
        }
        return;
    }

    if(kw("DATA"))return;

    /* READ */
    if(kw("READ")){
        for(;;){char nm4[NAMELEN];if(!readname(nm4,NAMELEN)){berr("SYNTAX");return;}int found=0;for(int i=dataline;i<Pn&&!found;i++){const char *dp=P[i].txt;const char *save2=pp;pp=dp;if(kw("DATA")){int col=0;while(*pp&&col<datacol){while(*pp&&*pp!=',')pp++;if(*pp==','){pp++;col++;}}if(*pp){if(isstrname(nm4)){int r=snew();if(r<0){berr("MEM");pp=save2;return;}int k=0;if(*pp=='"'){pp++;while(*pp&&*pp!='"'&&k<SLEN-1)spool[r][k++]=*pp++;if(*pp=='"')pp++;}else{while(*pp&&*pp!=','&&k<SLEN-1)spool[r][k++]=*pp++;}spool[r][k]=0;varset(nm4,vstr(r));}else{double n=0;pnum(&n);varset(nm4,n!=(double)(int32_t)n?vflt(n):vint((int32_t)n));}dataline=i;datacol++;found=1;}pp=save2;}else pp=save2;}if(!found){berr("OUT OF DATA");return;}sw();if(*pp!=',')break;pp++;}return;}

    if(kw("RESTORE")){dataline=0;datacol=0;return;}

