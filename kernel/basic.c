#include "basic.h"

/* x87 math */
static double x87_sin(double x)  { double r; __asm__("fsin"  :"=t"(r):"0"(x)); return r; }
static double x87_cos(double x)  { double r; __asm__("fcos"  :"=t"(r):"0"(x)); return r; }
static double x87_sqrt(double x) { double r; __asm__("fsqrt" :"=t"(r):"0"(x)); return r; }
static double x87_log(double x)  { double r; __asm__("fldln2\nfxch\nfyl2x":"=t"(r):"0"(x)); return r; }
static double x87_exp(double x)  {
    double r;
    __asm__("fldl2e\nfmulp\nfld1\nfscale\nfxch\nfprem\nf2xm1\nfld1\nfaddp\nfscale\nfstp %%st(1)"
            :"=t"(r):"0"(x));
    return r;
}
static double x87_tan(double x)  { return x87_sin(x)/x87_cos(x); }
static double x87_atn(double x)  { double r; __asm__("fpatan":"=t"(r):"0"(x),"u"(1.0)); return r; }
static double x87_abs(double x)  { return x<0.0?-x:x; }
static double x87_int(double x)  { double r; __asm__("frndint":"=t"(r):"0"(x)); return r; }
static double x87_fix(double x)  { return x<0.0?-x87_int(-x):x87_int(x); }
static int32_t x87_sgn(double x) { return (x>0.0)-(x<0.0); }

static uint32_t rng_state=12345;
static double rnd(void) {
    rng_state=rng_state*1664525u+1013904223u;
    return (double)(rng_state>>1)/2147483648.0;
}

/* ── value type ── */
#define TY_INT  0
#define TY_FLOAT 1
#define TY_STR  2
typedef struct { int ty; int32_t i; double f; int si; } Val; /* si = string pool index */

/* ── string pool: fixed 64 slots, max 128 chars each ── */
#define NSTRS  64
#define SLEN  128
static char spool[NSTRS][SLEN];
static int  sused[NSTRS];

static int snew(void) {
    for(int i=0;i<NSTRS;i++) if(!sused[i]){ sused[i]=1; spool[i][0]=0; return i; }
    return -1;
}
static void sfree(int i) { if(i>=0&&i<NSTRS) sused[i]=0; }

static int slen(int i)  {
    if(i<0) return 0;
    int n=0; const char *s=spool[i]; while(*s++) n++; return n;
}
static Val vstr(int si) { Val v; v.ty=TY_STR; v.si=si; v.i=0; v.f=0.0; return v; }
static Val vint(int32_t i) { Val v; v.ty=TY_INT;  v.i=i; v.f=(double)i; v.si=-1; return v; }
static Val vflt(double  f) { Val v; v.ty=TY_FLOAT;v.f=f; v.i=(int32_t)f;v.si=-1; return v; }

static Val vadd(Val a,Val b){
    if(a.ty==TY_STR&&b.ty==TY_STR){
        int r=snew(); if(r<0) return a;
        char *d=spool[r]; const char *p=spool[a.si]; while(*p) *d++=*p++;
        p=spool[b.si]; while(*p&&d-spool[r]<SLEN-1) *d++=*p++;
        *d=0; return vstr(r);
    }
    return (a.ty==TY_FLOAT||b.ty==TY_FLOAT)?vflt(a.f+b.f):vint(a.i+b.i);
}
static Val vsub(Val a,Val b){ return (a.ty==TY_FLOAT||b.ty==TY_FLOAT)?vflt(a.f-b.f):vint(a.i-b.i); }
static Val vmul(Val a,Val b){ return (a.ty==TY_FLOAT||b.ty==TY_FLOAT)?vflt(a.f*b.f):vint(a.i*b.i); }
static Val vdiv(Val a,Val b){
    if(b.ty==TY_INT&&b.i==0){return vflt(0.0);}
    if(a.ty==TY_FLOAT||b.ty==TY_FLOAT) return vflt(a.f/b.f);
    if(a.i%b.i==0) return vint(a.i/b.i);
    return vflt((double)a.f/b.f);
}
static Val vmod(Val a,Val b){ return vint(a.i%(b.i?b.i:1)); }
static int vcmp(Val a,Val b){
    if(a.ty==TY_STR&&b.ty==TY_STR){
        const char *p=spool[a.si],*q=spool[b.si];
        while(*p&&*p==*q){p++;q++;}
        return (*p>*q)-(*p<*q);
    }
    double d=a.f-b.f; return (d>0)-(d<0);
}

static void vprint(Val v){
    if(v.ty==TY_STR){ term_puts(spool[v.si]); return; }
    if(v.ty==TY_INT) term_puti(v.i); else term_putf(v.f);
}

/* ── var table ── */
#define MAXVARS 128
#define NAMELEN  32
typedef struct { char name[NAMELEN]; Val val; int is_str; } Var;
static Var vars[MAXVARS]; static int nvars=0;

static int bstreq(const char *a,const char *b){
    while(*a&&*a==*b){a++;b++;} return !*a&&!*b;
}
static void bstrcpy(char *d,const char *s,int max){
    int i=0; while(*s&&i<max-1){*d++=*s++;i++;} *d=0;
}
static void upr(char *s){ for(;*s;s++) if(*s>='a'&&*s<='z') *s-=32; }

static Var *var_get(const char *nm) {
    for(int i=0;i<nvars;i++){
        const char *a=vars[i].name,*b=nm;
        while(*a&&*a==*b){a++;b++;}
        if(!*a&&!*b) return &vars[i];
    }
    return 0;
}
static Var *var_set(const char *nm, Val v) {
    Var *vp=var_get(nm);
    if(vp){
        /* free old string slot if replacing */
        if(vp->val.ty==TY_STR&&v.ty!=TY_STR) sfree(vp->val.si);
        vp->val=v; return vp;
    }
    if(nvars>=MAXVARS) return 0;
    Var *nv=&vars[nvars++];
    int i=0; while(nm[i]&&i<NAMELEN-1){nv->name[i]=nm[i];i++;} nv->name[i]=0;
    nv->is_str=(nm[i-1]=='$');
    nv->val=v; return nv;
}

/* ── program store ── */
#define MAX_LINES 1024
#define LINE_LEN   128
typedef struct { uint16_t num; char txt[LINE_LEN]; } Line;
static Line P[MAX_LINES]; static int Pn=0;

/* ── runtime state ── */
#define CALL_DEPTH 64
#define FOR_DEPTH  32
#define WHILE_DEPTH 32
#define IBUF 256
typedef struct { char var[NAMELEN]; Val lim,step; int ret; } ForFrame;
typedef struct { int ret; } WhileFrame;

static int   pc=0,running=0,err=0;
static int   cstk[CALL_DEPTH]; static int csp=0;
static ForFrame  fstk[FOR_DEPTH];  static int fsp=0;
static WhileFrame wstk[WHILE_DEPTH]; static int wsp=0;
static char  ibuf[IBUF];
static const char *pp;
static int   data_line=0,data_col=0;

static void sw(void){ while(*pp==' '||*pp=='	') pp++; }

static void berr(const char *m) {
    if(err) return;
    err=1; running=0;
    term_set_color(VGA_LIGHT_RED,VGA_BLACK);
    term_puts("\n? "); term_puts(m); term_putchar('\n');
    term_set_color(VGA_LIGHT_GREY,VGA_BLACK);
}

static int kw(const char *k) {
    const char *p=pp;
    while(*k){char a=*p,b=*k; if(a>='a'&&a<='z')a-=32; if(a!=b)return 0; p++;k++;}
    char nx=*p;
    if((nx>='A'&&nx<='Z')||(nx>='a'&&nx<='z')||(nx>='0'&&nx<='9')||nx=='_') return 0;
    pp=p; return 1;
}

static int read_name(char *out, int max) {
    sw();
    char c=*pp;
    if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z'))) return 0;
    int i=0;
    while((*pp>='A'&&*pp<='Z')||(*pp>='a'&&*pp<='z')||(*pp>='0'&&*pp<='9')||*pp=='_'||(*pp=='$'&&i>0&&pp[1]!='(')) {
        char ch=*pp++;
        if(ch>='a'&&ch<='z') ch-=32;
        if(i<max-1) out[i++]=ch;
    }
    out[i]=0;
    return i>0;
}

static Val expr(void);

static int pnum(double *o) {
    sw();
    if(!(*pp>='0'&&*pp<='9')&&!(*pp=='.'&&pp[1]>='0'&&pp[1]<='9')) return 0;
    double v=0; int fdiv=1,frac=0;
    while(*pp>='0'&&*pp<='9') v=v*10+(*pp++-'0');
    if(*pp=='.'){pp++; while(*pp>='0'&&*pp<='9'){v=v*10+(*pp++-'0');fdiv*=10;frac=1;}}
    if(frac) v/=fdiv;
    if(*pp=='E'||*pp=='e'){
        pp++; int neg=0,ex=0;
        if(*pp=='-'){neg=1;pp++;} else if(*pp=='+') pp++;
        while(*pp>='0'&&*pp<='9') ex=ex*10+(*pp++-'0');
        double m=1; for(int i=0;i<ex;i++) m*=10.0;
        if(neg) v/=m; else v*=m;
    }
    *o=v; return 1;
}

/* str$ expression: returns Val with TY_STR */
static Val strexpr(void);

/* string function call — pp is just past the name, pointing at '(' */
static Val call_strfn(const char *nm) {
    sw(); if(*pp!='('){berr("SYNTAX");return vstr(-1);}
    pp++;
    if(bstreq(nm,"LEFT$")) {
        Val s=strexpr(); if(err) return vstr(-1);
        sw(); if(*pp!=','){berr("SYNTAX");return vstr(-1);} pp++;
        Val n=expr(); if(err) return vstr(-1);
        sw(); if(*pp!=')'){berr("SYNTAX");return vstr(-1);} pp++;
        int r=snew(); if(r<0){berr("MEM");return vstr(-1);}
        int cnt=n.i<0?0:n.i; const char *src=spool[s.si];
        int i=0; while(*src&&i<cnt&&i<SLEN-1) spool[r][i++]=*src++;
        spool[r][i]=0;
        if(s.si>=0&&s.ty==TY_STR) sfree(s.si);
        return vstr(r);
    }
    if(bstreq(nm,"RIGHT$")) {
        Val s=strexpr(); if(err) return vstr(-1);
        sw(); if(*pp!=','){berr("SYNTAX");return vstr(-1);} pp++;
        Val n=expr(); if(err) return vstr(-1);
        sw(); if(*pp!=')'){berr("SYNTAX");return vstr(-1);} pp++;
        int r=snew(); if(r<0){berr("MEM");return vstr(-1);}
        const char *src=spool[s.si]; int l=0; const char *p=src; while(*p++) l++;
        int start=l-n.i; if(start<0) start=0;
        int i=0; while(src[start]&&i<SLEN-1) spool[r][i++]=src[start++];
        spool[r][i]=0;
        if(s.si>=0&&s.ty==TY_STR) sfree(s.si);
        return vstr(r);
    }
    if(bstreq(nm,"MID$")) {
        Val s=strexpr(); if(err) return vstr(-1);
        sw(); if(*pp!=','){berr("SYNTAX");return vstr(-1);} pp++;
        Val start=expr(); if(err) return vstr(-1);
        int cnt=-1;
        sw(); if(*pp==','){pp++; Val n=expr(); if(err) return vstr(-1); cnt=n.i;}
        sw(); if(*pp!=')'){berr("SYNTAX");return vstr(-1);} pp++;
        int r=snew(); if(r<0){berr("MEM");return vstr(-1);}
        const char *src=spool[s.si]; int st=start.i-1; if(st<0) st=0;
        int i=0;
        while(*src&&i<st) {src++;i++;} i=0;
        while(*src&&i<SLEN-1&&(cnt<0||i<cnt)) spool[r][i++]=*src++;
        spool[r][i]=0;
        if(s.si>=0&&s.ty==TY_STR) sfree(s.si);
        return vstr(r);
    }
    if(bstreq(nm,"STR$")) {
        Val n=expr(); if(err) return vstr(-1);
        sw(); if(*pp!=')'){berr("SYNTAX");return vstr(-1);} pp++;
        int r=snew(); if(r<0){berr("MEM");return vstr(-1);}
        /* format number into spool[r] via term_putf equivalent */
        char *d=spool[r]; int i=0;
        if(n.ty==TY_FLOAT){
            /* simple sprintf-equivalent for double */
            double f=n.f; if(f<0){*d++='-';f=-f;i++;}
            int32_t ip=(int32_t)f; double fp=f-(double)ip;
            /* int part */
            char tmp[12]; int ti=0;
            if(!ip){tmp[ti++]='0';} else {int32_t t=ip;while(t){tmp[ti++]='0'+(t%10);t/=10;}}
            while(ti>0&&i<SLEN-1) spool[r][i++]=tmp[--ti];
            /* frac part */
            int fdig=0; char fb[9]; int fi=0;
            double fp2=fp;
            for(int k=0;k<8;k++){fp2*=10;int d2=(int)fp2;fb[fi++]='0'+d2;fp2-=d2;}
            while(fi>0&&fb[fi-1]=='0') fi--;
            if(fi>0){spool[r][i++]='.'; for(int k=0;k<fi&&i<SLEN-1;k++) spool[r][i++]=fb[k];}
            (void)fdig;
        } else {
            int32_t v=n.i; if(v<0){spool[r][i++]='-';v=-v;}
            char tmp[12]; int ti=0;
            if(!v){tmp[ti++]='0';} else {int32_t t=v;while(t){tmp[ti++]='0'+(t%10);t/=10;}}
            while(ti>0&&i<SLEN-1) spool[r][i++]=tmp[--ti];
        }
        spool[r][i]=0; return vstr(r);
    }
    if(bstreq(nm,"CHR$")) {
        Val n=expr(); if(err) return vstr(-1);
        sw(); if(*pp!=')'){berr("SYNTAX");return vstr(-1);} pp++;
        int r=snew(); if(r<0){berr("MEM");return vstr(-1);}
        spool[r][0]=(char)n.i; spool[r][1]=0; return vstr(r);
    }
    /* LEN, VAL, ASC as numeric functions but listed here */
    berr("UNKNOWN FUNCTION"); return vstr(-1);
}

static Val numfn(const char *nm) {
    sw(); if(*pp!='('){berr("SYNTAX");return vint(0);}
    pp++;
    if(bstreq(nm,"LEN")) {
        Val s=strexpr(); if(err) return vint(0);
        sw(); if(*pp!=')'){berr("SYNTAX");return vint(0);} pp++;
        int l=slen(s.si);
        if(s.ty==TY_STR) sfree(s.si);
        return vint(l);
    }
    if(bstreq(nm,"VAL")) {
        Val s=strexpr(); if(err) return vint(0);
        sw(); if(*pp!=')'){berr("SYNTAX");return vint(0);} pp++;
        const char *p=spool[s.si]; while(*p==' ') p++;
        double n=0; int frac=0,fdiv=1;
        int neg=(*p=='-'); if(neg) p++;
        while(*p>='0'&&*p<='9') n=n*10+(*p++-'0');
        if(*p=='.'){p++;while(*p>='0'&&*p<='9'){n=n*10+(*p++-'0');fdiv*=10;frac=1;}}
        if(frac) n/=fdiv;
        if(neg) n=-n;
        if(s.ty==TY_STR) sfree(s.si);
        return (n==(double)(int32_t)n)?vint((int32_t)n):vflt(n);
    }
    if(bstreq(nm,"ASC")) {
        Val s=strexpr(); if(err) return vint(0);
        sw(); if(*pp!=')'){berr("SYNTAX");return vint(0);} pp++;
        int c=(unsigned char)spool[s.si][0];
        if(s.ty==TY_STR) sfree(s.si);
        return vint(c);
    }
    /* numeric math functions */
    Val a=expr(); if(err) return vint(0);
    Val second; int has_second=0;
    sw(); if(*pp==','){pp++;second=expr();if(err)return vint(0);has_second=1;}
    sw(); if(*pp!=')'){berr("SYNTAX");return vint(0);} pp++;
    if(bstreq(nm,"MOD")){ if(!has_second){berr("SYNTAX");return vint(0);} return vmod(a,second); }
    if(bstreq(nm,"SIN"))  return vflt(x87_sin(a.f));
    if(bstreq(nm,"COS"))  return vflt(x87_cos(a.f));
    if(bstreq(nm,"TAN"))  return vflt(x87_tan(a.f));
    if(bstreq(nm,"ATN"))  return vflt(x87_atn(a.f));
    if(bstreq(nm,"EXP"))  return vflt(x87_exp(a.f));
    if(bstreq(nm,"LOG"))  { if(a.f<=0){berr("MATH");return vint(0);} return vflt(x87_log(a.f)); }
    if(bstreq(nm,"SQR"))  { if(a.f<0) {berr("MATH");return vint(0);} return vflt(x87_sqrt(a.f));}
    if(bstreq(nm,"ABS"))  return (a.ty==TY_FLOAT)?vflt(x87_abs(a.f)):vint(a.i<0?-a.i:a.i);
    if(bstreq(nm,"INT"))  return vflt(x87_int(a.f));
    if(bstreq(nm,"FIX"))  return vflt(x87_fix(a.f));
    if(bstreq(nm,"SGN"))  return vint(x87_sgn(a.f));
    if(bstreq(nm,"RND"))  return vflt(rnd());
    if(bstreq(nm,"CINT")) return vint((int32_t)(a.f+0.5));
    if(bstreq(nm,"CDBL")) return vflt(a.f);
    berr("UNKNOWN FUNCTION"); return vint(0);
}

static int isstrname(const char *nm){
    int i=0; while(nm[i]) i++; return i>0&&nm[i-1]=='$';
}

static Val strexpr(void) {
    sw();
    Val a;
    if(*pp=='"') {
        pp++;
        int r=snew(); if(r<0){berr("MEM");return vstr(-1);}
        int i=0; while(*pp&&*pp!='"'&&i<SLEN-1) spool[r][i++]=*pp++;
        if(*pp=='"') pp++;
        spool[r][i]=0; a=vstr(r);
    } else {
        char nm[NAMELEN]; const char *save=pp;
        if(read_name(nm,NAMELEN)&&isstrname(nm)) {
            sw();
            if(*pp=='(') { a=call_strfn(nm); }
            else {
                Var *vp=var_get(nm);
                if(!vp||vp->val.ty!=TY_STR){
                    int r=snew(); if(r<0){berr("MEM");return vstr(-1);}
                    spool[r][0]=0; a=vstr(r);
                } else {
                    /* return a copy so caller can free safely */
                    int r=snew(); if(r<0){berr("MEM");return vstr(-1);}
                    bstrcpy(spool[r],spool[vp->val.si],SLEN); a=vstr(r);
                }
            }
        } else { pp=save; berr("TYPE MISMATCH"); return vstr(-1); }
    }
    /* string concatenation with + */
    for(;;){
        sw(); if(*pp!='+') break;
        pp++; Val b=strexpr(); if(err) break;
        Val c=vadd(a,b);
        sfree(a.si); sfree(b.si);
        a=c;
    }
    return a;
}

static Val fact(void) {
    sw(); if(err) return vint(0);
    if(*pp=='(') { pp++; Val v=expr(); sw(); if(*pp!=')'){berr("SYNTAX");return vint(0);} pp++; return v; }
    if(*pp=='-') { pp++; Val v=fact(); v.f=-v.f; v.i=-v.i; return v; }
    if(*pp=='+') { pp++; return fact(); }
    double n; if(pnum(&n)) return (n==(double)(int32_t)n)?vint((int32_t)n):vflt(n);
    char nm[NAMELEN]; const char *save=pp;
    if(read_name(nm,NAMELEN)) {
        sw();
        if(*pp=='(') {
            if(isstrname(nm)) return call_strfn(nm);
            return numfn(nm);
        }
        if(bstreq(nm,"RND")) return vflt(rnd());
        Var *vp=var_get(nm);
        return vp ? vp->val : vint(0);
    }
    pp=save; berr("SYNTAX"); return vint(0);
}

static Val term_expr(void) {
    Val a=fact();
    while(!err){ sw();
        if(*pp=='*'){pp++;a=vmul(a,fact());}
        else if(*pp=='/'){pp++;Val b=fact();if(b.ty==TY_INT&&b.i==0){berr("DIV/0");return vint(0);}a=vdiv(a,b);}
        else if(*pp=='\\'){pp++;Val b=fact();if(!b.i&&b.ty==TY_INT){berr("DIV/0");return vint(0);}a=vint(a.i/b.i);}
        else break;
    }
    return a;
}
static Val expr(void) {
    Val a=term_expr();
    while(!err){ sw();
        if(*pp=='+'){pp++;a=vadd(a,term_expr());}
        else if(*pp=='-'){pp++;a=vsub(a,term_expr());}
        else break;
    }
    return a;
}

static int relop(void){
    sw();
    if(*pp=='='){pp++;return 1;}
    if(*pp=='<'){pp++;if(*pp=='>'){pp++;return 2;}if(*pp=='='){pp++;return 4;}return 3;}
    if(*pp=='>'){pp++;if(*pp=='='){pp++;return 6;}return 5;}
    return 0;
}
static int applyrel(int op,Val a,Val b){
    int c=vcmp(a,b);
    switch(op){case 1:return c==0;case 2:return c!=0;case 3:return c<0;
               case 4:return c<=0;case 5:return c>0;case 6:return c>=0;}
    return 0;
}

static int findge(int n){
    for(int i=0;i<Pn;i++) if(P[i].num>=(uint16_t)n) return i;
    return Pn;
}
static void storeline(int n,const char *t){
    int lo=0,hi=Pn,idx=0,found=0;
    while(lo<hi){int m=(lo+hi)/2;
        if(P[m].num==(uint16_t)n){found=1;idx=m;break;}
        if(P[m].num<(uint16_t)n) lo=m+1; else hi=m;}
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
    P[idx].num=(uint16_t)n; bstrcpy(P[idx].txt,t,LINE_LEN);
}

static void stmt(const char *line);

static void stmt(const char *line) {
    pp=line; sw(); if(!*pp) return;
    if(kw("REM")) return;
    if(kw("CLEAR"))  { term_screen_reset(); return; }
    if(kw("HALT"))   { term_halt(); return; }
    if(kw("NEW"))    { Pn=0; nvars=0; csp=0; fsp=0; wsp=0; data_line=0; data_col=0;
                       for(int i=0;i<NSTRS;i++) sused[i]=0;
                       return; }
    if(kw("END"))    { running=0; return; }

    if(kw("LIST")) {
        if(!Pn){ term_set_color(VGA_DARK_GREY,VGA_BLACK); term_puts("  (empty)\n"); }
        for(int i=0;i<Pn;i++){
            term_set_color(VGA_YELLOW,VGA_BLACK); term_puti(P[i].num); term_putchar(' ');
            term_set_color(VGA_LIGHT_GREY,VGA_BLACK); term_puts(P[i].txt); term_putchar('\n');
        }
        term_set_color(VGA_LIGHT_GREY,VGA_BLACK); return;
    }
    if(kw("RUN")) {
        nvars=0; csp=0; fsp=0; wsp=0; data_line=0; data_col=0; pc=0; running=1;
        for(int i=0;i<NSTRS;i++) sused[i]=0;
        return;
    }
    if(kw("GOTO")) {
        sw(); Val n=expr(); if(err) return;
        int i=findge((int)n.i);
        if(i>=Pn||P[i].num!=(uint16_t)n.i){berr("LINE NOT FOUND");return;}
        pc=i; return;
    }
    if(kw("GOSUB")) {
        sw(); Val n=expr(); if(err) return;
        int i=findge((int)n.i);
        if(i>=Pn||P[i].num!=(uint16_t)n.i){berr("LINE NOT FOUND");return;}
        if(csp>=CALL_DEPTH){berr("GOSUB OVERFLOW");return;}
        cstk[csp++]=pc; pc=i; return;
    }
    if(kw("RETURN")) {
        if(csp<=0){berr("RETURN WITHOUT GOSUB");return;}
        pc=cstk[--csp]; return;
    }
    if(kw("FOR")) {
        sw(); char nm[NAMELEN]; if(!read_name(nm,NAMELEN)){berr("SYNTAX");return;}
        sw(); if(*pp!='='){berr("SYNTAX");return;} pp++;
        Val from=expr(); if(err) return;
        sw(); if(!kw("TO")){berr("SYNTAX");return;}
        Val to=expr(); if(err) return;
        Val step=vint(1); sw(); if(kw("STEP")){step=expr();if(err)return;}
        if(!step.i&&!step.f){berr("STEP=0");return;}
        var_set(nm,from);
        int go=(step.f>0)?vcmp(from,to)<=0:vcmp(from,to)>=0;
        if(!go){
            int depth=0;
            while(pc<Pn){
                pp=P[pc].txt; sw();
                const char *sp=pp;
                if(kw("FOR")) depth++;
                else{ pp=sp; if(kw("NEXT")){if(!depth){pc++;break;}depth--;} else pp=sp; }
                pc++;
            }
            return;
        }
        if(fsp>=FOR_DEPTH){berr("FOR OVERFLOW");return;}
        ForFrame *f=&fstk[fsp++];
        bstrcpy(f->var,nm,NAMELEN); f->lim=to; f->step=step; f->ret=pc;
        return;
    }
    if(kw("NEXT")) {
        if(fsp<=0){berr("NEXT WITHOUT FOR");return;}
        ForFrame *f=&fstk[fsp-1];
        sw(); char nm[NAMELEN];
        if(read_name(nm,NAMELEN)&&!bstreq(nm,f->var)){berr("NEXT MISMATCH");return;}
        Var *vp=var_get(f->var); if(!vp){berr("NEXT VAR LOST");return;}
        vp->val=vadd(vp->val,f->step);
        int go=(f->step.f>0)?vcmp(vp->val,f->lim)<=0:vcmp(vp->val,f->lim)>=0;
        if(go) pc=f->ret; else fsp--;
        return;
    }
    if(kw("WHILE")) {
        Val cond=expr(); if(err) return;
        if(cond.i||cond.f!=0.0){
            if(wsp>=WHILE_DEPTH){berr("WHILE OVERFLOW");return;}
            wstk[wsp++].ret=pc-1;
        } else {
            int depth=0;
            while(pc<Pn){
                pp=P[pc].txt; sw();
                const char *sp=pp;
                if(kw("WHILE")) depth++;
                else{ pp=sp; if(kw("WEND")){if(!depth){pc++;break;}depth--;} else pp=sp; }
                pc++;
            }
        }
        return;
    }
    if(kw("WEND")) {
        if(wsp<=0){berr("WEND WITHOUT WHILE");return;}
        pc=wstk[--wsp].ret; return;
    }
    if(kw("IF")) {
        /* evaluate condition: expr relop expr  or  expr (bool) */
        Val a=expr(); if(err) return;
        int op=relop(); Val cond;
        if(op){ Val b=expr(); if(err) return; cond=vint(applyrel(op,a,b)); }
        else cond=a;
        sw(); kw("THEN"); sw();
        /* single-line: THEN linenum */
        if(*pp>='0'&&*pp<='9'){
            if(!cond.i&&!cond.f) return;
            Val n=expr(); if(err) return;
            int i=findge((int)n.i);
            if(i>=Pn||P[i].num!=(uint16_t)n.i){berr("LINE NOT FOUND");return;}
            pc=i; return;
        }
        /* single-line: THEN stmt */
        if(*pp&&*pp!='\0'){
            if(cond.i||cond.f!=0.0) stmt(pp);
            return;
        }
        /* multi-line */
        if(!cond.i&&cond.f==0.0){
            int depth=0;
            while(pc<Pn){
                pp=P[pc].txt; sw();
                const char *sp=pp;
                if(kw("IF")) depth++;
                else{ pp=sp;
                    if(kw("ELSE")&&depth==0){pc++;return;}
                    else{ pp=sp;
                        if(kw("END")){sw();if(kw("IF")){if(!depth){pc++;return;}depth--;}}
                        else pp=sp;
                    }
                }
                pc++;
            }
        }
        return;
    }
    if(kw("ELSE")) {
        int depth=0;
        while(pc<Pn){
            pp=P[pc].txt; sw();
            const char *sp=pp;
            if(kw("IF")) depth++;
            else{ pp=sp;
                if(kw("END")){sw();if(kw("IF")){if(!depth){pc++;return;}depth--;}}
                else pp=sp;
            }
            pc++;
        }
        return;
    }
    if(kw("END")) {
        sw(); if(kw("IF")||kw("WHILE")) return;
        running=0; return;
    }

    if(kw("PRINT")) {
        sw(); if(!*pp){term_putchar('\n');return;}
        int nl=1;
        while(!err){
            sw(); if(!*pp) break;
            if(*pp=='"'){
                pp++; while(*pp&&*pp!='"') term_putchar(*pp++);
                if(*pp=='"') pp++;
            } else {
                /* peek: is it a string expression or numeric? */
                const char *save=pp;
                char nm[NAMELEN];
                int is_s=0;
                if(read_name(nm,NAMELEN)){
                    sw();
                    if(isstrname(nm)&&(*pp=='('||*pp==','||*pp==';'||*pp=='\0'||(*pp>='A'&&*pp<='Z')))
                        is_s=1;
                    pp=save;
                } else pp=save;
                if(is_s){ Val v=strexpr(); if(!err){ term_puts(spool[v.si]); sfree(v.si); } }
                else { Val v=expr(); if(!err) vprint(v); }
            }
            sw(); nl=1;
            if(*pp==';'){pp++;nl=0;}
            else if(*pp==','){pp++;term_putchar('\t');nl=0;}
            else break;
        }
        if(nl&&!err) term_putchar('\n');
        return;
    }

    if(kw("INPUT")) {
        sw();
        if(*pp=='"'){
            pp++; while(*pp&&*pp!='"') term_putchar(*pp++);
            if(*pp=='"') pp++;
            sw(); if(*pp==','||*pp==';') pp++;
        } else { term_putchar('?'); term_putchar(' '); }
        for(;;){
            sw(); char nm[NAMELEN];
            if(!read_name(nm,NAMELEN)){berr("SYNTAX");return;}
            char lb[IBUF]; term_get_line(lb,IBUF);
            if(isstrname(nm)){
                int r=snew(); if(r<0){berr("MEM");return;}
                bstrcpy(spool[r],lb,SLEN);
                var_set(nm,vstr(r));
            } else {
                upr(lb);
                const char *sv=pp; pp=lb; sw();
                double n=0; pnum(&n);
                var_set(nm,(n==(double)(int32_t)n)?vint((int32_t)n):vflt(n));
                pp=sv;
            }
            sw(); if(*pp!=',') break;
            pp++; term_puts("? ");
        }
        return;
    }

    if(kw("DATA")) return;
    if(kw("READ")) {
        for(;;){
            sw(); char nm[NAMELEN];
            if(!read_name(nm,NAMELEN)){berr("SYNTAX");return;}
            int found=0;
            for(int i=data_line;i<Pn&&!found;i++){
                const char *tp=P[i].txt; pp=tp; sw();
                if(!kw("DATA")) continue;
                int col=0;
                while(!found){
                    sw();
                    if(col==data_col){
                        if(isstrname(nm)){
                            if(*pp=='"'){
                                pp++; int r=snew(); if(r<0){berr("MEM");return;}
                                int k=0; while(*pp&&*pp!='"'&&k<SLEN-1) spool[r][k++]=*pp++;
                                spool[r][k]=0; if(*pp=='"') pp++;
                                var_set(nm,vstr(r));
                            } else { berr("TYPE MISMATCH"); return; }
                        } else {
                            double n=0; pnum(&n);
                            var_set(nm,(n==(double)(int32_t)n)?vint((int32_t)n):vflt(n));
                        }
                        sw(); if(*pp==','){pp++;data_col++;} else{data_line=i+1;data_col=0;}
                        found=1;
                    } else {
                        /* skip item */
                        if(*pp=='"'){pp++;while(*pp&&*pp!='"')pp++;if(*pp=='"')pp++;}
                        else while(*pp&&*pp!=',')pp++;
                        if(*pp==',') pp++; else break;
                        col++;
                    }
                }
            }
            if(!found){berr("OUT OF DATA");return;}
            sw(); if(*pp!=',') break; pp++;
        }
        return;
    }
    if(kw("RESTORE")){ data_line=0; data_col=0; return; }

    /* LET (explicit or implicit) */
    {
        const char *save=pp;
        kw("LET"); sw();
        char nm[NAMELEN];
        if(read_name(nm,NAMELEN)){
            sw();
            if(*pp=='='){
                pp++;
                Val v;
                if(isstrname(nm)){ v=strexpr(); } else { v=expr(); }
                if(!err) var_set(nm,v);
                return;
            }
        }
        pp=save;
    }

    berr("WHAT?");
}

void basic_run(void) {
    for(;;){
        if(running){
            if(pc>=Pn){running=0;continue;}
            int here=pc++; err=0;
            stmt(P[here].txt);
            continue;
        }
        err=0;
        term_sync_cursor();
        term_set_color(VGA_LIGHT_GREEN,VGA_BLACK); term_puts("BFBX");
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
            stmt(p);
        }
    }
}
