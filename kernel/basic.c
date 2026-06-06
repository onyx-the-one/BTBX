/*
 * BTBX basic.c - TinyBASIC interpreter
 */
#include "basic.h"

#define MAX_LINES  512
#define LINE_LEN   128
#define CALL_DEPTH  64
#define FOR_DEPTH   32
#define IBUF        256
typedef int32_t V;

typedef struct { uint16_t num; char txt[LINE_LEN]; } Line;
typedef struct { int var; V lim, step; int ret; } Frame;

static Line  P[MAX_LINES]; static int Pn=0;
static V     vars[26];
static int   pc=0, running=0;
static int   cstk[CALL_DEPTH]; static int csp=0;
static Frame fstk[FOR_DEPTH];  static int fsp=0;
static char  ibuf[IBUF];
static const char *pp;
static int   err=0;

/*  string helpers  */
static void bcp(char *d,const char *s){ while((*d++=*s++)); }
static void upr(char *s){ for(;*s;s++) if(*s>='a'&&*s<='z') *s-=32; }

static V isqrt(V n){ if(n<=0)return 0; V x=n,y=1;
    while(x>y){x=(x+y)/2;y=n/x;} return x; }

static void berr(const char *m){
	    if(err) return;
    err=1; running=0;
    term_set_color(VGA_LIGHT_RED,VGA_BLACK);
    term_puts("\n  ? "); term_puts(m); term_putchar('\n');
    term_set_color(VGA_LIGHT_GREY,VGA_BLACK);
}
static void sw(void){ while(*pp==' '||*pp=='\t') pp++; }
static int kw(const char *k){
	    const char *p=pp;
    while(*k){ if(*p!=*k) return 0; p++; k++; }
    char b=*p;
    if((b>='A'&&b<='Z')||(b>='0'&&b<='9')||(b>='a'&&b<='z')) return 0;
    pp=p; return 1;
}
static int pnum(V *o){
	    if(*pp<'0'||*pp>'9') return 0;
    V v=0; while(*pp>='0'&&*pp<='9') v=v*10+(*pp++-'0'); *o=v; return 1;
}

static V expr(void);
static V term(void);
static V fact(void);

static V fact(void){
	    sw(); if(err) return 0;
    if(kw("ABS")){
	        sw(); if(*pp!='('){berr("SYNTAX");return 0;} pp++;
        V v=expr(); sw(); if(*pp!=')'){berr("SYNTAX");return 0;} pp++;
        return v<0?-v:v;
    }
    if(kw("SQR")){
	        sw(); if(*pp!='('){berr("SYNTAX");return 0;} pp++;
        V v=expr(); sw(); if(*pp!=')'){berr("SYNTAX");return 0;} pp++;
        if(v<0){berr("MATH");return 0;} return isqrt(v);
    }
    if(*pp=='('){ pp++; V v=expr(); sw();
        if(*pp!=')'){berr("SYNTAX");return 0;} pp++; return v; }
    if(*pp=='-'){ pp++; return -fact(); }
    if(*pp=='+'){ pp++; return  fact(); }
    if(*pp>='A'&&*pp<='Z'){
	        int i=*pp-'A'; pp++;
        char n=*pp;
        if((n>='A'&&n<='Z')||(n>='0'&&n<='9')){berr("SYNTAX");return 0;}
        return vars[i];
    }
    V v; if(pnum(&v)) return v;
    berr("SYNTAX"); return 0;
}
static V term(void){
	    V a=fact();
    while(!err){ sw();
        if(*pp=='*'){pp++; a*=fact();}
        else if(*pp=='/'){ pp++; V b=fact();
            if(!b){berr("DIV/0");return 0;} a/=b; }
        else break; }
    return a;
}
static V expr(void){
	    V a=term();
    while(!err){ sw();
        if(*pp=='+'){pp++; a+=term();}
        else if(*pp=='-'){pp++; a-=term();}
        else break; }
    return a;
}

static int relop(void){
	    sw();
    if(*pp=='='){ pp++; return 1; }
    if(*pp=='<'){ pp++;
        if(*pp=='>'){ pp++; return 2; }
        if(*pp=='='){ pp++; return 4; }
        return 3; }
    if(*pp=='>'){ pp++;
        if(*pp=='='){ pp++; return 6; }
        return 5; }
    return 0;
}
static int applyrel(int op, V a, V b){
	    switch(op){
	        case 1:return a==b; case 2:return a!=b; case 3:return a<b;
        case 4:return a<=b; case 5:return a>b;  case 6:return a>=b;
    } return 0;
}

static int findge(int n){
	    for(int i=0;i<Pn;i++) if(P[i].num>=(uint16_t)n) return i;
    return Pn;
}
static void storeline(int n, const char *t){
	    int lo=0,hi=Pn,idx,found=0;
    while(lo<hi){ int m=(lo+hi)/2;
        if(P[m].num==(uint16_t)n){found=1;idx=m;break;}
        if(P[m].num<(uint16_t)n) lo=m+1; else hi=m; }
    if(!found) idx=lo;
    if(!t||!*t){
	        if(!found) return;
        for(int i=idx;i<Pn-1;i++) P[i]=P[i+1];
        Pn--; return;
    }
    if(!found){
	        if(Pn>=MAX_LINES){berr("MEM");return;}
        for(int i=Pn;i>idx;i--) P[i]=P[i-1];
        Pn++;
    }
    P[idx].num=(uint16_t)n; bcp(P[idx].txt,t);
}

static void stmt(const char *line);

static void do_next(const char *rest){
	    if(fsp<=0){berr("NEXT WITHOUT FOR");return;}
    Frame *f=&fstk[fsp-1];
    pp=rest; sw();
    if(*pp>='A'&&*pp<='Z'){ if(*pp-'A'!=f->var){berr("NEXT MISMATCH");return;} }
    vars[f->var]+=f->step;
    int go=(f->step>0)?(vars[f->var]<=f->lim):(vars[f->var]>=f->lim);
    if(go) pc=f->ret; else fsp--;
}

static void stmt(const char *line){
	    pp=line; sw(); if(!*pp) return;

    if(kw("REM")) return;
    if(kw("NEW")){ Pn=0; for(int i=0;i<26;i++) vars[i]=0; csp=0; fsp=0; return; }
    if(kw("LIST")){
	        if(!Pn){
	            term_set_color(VGA_DARK_GREY,VGA_BLACK);
            term_puts("  (empty)\n");
            term_set_color(VGA_LIGHT_GREY,VGA_BLACK);
            return;
        }
        for(int i=0;i<Pn;i++){
	            term_set_color(VGA_YELLOW,VGA_BLACK);
            term_puti(P[i].num);
            term_putchar(' ');
            term_set_color(VGA_LIGHT_GREY,VGA_BLACK);
            term_puts(P[i].txt);
            term_putchar('\n');
        }
        return;
    }
    if(kw("RUN")){ for(int i=0;i<26;i++) vars[i]=0; csp=0; fsp=0; pc=0; running=1; return; }
    if(kw("END")){ running=0; return; }
    if(kw("GOTO")){
	        sw(); V n=expr(); if(err) return;
        int i=findge((int)n);
        if(i>=Pn||P[i].num!=(uint16_t)n){berr("LINE NOT FOUND");return;}
        pc=i; return;
    }
    if(kw("GOSUB")){
	        sw(); V n=expr(); if(err) return;
        int i=findge((int)n);
        if(i>=Pn||P[i].num!=(uint16_t)n){berr("LINE NOT FOUND");return;}
        if(csp>=CALL_DEPTH){berr("GOSUB OVERFLOW");return;}
        cstk[csp++]=pc; pc=i; return;
    }
    if(kw("RETURN")){
	        if(csp<=0){berr("RETURN WITHOUT GOSUB");return;}
        pc=cstk[--csp]; return;
    }
    if(kw("FOR")){
	        sw(); if(*pp<'A'||*pp>'Z'){berr("SYNTAX");return;}
        int v=*pp-'A'; pp++;
        sw(); if(*pp!='='){berr("SYNTAX");return;} pp++;
        V from=expr(); if(err) return;
        sw(); if(!kw("TO")){berr("SYNTAX");return;}
        V to=expr(); if(err) return;
        V step=1; sw(); if(kw("STEP")){ step=expr(); if(err) return; }
        if(!step){berr("STEP=0");return;}
        vars[v]=from;
        int go=(step>0)?(from<=to):(from>=to);
        if(!go){
	            int depth=0;
            while(pc<Pn){
	                pp=P[pc].txt; sw();
                if(kw("FOR")) depth++;
                else { pp=P[pc].txt; sw(); if(kw("NEXT")){ if(!depth){pc++;break;} depth--; } }
                pc++;
            }
            return;
        }
        if(fsp>=FOR_DEPTH){berr("FOR OVERFLOW");return;}
        Frame *f=&fstk[fsp++];
        f->var=v; f->lim=to; f->step=step; f->ret=pc;
        return;
    }
    if(kw("NEXT")){ do_next(pp); return; }
    if(kw("IF")){
	        V a=expr(); if(err) return;
        int op=relop(); if(!op){berr("SYNTAX");return;}
        V b=expr(); if(err) return;
        sw(); if(!kw("THEN")){berr("SYNTAX");return;}
        if(!applyrel(op,a,b)) return;
        sw();
        if(*pp>='0'&&*pp<='9'){
	            V n=0; while(*pp>='0'&&*pp<='9') n=n*10+(*pp++-'0');
            int i=findge((int)n);
            if(i>=Pn||P[i].num!=(uint16_t)n){berr("LINE NOT FOUND");return;}
            pc=i;
        } else stmt(pp);
        return;
    }
    /* LET (implicit or explicit) */
    {
	        const char *save=pp;
        int expl=kw("LET");
        sw();
        if(*pp>='A'&&*pp<='Z'){
	            int idx=*pp-'A'; char nx=pp[1];
            if(!((nx>='A'&&nx<='Z')||(nx>='0'&&nx<='9'))){
	                pp++; sw();
                if(*pp=='='){ pp++; V v=expr();
                    if(!err) vars[idx]=v;
                    return; }
            }
        }
        if(!expl) pp=save;
    }
    if(kw("PRINT")){
	        sw();
        while(!err){
	            sw(); if(!*pp) break;
            if(*pp=='"'){
	                pp++;
                while(*pp&&*pp!='"') term_putchar(*pp++);
                if(*pp=='"') pp++;
            } else { V v=expr(); if(err) break; term_puti(v); }
            sw();
            if(*pp==';'){ pp++; }
            else if(*pp==','){ pp++; term_putchar(' '); }
            else break;
        }
        if(!err) term_putchar('\n');
        return;
    }
    if(kw("INPUT")){
	        sw();
        if(*pp=='"'){
	            pp++;
            while(*pp&&*pp!='"') term_putchar(*pp++);
            if(*pp=='"') pp++;
            sw();
            if(*pp==','||*pp==';') pp++;
        } else { term_putchar('?'); term_putchar(' '); }
        while(!err){
	            sw();
            if(*pp<'A'||*pp>'Z'){berr("SYNTAX");return;}
            int idx=*pp-'A'; pp++;
            char lb[IBUF]; term_get_line(lb,IBUF); upr(lb);
            const char *sv=pp; pp=lb; sw();
            vars[idx]=expr(); if(err){pp=sv;return;}
            pp=sv; sw(); if(*pp!=',') break;
            pp++; term_puts("? ");
        }
        return;
    }
    berr("WHAT?");
}

void basic_run(void){
	    for(;;){
	        if(running){
	            if(pc>=Pn){ running=0; continue; }
            int here=pc++; err=0;
            stmt(P[here].txt);
            continue;
        }
        err=0;
        term_set_color(VGA_LIGHT_GREEN,VGA_BLACK); term_puts("BTBX");
        term_set_color(VGA_DARK_GREY,VGA_BLACK);   term_puts("> ");
        term_set_color(VGA_WHITE,VGA_BLACK);
        term_get_line(ibuf,IBUF); upr(ibuf);
        const char *p=ibuf;
        while(*p==' '||*p=='\t') p++;
        if(!*p) continue;
        if(*p>='0'&&*p<='9'){
	            int n=0; while(*p>='0'&&*p<='9') n=n*10+(*p++-'0');
            if(n<1||n>65535){berr("BAD LINE NUMBER");continue;}
            while(*p==' '||*p=='\t') p++;
            storeline(n,p);
        } else {
	            pp=p;
            if(kw("RUN")){ for(int i=0;i<26;i++) vars[i]=0; csp=0; fsp=0; pc=0; running=1; }
            else stmt(p);
        }
    }
}
