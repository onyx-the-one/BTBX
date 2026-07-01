#include "basic.h"
#include "fat12.h"
#include "sound.h"

/* ---- x87 math ------------------------------------------------------------ */
static double x87_sin(double x) { double r; __asm__("fsin" : "=t"(r) : "0"(x)); return r; }
static double x87_cos(double x) { double r; __asm__("fcos" : "=t"(r) : "0"(x)); return r; }
static double x87_sqrt(double x) { double r; __asm__("fsqrt" : "=t"(r) : "0"(x)); return r; }
static double x87_log(double x) { double r; __asm__("fldln2\nfxch\nfyl2x" : "=t"(r) : "0"(x)); return r; }
static double x87_exp(double x) {
    double r;
    __asm__("fldl2e\nfmulp\nfld %%st(0)\nfrndint\nfxch\nfsub %%st(1),%%st(0)\n"
            "f2xm1\nfld1\nfaddp\nfscale\nffree %%st(1)" : "=t"(r) : "0"(x));
    return r;
}
static double x87_tan(double x) { return x87_sin(x) / x87_cos(x); }
static double x87_atn(double x) { double r; __asm__("fpatan" : "=t"(r) : "0"(x), "u"(1.0)); return r; }
static double x87_abs(double x) { return x < 0.0 ? -x : x; }
static double x87_int(double x) { double r; __asm__("frndint" : "=t"(r) : "0"(x)); return r; }
static double x87_fix(double x) { return x < 0.0 ? -x87_int(-x) : x87_int(x); }
static int32_t x87_sgn(double x) { return (x > 0.0) - (x < 0.0); }

/* ---- RNG ----------------------------------------------------------------- */
static uint32_t rng_state = 12345;
static double rnd(void) {
    rng_state = rng_state * 1664525u + 1013904223u;
    return (double)rng_state / 4294967296.0;
}

/* ---- Val ----------------------------------------------------------------- */
#define TY_INT   0
#define TY_FLOAT 1
#define TY_STR   2
typedef struct { int ty; int32_t i; double f; int si; } Val;

/* ---- String pool --------------------------------------------------------- */
#define NSTRS 128
#define SLEN  256
static char spool[NSTRS][SLEN];
static int  sused[NSTRS];

static int snew(void) {
    for (int i = 0; i < NSTRS; i++)
        if (!sused[i]) { sused[i] = 1; spool[i][0] = 0; return i; }
    return -1;
}
static void sfree(int i) { if (i >= 0 && i < NSTRS) sused[i] = 0; }

static Val vint(int32_t i) { Val v; v.ty=TY_INT;   v.i=i; v.f=(double)i; v.si=-1; return v; }
static Val vflt(double f)  { Val v; v.ty=TY_FLOAT; v.f=f; v.i=(int32_t)f; v.si=-1; return v; }
static Val vstr(int si)    { Val v; v.ty=TY_STR;   v.si=si; v.i=0; v.f=0; return v; }

/* ---- Arithmetic ---------------------------------------------------------- */
#define NUMVAL(v) ((v).ty==TY_FLOAT?(v).f:(double)(v).i)

static Val vadd(Val a, Val b) {
    if (a.ty==TY_STR||b.ty==TY_STR) return vint(0);
    if (a.ty==TY_FLOAT||b.ty==TY_FLOAT) return vflt(NUMVAL(a)+NUMVAL(b));
    return vint(a.i+b.i);
}
static Val vsub(Val a, Val b) {
    if (a.ty==TY_STR||b.ty==TY_STR) return vint(0);
    if (a.ty==TY_FLOAT||b.ty==TY_FLOAT) return vflt(NUMVAL(a)-NUMVAL(b));
    return vint(a.i-b.i);
}
static Val vmul(Val a, Val b) {
    if (a.ty==TY_STR||b.ty==TY_STR) return vint(0);
    if (a.ty==TY_FLOAT||b.ty==TY_FLOAT) return vflt(NUMVAL(a)*NUMVAL(b));
    return vint(a.i*b.i);
}
static Val vdiv(Val a, Val b) { return vflt(NUMVAL(a)/NUMVAL(b)); }
static Val vmod(Val a, Val b) { if (!b.i&&b.ty==TY_INT) return vint(0); return vint(a.i%b.i); }
static int vcmp(Val a, Val b) {
    if (a.ty==TY_STR&&b.ty==TY_STR) {
        const char *p=spool[a.si], *q=spool[b.si];
        while (*p&&*p==*q){p++;q++;}
        return (*p>*q)-(*p<*q);
    }
    double d=NUMVAL(a)-NUMVAL(b);
    return (d>0)-(d<0);
}
static void vprint(Val v) {
    if (v.ty==TY_STR) { term_puts(spool[v.si]); return; }
    if (v.ty==TY_INT) term_puti(v.i); else term_putf(v.f);
}

/* ---- Variable table ------------------------------------------------------ */
#define MAXVARS 256
#define NAMELEN  32
typedef struct { char name[NAMELEN]; Val val; int isstr; } Var;
static Var vars[MAXVARS];
static int nvars = 0;

/* ---- Array table --------------------------------------------------------- */
#define MAXARRAYS 32
#define MAXELEMS  512
typedef struct {
    char name[NAMELEN];
    int isstr;
    int dims;
    int size[3]; /* max index per dimension (inclusive) */
    int base;    /* index into aelems[] */
    int total;
} Array;
static Array arrays[MAXARRAYS];
static int   narrays = 0;
static Val   aelems[MAXELEMS];
static int   aused = 0;

/* ---- Open file table ----------------------------------------------------- */
#define MAXFILES 4
#define FBUFSZ   8192
typedef struct {
    int  open;
    int  forwrite;
    char nm11[12];
    char buf[FBUFSZ];
    int  len;
    int  pos;
    int  dirty;
} FileSlot;
static FileSlot files[MAXFILES];

/* ---- String helpers ------------------------------------------------------ */
static int  bstreq(const char *a, const char *b) { while(*a&&*a==*b){a++;b++;} return !*a&&!*b; }
static void bstrcpy(char *d, const char *s, int max) { int i=0; while(*s&&i<max-1) d[i++]=*s++; d[i]=0; }
static void upr(char *s) { while(*s){if(*s>='a'&&*s<='z')*s-=32; s++;} }
static int  bstrlen(const char *s) { int n=0; while(*s++) n++; return n; }

/* ---- Variable helpers ---------------------------------------------------- */
static Var *varget(const char *nm) {
    for (int i=0;i<nvars;i++) if(bstreq(vars[i].name,nm)) return &vars[i];
    return 0;
}
static Var *varset(const char *nm, Val v) {
    Var *vp=varget(nm);
    if (vp) { if(vp->val.ty==TY_STR&&v.ty!=TY_STR) sfree(vp->val.si); vp->val=v; return vp; }
    if (nvars>=MAXVARS) return 0;
    Var *nv=&vars[nvars++];
    int i=0; while(nm[i]&&i<NAMELEN-1) { nv->name[i]=nm[i]; i++; } nv->name[i]=0;
    nv->isstr=(nm[i-1]=='$');
    nv->val=v;
    return nv;
}

/* ---- Program store ------------------------------------------------------- */
#define MAXLINES 1024
#define LINELEN   128
typedef struct { uint16_t num; char txt[LINELEN]; } Line;
static Line P[MAXLINES];
static int  Pn = 0;

/* shared I/O buffer - avoids a double static for SAVE/LOAD */
static char fbuf[MAXLINES * LINELEN];

/* ---- Runtime state ------------------------------------------------------- */
#define CALL_DEPTH  64
#define FOR_DEPTH   32
#define WHILE_DEPTH 32
#define IBUF 256
typedef struct { char var[NAMELEN]; Val lim, step; int ret; } ForFrame;
typedef struct { int ret; } WhileFrame;
static int pc=0, running=0, err=0;
static int cstk[CALL_DEPTH];  static int csp=0;
static ForFrame  fstk[FOR_DEPTH];   static int fsp=0;
static WhileFrame wstk[WHILE_DEPTH]; static int wsp=0;
static char ibuf[IBUF];
static const char *pp;
static int data_line=0, data_col=0;

static void sw(void) { while(*pp==' '||*pp=='\t') pp++; }
static void berr(const char *m) {
    if (err) return;
    err=1; running=0;
    term_set_color(VGA_LIGHT_RED,VGA_BLACK);
    term_puts("? "); term_puts(m); term_putchar('\n');
    term_set_color(VGA_LIGHT_GREY,VGA_BLACK);
}
static int kw(const char *k) {
    const char *p=pp;
    while(*k){ char a=*p,b=*k; if(a>='a'&&a<='z') a-=32; if(a!=b) return 0; p++; k++; }
    char nx=*p;
    if((nx>='A'&&nx<='Z')||(nx>='a'&&nx<='z')||(nx>='0'&&nx<='9')||nx=='_') return 0;
    pp=p; return 1;
}
static int readname(char *out, int max) {
    sw();
    char c=*pp;
    if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z'))) return 0;
    int i=0;
    while ((*pp>='A'&&*pp<='Z')||(*pp>='a'&&*pp<='z')||
           (*pp>='0'&&*pp<='9')||*pp=='_') {
        char ch=*pp++;
        if (ch>='a'&&ch<='z') ch-=32;
        if (i<max-2) out[i++]=ch;
    }
    /* grab trailing $ */
    if (*pp=='$'&&i<max-1) out[i++]=*pp++;
    out[i]=0;
    return i>0;
}

/* forward decls */
static Val expr(void);
static Val strexpr(void);

/* ---- Number literal ------------------------------------------------------ */
static int pnum(double *o) {
    sw();
    if (!((*pp>='0'&&*pp<='9')||(*pp=='.'&&pp[1]>='0'&&pp[1]<='9'))) return 0;
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
static int isstrname(const char *nm) { int i=0; while(nm[i]) i++; return i>0&&nm[i-1]=='$'; }

/* ---- Array helpers ------------------------------------------------------- */
static Array *arrget(const char *nm) {
    for(int i=0;i<narrays;i++) if(bstreq(arrays[i].name,nm)) return &arrays[i];
    return 0;
}
static int arridx(Array *a, int i1, int i2, int i3) {
    if(a->dims==1){
        if(i1<0||i1>a->size[0]){berr("SUBSCRIPT");return -1;}
        return a->base+i1;
    }
    if(a->dims==2){
        if(i1<0||i1>a->size[0]||i2<0||i2>a->size[1]){berr("SUBSCRIPT");return -1;}
        return a->base+i1*(a->size[1]+1)+i2;
    }
    if(i1<0||i1>a->size[0]||i2<0||i2>a->size[1]||i3<0||i3>a->size[2]){berr("SUBSCRIPT");return -1;}
    return a->base+i1*(a->size[1]+1)*(a->size[2]+1)+i2*(a->size[2]+1)+i3;
}

/* ---- String functions ---------------------------------------------------- */
static Val call_strfn(const char *nm) {
    sw(); pp++; /* consume '(' */

    if (bstreq(nm,"INKEY$")) {
        pp--; /* no arg - back up past '(' */
        int r=snew(); if(r<0){berr("MEM");return vstr(-1);}
        int c=term_peekkey();
        spool[r][0]=c?(char)c:0; spool[r][1]=0;
        return vstr(r);
    }
    if (bstreq(nm,"LEFT$")) {
        Val s=strexpr(); if(err) return vstr(-1);
        sw(); if(*pp!=','){berr("SYNTAX");return vstr(-1);} pp++;
        Val n=expr(); if(err) return vstr(-1);
        sw(); if(*pp!=')'){berr("SYNTAX");return vstr(-1);} pp++;
        int r=snew(); if(r<0){berr("MEM");return vstr(-1);}
        const char *src=spool[s.si]; int i=0;
        while(src[i]&&i<n.i&&i<SLEN-1) { spool[r][i]=src[i]; i++; }
        spool[r][i]=0;
        if(s.si>=0&&s.ty==TY_STR) sfree(s.si);
        return vstr(r);
    }
    if (bstreq(nm,"RIGHT$")) {
        Val s=strexpr(); if(err) return vstr(-1);
        sw(); if(*pp!=','){berr("SYNTAX");return vstr(-1);} pp++;
        Val n=expr(); if(err) return vstr(-1);
        sw(); if(*pp!=')'){berr("SYNTAX");return vstr(-1);} pp++;
        int r=snew(); if(r<0){berr("MEM");return vstr(-1);}
        const char *src=spool[s.si];
        int l=bstrlen(src);
        int st=l-n.i; if(st<0) st=0;
        int i=0;
        while(src[st]&&i<SLEN-1) { spool[r][i++]=src[st++]; }
        spool[r][i]=0;
        if(s.si>=0&&s.ty==TY_STR) sfree(s.si);
        return vstr(r);
    }
    if (bstreq(nm,"MID$")) {
        Val s=strexpr(); if(err) return vstr(-1);
        sw(); if(*pp!=','){berr("SYNTAX");return vstr(-1);} pp++;
        Val start=expr(); if(err) return vstr(-1);
        int cnt=-1;
        sw(); if(*pp==','){pp++; Val n2=expr(); if(err) return vstr(-1); cnt=n2.i;}
        sw(); if(*pp!=')'){berr("SYNTAX");return vstr(-1);} pp++;
        int r=snew(); if(r<0){berr("MEM");return vstr(-1);}
        const char *src=spool[s.si];
        int st=start.i-1; if(st<0) st=0;
        src+=st;
        int i=0;
        while(*src&&(cnt<0||i<cnt)&&i<SLEN-1) { spool[r][i++]=*src++; }
        spool[r][i]=0;
        if(s.si>=0&&s.ty==TY_STR) sfree(s.si);
        return vstr(r);
    }
    if (bstreq(nm,"STR$")) {
        Val n=expr(); if(err) return vstr(-1);
        sw(); if(*pp!=')'){berr("SYNTAX");return vstr(-1);} pp++;
        int r=snew(); if(r<0){berr("MEM");return vstr(-1);}
        char *d=spool[r]; int i=0;
        if(n.ty==TY_FLOAT){
            double f=n.f; if(f<0){d[i++]='-';f=-f;}
            int32_t ip=(int32_t)f; double fp=f-(double)ip;
            char tmp[12]; int ti=0;
            if(!ip) tmp[ti++]='0';
            else { int32_t t=ip; while(t){tmp[ti++]='0'+(int)(t%10);t/=10;} }
            while(ti>0&&i<SLEN-2) d[i++]=tmp[--ti];
            char fb[10]; int fi=0;
            for(int k=0;k<6;k++){fp*=10.0;int dg=(int)fp;fb[fi++]='0'+dg;fp-=(double)dg;}
            while(fi>1&&fb[fi-1]=='0') fi--;
            if(fi>0&&i<SLEN-2){d[i++]='.';for(int k=0;k<fi&&i<SLEN-2;k++) d[i++]=fb[k];}
        } else {
            int32_t iv=n.i; int neg=0;
            if(iv<0){neg=1;iv=-iv;}
            char tmp[12]; int ti=0;
            if(!iv) tmp[ti++]='0';
            else { int32_t t=iv; while(t){tmp[ti++]='0'+(int)(t%10);t/=10;} }
            if(neg&&i<SLEN-2) d[i++]='-';
            while(ti>0&&i<SLEN-2) d[i++]=tmp[--ti];
        }
        d[i]=0;
        return vstr(r);
    }
    if (bstreq(nm,"CHR$")) {
        Val n=expr(); if(err) return vstr(-1);
        sw(); if(*pp!=')'){berr("SYNTAX");return vstr(-1);} pp++;
        int r=snew(); if(r<0){berr("MEM");return vstr(-1);}
        spool[r][0]=(char)n.i; spool[r][1]=0;
        return vstr(r);
    }
    if (bstreq(nm,"VAL")) {
        Val s=strexpr(); if(err) return vint(0);
        sw(); if(*pp!=')'){berr("SYNTAX");return vint(0);} pp++;
        const char *p=spool[s.si]; int neg=0; double n=0; int fdiv=1,frac=0;
        if(*p=='-'){neg=1;p++;}
        while(*p>='0'&&*p<='9') n=n*10+(*p++-'0');
        if(*p=='.'){p++;while(*p>='0'&&*p<='9'){n=n*10+(*p++-'0');fdiv*=10;frac=1;}}
        if(frac) { n/=fdiv; } if(neg) { n=-n; }
        if(s.ty==TY_STR) sfree(s.si);
        return (n!=(double)(int32_t)n)?vflt(n):vint((int32_t)n);
    }
    if (bstreq(nm,"ASC")) {
        Val s=strexpr(); if(err) return vint(0);
        sw(); if(*pp!=')'){berr("SYNTAX");return vint(0);} pp++;
        int c=(unsigned char)spool[s.si][0];
        if(s.ty==TY_STR) sfree(s.si);
        return vint(c);
    }
    berr("UNKNOWN FUNCTION"); return vstr(-1);
}

static Val numfn(const char *nm) {
    sw(); pp++; /* consume '(' */
    Val a=expr(); if(err) return vint(0);
    Val second=vint(0); int hassecond=0;
    sw(); if(*pp==','){pp++; second=expr(); if(err) return vint(0); hassecond=1;}
    sw(); if(*pp!=')'){berr("SYNTAX");return vint(0);} pp++;
    if(bstreq(nm,"MOD")) { if(!hassecond){berr("SYNTAX");return vint(0);} return vmod(a,second); }
    if(bstreq(nm,"SIN")) return vflt(x87_sin(NUMVAL(a)));
    if(bstreq(nm,"COS")) return vflt(x87_cos(NUMVAL(a)));
    if(bstreq(nm,"TAN")) return vflt(x87_tan(NUMVAL(a)));
    if(bstreq(nm,"ATN")) return vflt(x87_atn(NUMVAL(a)));
    if(bstreq(nm,"EXP")) return vflt(x87_exp(NUMVAL(a)));
    if(bstreq(nm,"LOG")) { if(a.f<=0){berr("MATH");return vint(0);} return vflt(x87_log(NUMVAL(a))); }
    if(bstreq(nm,"SQR")) { if(a.f<0){berr("MATH");return vint(0);} return vflt(x87_sqrt(NUMVAL(a))); }
    if(bstreq(nm,"ABS")) return a.ty==TY_FLOAT?vflt(x87_abs(a.f)):vint(a.i<0?-a.i:a.i);
    if(bstreq(nm,"INT")) return vflt(x87_int(NUMVAL(a)));
    if(bstreq(nm,"FIX")) return vflt(x87_fix(NUMVAL(a)));
    if(bstreq(nm,"SGN")) return vint(x87_sgn(NUMVAL(a)));
    if(bstreq(nm,"RND")) return vflt(rnd());
    if(bstreq(nm,"CINT")) return vint((int32_t)(NUMVAL(a)+0.5));
    if(bstreq(nm,"CDBL")) return vflt(NUMVAL(a));
    if(bstreq(nm,"PEEK")) { uint8_t *p=(uint8_t *)(uint32_t)a.i; return vint((int32_t)*p); }
    berr("UNKNOWN FUNCTION"); return vint(0);
}

/* ---- strexpr ------------------------------------------------------------- */
static Val strexpr(void) {
    sw(); Val a;
    if (*pp=='"') {
        pp++;
        int r=snew(); if(r<0){berr("MEM");return vstr(-1);}
        int i=0;
        while(*pp&&*pp!='"'&&i<SLEN-1) { spool[r][i++]=*pp++; }
        spool[r][i]=0;
        if(*pp=='"') pp++;
        a=vstr(r);
    } else {
        const char *save=pp;
        char nm[NAMELEN];
        if (readname(nm,NAMELEN)&&isstrname(nm)) {
            sw();
            if (*pp=='(') {
                /* could be a string function or array */
                Array *ar=arrget(nm);
                if (ar) {
                    pp++;
                    Val i1=expr(); if(err) return vstr(-1);
                    Val i2=vint(0),i3=vint(0);
                    if(*pp==','){pp++;i2=expr();if(err)return vstr(-1);}
                    if(*pp==','){pp++;i3=expr();if(err)return vstr(-1);}
                    sw(); if(*pp!=')'){berr("SYNTAX");return vstr(-1);} pp++;
                    int idx=arridx(ar,i1.i,i2.i,i3.i); if(idx<0) return vstr(-1);
                    int r=snew(); if(r<0){berr("MEM");return vstr(-1);}
                    if(aelems[idx].ty==TY_STR&&aelems[idx].si>=0)
                        bstrcpy(spool[r],spool[aelems[idx].si],SLEN);
                    else spool[r][0]=0;
                    a=vstr(r);
                } else {
                    a=call_strfn(nm);
                }
            } else {
                Var *vp=varget(nm);
                int r=snew(); if(r<0){berr("MEM");return vstr(-1);}
                if(vp&&vp->val.ty==TY_STR)
                    bstrcpy(spool[r],spool[vp->val.si],SLEN);
                else spool[r][0]=0;
                a=vstr(r);
            }
        } else {
            pp=save;
            berr("TYPE MISMATCH"); return vstr(-1);
        }
    }
    /* string concatenation */
    for(;;) {
        sw(); if(*pp!='+') break; pp++;
        Val b=strexpr(); if(err) break;
        int r=snew();
        if(r<0){berr("MEM");sfree(a.si);sfree(b.si);return vstr(-1);}
        int la=bstrlen(spool[a.si]), lb=bstrlen(spool[b.si]);
        int n=la+lb<SLEN-1?la+lb:SLEN-1;
        for(int j=0;j<la&&j<SLEN-1;j++) spool[r][j]=spool[a.si][j];
        for(int j=0;j<lb&&la+j<SLEN-1;j++) spool[r][la+j]=spool[b.si][j];
        spool[r][n]=0;
        sfree(a.si); sfree(b.si);
        a=vstr(r);
    }
    return a;
}

/* ---- Expression parser --------------------------------------------------- */
static Val fact(void) {
    sw(); if(err) return vint(0);
    if(*pp=='(') {
        pp++;
        Val v=expr();
        sw(); if(*pp!=')'){berr("SYNTAX");return vint(0);} pp++;
        return v;
    }
    if(*pp=='-'){pp++; Val v=fact(); v.f=-v.f; v.i=-v.i; return v;}
    if(*pp=='+'){pp++; return fact();}
    double n;
    if(pnum(&n)) return n!=(double)(int32_t)n?vflt(n):vint((int32_t)n);
    char nm[NAMELEN];
    const char *save=pp;
    if (readname(nm,NAMELEN)) {
        sw();
        /* INKEY$ - string result, no parens required */
        if (bstreq(nm,"INKEY$")) {
            if(*pp=='(') pp++;
            int r=snew(); if(r<0){berr("MEM");return vint(0);}
            int c=term_peekkey();
            spool[r][0]=c?(char)c:0; spool[r][1]=0;
            if(*pp==')') pp++;
            return vstr(r);
        }
        /* string vars/functions return to strexpr */
        if (isstrname(nm)) { pp=save; return strexpr(); }
        /* LEN(s) - numeric function taking a string arg */
        if (bstreq(nm,"LEN")&&*pp=='(') {
            pp++;
            Val s=strexpr(); if(err) return vint(0);
            sw(); if(*pp!=')'){berr("SYNTAX");return vint(0);} pp++;
            int l=bstrlen(spool[s.si]);
            if(s.ty==TY_STR) sfree(s.si);
            return vint(l);
        }
        /* PEEK(addr) */
        if (bstreq(nm,"PEEK")&&*pp=='(') {
            pp++;
            Val a=expr(); if(err) return vint(0);
            sw(); if(*pp!=')'){berr("SYNTAX");return vint(0);} pp++;
            uint8_t *p=(uint8_t *)(uint32_t)a.i;
            return vint((int32_t)*p);
        }
        if (*pp=='(') {
            /* array access or numeric function */
            Array *ar=arrget(nm);
            if (ar) {
                pp++;
                Val i1=expr(); if(err) return vint(0);
                Val i2=vint(0),i3=vint(0);
                if(*pp==','){pp++;i2=expr();if(err)return vint(0);}
                if(*pp==','){pp++;i3=expr();if(err)return vint(0);}
                sw(); if(*pp!=')'){berr("SYNTAX");return vint(0);} pp++;
                int idx=arridx(ar,i1.i,i2.i,i3.i); if(idx<0) return vint(0);
                Val el=aelems[idx];
                if(el.ty==TY_STR) {
                    int r=snew(); if(r<0){berr("MEM");return vint(0);}
                    bstrcpy(spool[r],el.si>=0?spool[el.si]:"",SLEN);
                    return vstr(r);
                }
                return el;
            }
            return numfn(nm);
        }
        if(bstreq(nm,"RND")) return vflt(rnd());
        Var *vp=varget(nm);
        return vp?vp->val:vint(0);
    }
    pp=save; berr("SYNTAX"); return vint(0);
}

static Val termexpr(void) {
    Val a=fact();
    while(!err) {
        sw();
        if(*pp=='*'){pp++;a=vmul(a,fact());}
        else if(*pp=='/'){pp++;Val b=fact();if(b.ty==TY_INT&&b.i==0){berr("DIV0");return vint(0);}a=vdiv(a,b);}
        else if(*pp=='\\'){pp++;Val b=fact();if(!b.i&&b.ty==TY_INT){berr("DIV0");return vint(0);}a=vint(a.i/b.i);}
        else break;
    }
    return a;
}
static Val expr(void) {
    Val a=termexpr();
    while(!err) {
        sw();
        if(*pp=='+'){pp++;a=vadd(a,termexpr());}
        else if(*pp=='-'){pp++;a=vsub(a,termexpr());}
        else break;
    }
    return a;
}
static int relop(void) {
    sw();
    if(*pp=='<'){pp++;if(*pp=='='){pp++;return 2;}if(*pp=='>'){pp++;return 4;}return 3;}
    if(*pp=='>'){pp++;if(*pp=='='){pp++;return 6;}if(*pp=='<'){pp++;return 5;}return 5;}
    if(*pp=='='){pp++;return 1;}
    return 0;
}
static int applyrel(int op,Val a,Val b) {
    int c=vcmp(a,b);
    switch(op){case 1:return c==0;case 2:return c!=0;case 3:return c<0;
               case 4:return c>0;case 5:return c<=0;case 6:return c>=0;}
    return 0;
}

/* ---- Program helpers ----------------------------------------------------- */
static int findge(int n) {
    for(int i=0;i<Pn;i++) if((int)P[i].num>=n) return i;
    return Pn;
}
static void storeline(int n, const char *t) {
    int lo=0,hi=Pn,idx=0,found=0;
    while(lo<hi) {
        int m=(lo+hi)/2;
        if((int)P[m].num==n){idx=m;found=1;break;}
        if((int)P[m].num<n){lo=m+1;idx=lo;}else{hi=m;idx=lo;}
    }
    if(!found) idx=lo;
    if(found) {
        if(!t||!t[0]){for(int i=idx;i<Pn-1;i++) P[i]=P[i+1];Pn--;return;}
        bstrcpy(P[idx].txt,t,LINELEN); return;
    }
    if(!t||!t[0]) return;
    if(Pn>=MAXLINES){berr("MEM");return;}
    for(int i=Pn;i>idx;i--) P[i]=P[i-1];
    Pn++;
    P[idx].num=(uint16_t)n;
    bstrcpy(P[idx].txt,t,LINELEN);
}

/* ---- 8.3 name helper ----------------------------------------------------- */
static void toname11(const char *s, char nm11[12]) {
    char base[8],ext[3];
    for(int i=0;i<8;i++) base[i]=' ';
    for(int i=0;i<3;i++) ext[i]=' ';
    int ni=0,ei=0;
    while(*s&&*s!='.'){if(ni<8){char c=*s;if(c>='a'&&c<='z')c-=32;base[ni++]=c;}s++;}
    if(*s=='.') s++;
    while(*s){if(ei<3){char c=*s;if(c>='a'&&c<='z')c-=32;ext[ei++]=c;}s++;}
    for(int i=0;i<8;i++) nm11[i]=base[i];
    for(int i=0;i<3;i++) nm11[8+i]=ext[i];
    nm11[11]=0;
}

/* ---- DIR callback -------------------------------------------------------- */
static void dircb(const char *name11, uint32_t size) {
    char out[13]; int oi=0;
    int nlen=0;
    for(int i=7;i>=0;i--){if(name11[i]!=' '){nlen=i+1;break;}}
    for(int i=0;i<nlen;i++) out[oi++]=name11[i];
    int elen=0;
    for(int i=10;i>=8;i--){if(name11[i]!=' '){elen=i-8+1;break;}}
    if(elen>0){out[oi++]='.';for(int i=0;i<elen;i++) out[oi++]=name11[8+i];}
    out[oi]=0;
    term_puts("  ");
    term_puts(out);
    int pad=13-oi; while(pad-->0) term_putchar(' ');
    term_puti((int32_t)size); term_puts(" bytes\n");
}

/* ---- int-to-string helper used in PRINT --------------------------------- */
static void itos(int32_t iv, char *out, int max) {
    int neg=0,i=0;
    if(iv<0){neg=1;iv=-iv;}
    char tmp[12]; int ti=0;
    if(!iv) tmp[ti++]='0';
    else { int32_t t=iv; while(t){tmp[ti++]='0'+(int)(t%10);t/=10;} }
    if(neg&&i<max-2) out[i++]='-';
    while(ti>0&&i<max-1) out[i++]=tmp[--ti];
    out[i]=0;
}
static void ftos(double f, char *out, int max) {
    int neg=0,i=0;
    if(f<0){neg=1;f=-f;}
    int32_t ip=(int32_t)f; double fp=f-(double)ip;
    char tmp[12]; int ti=0;
    if(!ip) tmp[ti++]='0';
    else { int32_t t=ip; while(t){tmp[ti++]='0'+(int)(t%10);t/=10;} }
    if(neg&&i<max-2) out[i++]='-';
    while(ti>0&&i<max-1) out[i++]=tmp[--ti];
    char fb[10]; int fi=0;
    for(int k=0;k<6;k++){fp*=10.0;int dg=(int)fp;fb[fi++]='0'+dg;fp-=(double)dg;}
    while(fi>1&&fb[fi-1]=='0') fi--;
    if(fi>0&&i<max-2){out[i++]='.';for(int k=0;k<fi&&i<max-1;k++) out[i++]=fb[k];}
    out[i]=0;
}

/* ---- Statement dispatcher ------------------------------------------------ */
static void stmt(const char *line);
static void stmt(const char *line) {
    pp=line; sw();
    if(!*pp) return;

    if(kw("REM")||kw("'")) return;

    if(kw("KPAN")) { kpanic("KPAN"); }

    /* CLEAR */
    if(kw("CLEAR")) { draw_banner(); return; }

    /* HALT */
    if(kw("HALT")) {
        term_set_color(VGA_YELLOW,VGA_BLACK); term_puts(" halted.\n");
        __asm__ __volatile__("cli");
        for(;;) __asm__ __volatile__("hlt");
    }

    /* NEW */
    if(kw("NEW")) {
        Pn=0;nvars=0;csp=0;fsp=0;wsp=0;data_line=0;data_col=0;
        narrays=0;aused=0;
        for(int i=0;i<NSTRS;i++) sused[i]=0;
        for(int i=0;i<MAXFILES;i++) files[i].open=0;
        return;
    }

    /* END */
    if(kw("END")){running=0;return;}

    /* LIST */
    if(kw("LIST")) {
        for(int i=0;i<Pn;i++) {
            term_set_color(VGA_DARK_GREY,VGA_BLACK); term_puti(P[i].num); term_putchar(' ');
            term_set_color(VGA_LIGHT_GREY,VGA_BLACK); term_puts(P[i].txt); term_putchar('\n');
        }
        return;
    }

    /* RUN */
    if(kw("RUN")) {
        pc=0;running=1;csp=0;fsp=0;wsp=0;data_line=0;data_col=0;
        narrays=0;aused=0;
        for(int i=0;i<NSTRS;i++) sused[i]=0;
        nvars=0;
        for(int i=0;i<MAXFILES;i++) files[i].open=0;
        return;
    }

    /* DIM name(d1[,d2[,d3]]) [, ...] */
    if(kw("DIM")) {
        for(;;) {
            sw(); char nm[NAMELEN];
            if(!readname(nm,NAMELEN)){berr("SYNTAX");return;}
            sw(); if(*pp!='('){berr("SYNTAX");return;} pp++;
            Val d1=expr(); if(err) return;
            Val d2=vint(-1),d3=vint(-1);
            if(*pp==','){pp++;d2=expr();if(err)return;}
            if(*pp==','){pp++;d3=expr();if(err)return;}
            sw(); if(*pp!=')'){berr("SYNTAX");return;} pp++;
            if(narrays>=MAXARRAYS){berr("MEM");return;}
            Array *a=&arrays[narrays];
            bstrcpy(a->name,nm,NAMELEN); a->isstr=isstrname(nm);
            a->dims=d3.i>=0?3:d2.i>=0?2:1;
            a->size[0]=d1.i; a->size[1]=d2.i; a->size[2]=d3.i;
            int total;
            if(a->dims==1) total=d1.i+1;
            else if(a->dims==2) total=(d1.i+1)*(d2.i+1);
            else total=(d1.i+1)*(d2.i+1)*(d3.i+1);
            if(aused+total>MAXELEMS){berr("MEM");return;}
            a->base=aused; a->total=total; aused+=total;
            for(int i=a->base;i<a->base+total;i++) aelems[i]=vint(0);
            narrays++;
            sw(); if(*pp!=',') break; pp++;
        }
        return;
    }

    /* POKE addr, val */
    if(kw("POKE")){
        Val addr=expr(); if(err) return;
        sw(); if(*pp!=','){berr("SYNTAX");return;} pp++;
        Val val=expr(); if(err) return;
        *(uint8_t *)(uint32_t)addr.i=(uint8_t)val.i;
        return;
    }

    /* LET / bare assignment / array element */
    {
        const char *save=pp;
        if(kw("LET")||1){
            char nm[NAMELEN];
            if(readname(nm,NAMELEN)){
                sw();
                if(*pp=='('){
                    Array *ar=arrget(nm);
                    if(ar){
                        pp++;
                        Val i1=expr(); if(err) return;
                        Val i2=vint(0),i3=vint(0);
                        if(*pp==','){pp++;i2=expr();if(err)return;}
                        if(*pp==','){pp++;i3=expr();if(err)return;}
                        sw(); if(*pp!=')'){berr("SYNTAX");return;} pp++;
                        sw(); if(*pp!='='){berr("SYNTAX");return;} pp++;
                        int idx=arridx(ar,i1.i,i2.i,i3.i); if(idx<0) return;
                        if(isstrname(nm)){Val v=strexpr();if(!err){if(aelems[idx].ty==TY_STR&&aelems[idx].si>=0)sfree(aelems[idx].si);aelems[idx]=v;}}
                        else {Val v=expr();if(!err) aelems[idx]=v;}
                        return;
                    }
                    pp=save; goto not_assign;
                }
                if(*pp=='='){
                    pp++;
                    if(isstrname(nm)){Val v=strexpr();if(!err) varset(nm,v);}
                    else {Val v=expr(); if(!err) varset(nm,v);}
                    return;
                }
                pp=save;
            }
        }
    }
not_assign:;

    /* PRINT / ? */
    if(kw("PRINT")||kw("?")) {
        int nl=1;
        if(!*pp||*pp=='\n'){term_putchar('\n');return;}
        for(;;){
            sw(); if(!*pp||*pp=='\n') break;
            /* detect string context */
            int iss=0;
            const char *s2=pp;
            if(*pp=='"') iss=1;
            else {
                char nm2[NAMELEN];
                if(readname(nm2,NAMELEN)){
                    if(isstrname(nm2)) iss=1;
                    else if(bstreq(nm2,"INKEY$")) iss=1;
                }
                pp=s2;
            }
            if(iss){Val v=strexpr();if(!err){term_puts(spool[v.si]);sfree(v.si);}}
            else {Val v=expr(); if(!err) vprint(v);}
            if(err) return;
            sw(); nl=1;
            if(*pp==';'){pp++;nl=0;}
            else if(*pp==','){pp++;term_putchar('\t');nl=0;}
            else break;
        }
        if(nl&&!err) term_putchar('\n');
        return;
    }

    /* INPUT */
    if(kw("INPUT")){
        sw();
        if(*pp=='"'){
            pp++; while(*pp&&*pp!='"'){term_putchar(*pp);pp++;} if(*pp=='"') pp++;
            sw(); if(*pp==',') pp++;
        } else { term_putchar('?'); term_putchar(' '); }
        for(;;){
            sw(); char nm3[NAMELEN];
            if(!readname(nm3,NAMELEN)){berr("SYNTAX");return;}
            char lb[IBUF]; term_getline(lb,IBUF);
            if(isstrname(nm3)){
                int r=snew();if(r<0){berr("MEM");return;}
                bstrcpy(spool[r],lb,SLEN); varset(nm3,vstr(r));
            } else {
                upr(lb);
                const char *sv=pp; pp=lb; double n=0; pnum(&n); pp=sv;
                varset(nm3,(n!=(double)(int32_t)n)?vflt(n):vint((int32_t)n));
            }
            sw(); if(*pp!=',') break; pp++;
            term_puts("? ");
        }
        return;
    }

    if(kw("DATA")) return;

    /* READ */
    if(kw("READ")){
        for(;;){
            sw(); char nm4[NAMELEN];
            if(!readname(nm4,NAMELEN)){berr("SYNTAX");return;}
            int found=0;
            for(int i=data_line;i<Pn&&!found;i++) {
                const char *dp=P[i].txt;
                const char *save2=pp; pp=dp;
                if(kw("DATA")) {
                    int col=0;
                    while(pp&&col<data_col){while(*pp&&*pp!=',')pp++;if(*pp==',')pp++;col++;}
                    if(*pp) {
                        if(isstrname(nm4)){
                            int r=snew();if(r<0){berr("MEM");pp=save2;return;}
                            int k=0;
                            if(*pp=='"'){pp++;while(*pp&&*pp!='"'&&k<SLEN-1)spool[r][k++]=*pp++;if(*pp=='"')pp++;}
                            else{while(*pp&&*pp!=','&&k<SLEN-1)spool[r][k++]=*pp++;}
                            spool[r][k]=0;
                            varset(nm4,vstr(r));
                        } else {
                            double n=0; pnum(&n);
                            varset(nm4,n!=(double)(int32_t)n?vflt(n):vint((int32_t)n));
                        }
                        data_line=i; data_col++; found=1;
                    }
                }
                pp=save2;
            }
            if(!found){berr("OUT OF DATA");return;}
            sw(); if(*pp!=',') break; pp++;
        }
        return;
    }

    if(kw("RESTORE")){data_line=0;data_col=0;return;}

    /* GOTO */
    if(kw("GOTO")) {
        sw(); Val n=expr(); if(err) return;
        int i=findge((int)n.i);
        if(i>=Pn||(int)P[i].num!=(int)n.i){berr("LINE NOT FOUND");return;}
        pc=i; return;
    }

    /* GOSUB */
    if(kw("GOSUB")){
        sw(); Val n=expr(); if(err) return;
        int i=findge((int)n.i);
        if(i>=Pn||(int)P[i].num!=(int)n.i){berr("LINE NOT FOUND");return;}
        if(csp>=CALL_DEPTH){berr("GOSUB OVERFLOW");return;}
        cstk[csp++]=pc; pc=i; return;
    }

    if(kw("RETURN")){
        if(csp<=0){berr("RETURN WITHOUT GOSUB");return;}
        pc=cstk[--csp]; return;
    }

    /* FOR */
    if(kw("FOR")){
        sw(); char nm5[NAMELEN];
        if(!readname(nm5,NAMELEN)){berr("SYNTAX");return;}
        sw(); if(*pp!='='){berr("SYNTAX");return;} pp++;
        Val from=expr(); if(err) return;
        if(!kw("TO")){berr("SYNTAX");return;}
        Val to=expr(); if(err) return;
        Val step=vint(1);
        if(kw("STEP")){step=expr();if(err)return;}
        if(!step.i&&!step.f){berr("STEP 0");return;}
        varset(nm5,from);
        int go=step.f>0?vcmp(from,to)<=0:vcmp(from,to)>=0;
        if(!go){
            int depth=0;
            while(pc<Pn){
                const char *t=P[pc].txt; const char *sv=pp; pp=t;
                if(kw("FOR")) depth++; else if(kw("NEXT")){if(!depth)break;depth--;}
                pp=sv; pc++;
            }
            return;
        }
        if(fsp>=FOR_DEPTH){berr("FOR OVERFLOW");return;}
        ForFrame *f=&fstk[fsp++];
        bstrcpy(f->var,nm5,NAMELEN); f->lim=to; f->step=step; f->ret=pc;
        return;
    }

    if(kw("NEXT")){
        if(fsp<=0){berr("NEXT WITHOUT FOR");return;}
        ForFrame *f=&fstk[fsp-1];
        sw(); char nm6[NAMELEN];
        if(readname(nm6,NAMELEN)&&!bstreq(nm6,f->var)){berr("NEXT MISMATCH");return;}
        Var *vp=varget(f->var); if(!vp){berr("NEXT VAR LOST");return;}
        vp->val=vadd(vp->val,f->step);
        int go=f->step.f>0?vcmp(vp->val,f->lim)<=0:vcmp(vp->val,f->lim)>=0;
        if(go) pc=f->ret; else fsp--;
        return;
    }

    /* WHILE / WEND */
    if(kw("WHILE")){
        Val cond=expr(); if(err) return;
        if(cond.i||cond.f!=0.0){
            if(wsp>=WHILE_DEPTH){berr("WHILE OVERFLOW");return;}
            wstk[wsp++].ret=pc-1;
        } else {
            int depth=0;
            while(pc<Pn){
                const char *t=P[pc].txt; const char *sv=pp; pp=t;
                if(kw("WHILE")) depth++; else if(kw("WEND")){if(!depth)break;depth--;}
                pp=sv; pc++;
            }
        }
        return;
    }

    if(kw("WEND")){
        if(wsp<=0){berr("WEND WITHOUT WHILE");return;}
        pc=wstk[wsp-1].ret;
        const char *sv=pp; pp=P[pc].txt;
        if(!kw("WHILE")){berr("WEND MISMATCH");pp=sv;return;}
        Val cond=expr(); pp=sv;
        if(!cond.i&&cond.f==0.0) wsp--;
        return;
    }

    /* IF */
    if(kw("IF")) {
        Val cond;
        const char *save3=pp;
        /* try string comparison first */
        {
            char nm7[NAMELEN];
            if(readname(nm7,NAMELEN)&&isstrname(nm7)) {
                Val a=strexpr(); if(err) return;
                int op=relop();
                if(!op){berr("SYNTAX");sfree(a.si);return;}
                Val b=strexpr();if(err){sfree(a.si);return;}
                cond=vint(applyrel(op,a,b));
                sfree(a.si); sfree(b.si);
            } else if(*save3=='"') {
                pp=save3;
                Val a=strexpr(); if(err) return;
                int op=relop();
                if(!op){berr("SYNTAX");sfree(a.si);return;}
                Val b=strexpr();if(err){sfree(a.si);return;}
                cond=vint(applyrel(op,a,b));
                sfree(a.si); sfree(b.si);
            } else {
                pp=save3; cond=expr(); if(err) return;
            }
        }
        if(!kw("THEN")){berr("SYNTAX");return;}
        sw();
        if(*pp>='0'&&*pp<='9'){
            if(!cond.i&&!cond.f) return;
            Val n=expr(); if(err) return;
            int i=findge((int)n.i);
            if(i>=Pn||(int)P[i].num!=(int)n.i){berr("LINE NOT FOUND");return;}
            pc=i; return;
        }
        if(*pp&&*pp!='\n'){
            if(cond.i||cond.f!=0.0) stmt(pp);
            return;
        }
        /* multi-line IF */
        if(!cond.i&&cond.f==0.0){
            int depth=0;
            while(pc<Pn){
                const char *t=P[pc].txt; const char *sv=pp; pp=t;
                if(kw("IF")) depth++;
                else { if(!depth&&kw("ELSE")){pp=sv;break;} else if(!depth&&kw("ENDIF")){pp=sv;break;} else if(kw("ENDIF")) depth--; }
                pp=sv; pc++;
            }
        }
        return;
    }

    if(kw("ELSE")||kw("ENDIF")) {
        /* skip to ENDIF */
        int depth=0;
        while(pc<Pn){
            const char *t=P[pc].txt; const char *sv=pp; pp=t;
            if(kw("IF")) depth++;
            else if(kw("ENDIF")){if(!depth){pp=sv;return;}depth--;}
            pp=sv; pc++;
        }
        return;
    }

    /* ON cond GOTO/GOSUB n1,n2,... */
    if(kw("ON")) {
        Val cond=expr(); if(err) return;
        int is_gosub=0;
        if(kw("GOSUB")) is_gosub=1;
        else if(!kw("GOTO")){berr("SYNTAX");return;}
        int idx=(int)cond.i; int ci=0;
        for(;;){
            sw(); Val n=expr(); if(err) return;
            ci++;
            if(ci==idx){
                int i=findge((int)n.i);
                if(i>=Pn||(int)P[i].num!=(int)n.i){berr("LINE NOT FOUND");return;}
                if(is_gosub){if(csp>=CALL_DEPTH){berr("GOSUB OVERFLOW");return;}cstk[csp++]=pc;}
                pc=i; return;
            }
            sw(); if(*pp!=',') break; pp++;
        }
        return;
    }

    /* ---- File IO -------------------------------------------------------- */

    /* OPEN name FOR INPUT|OUTPUT|APPEND AS #n */
    if(kw("OPEN")) {
        sw(); Val fn=strexpr(); if(err) return;
        if(!kw("FOR")){berr("SYNTAX");sfree(fn.si);return;}
        int forw=0;
        if(kw("OUTPUT")||kw("APPEND")) forw=1;
        else if(kw("INPUT")) forw=0;
        else{berr("SYNTAX");sfree(fn.si);return;}
        if(!kw("AS")){berr("SYNTAX");sfree(fn.si);return;}
        sw(); if(*pp=='#') pp++;
        Val fno=expr();if(err){sfree(fn.si);return;}
        int fi=(int)fno.i-1;
        if(fi<0||fi>=MAXFILES){berr("BAD FILE NUMBER");sfree(fn.si);return;}
        if(files[fi].open){berr("FILE ALREADY OPEN");sfree(fn.si);return;}
        if(!fat_ready()){berr("DISK NOT READY");sfree(fn.si);return;}
        FileSlot *fs=&files[fi];
        toname11(spool[fn.si],fs->nm11); sfree(fn.si);
        fs->forwrite=forw; fs->dirty=0; fs->pos=0; fs->len=0;
        if(!forw){
            int n=fat_load(fs->nm11,fs->buf,FBUFSZ-1);
            if(n<0){berr("FILE NOT FOUND");return;}
            fs->buf[n]=0; fs->len=n;
        }
        fs->open=1; return;
    }

    /* CLOSE [#n] */
    if(kw("CLOSE")){
        sw(); int target=-1;
        if(*pp=='#'){pp++;Val n=expr();if(err)return;target=(int)n.i-1;}
        else if(*pp>='0'&&*pp<='9'){Val n=expr();if(err)return;target=(int)n.i-1;}
        for(int i=0;i<MAXFILES;i++){
            if(!files[i].open) continue;
            if(target>=0&&i!=target) continue;
            if(files[i].forwrite&&files[i].dirty&&fat_ready())
                fat_save(files[i].nm11,files[i].buf,files[i].len);
            files[i].open=0;
        }
        return;
    }

    /* PRINT #n, ... */
    if(kw("PRINT")||kw("PRINT#")) {
        if(*pp=='#') pp++;
        Val fno=expr(); if(err) return;
        sw(); if(*pp==',') pp++;
        int fi=(int)fno.i-1;
        if(fi<0||fi>=MAXFILES||!files[fi].open||!files[fi].forwrite){berr("FILE NOT OPEN");return;}
        FileSlot *fs=&files[fi];
        int nl=1;
        for(;;){
            sw(); if(!*pp||*pp=='\n') break;
            int iss=0; const char *s2=pp;
            if(*pp=='"') iss=1;
            else {
                char nm8[NAMELEN];
                if(readname(nm8,NAMELEN)&&isstrname(nm8)) iss=1;
                pp=s2;
            }
            char tmp[SLEN]; tmp[0]=0;
            if(iss){Val v=strexpr();if(!err){bstrcpy(tmp,spool[v.si],SLEN);sfree(v.si);}}
            else{Val v=expr();if(!err){if(v.ty==TY_STR){bstrcpy(tmp,spool[v.si],SLEN);}else if(v.ty==TY_INT){itos(v.i,tmp,(int)sizeof(tmp));}else{ftos(v.f,tmp,(int)sizeof(tmp));}}}
            int tl=bstrlen(tmp);
            if(fs->len+tl<FBUFSZ-2){for(int k=0;k<tl;k++) fs->buf[fs->len++]=tmp[k];fs->dirty=1;}
            sw(); nl=1;
            if(*pp==';'){pp++;nl=0;}
            else if(*pp==','){pp++;if(fs->len<FBUFSZ-2){fs->buf[fs->len++]='\t';fs->dirty=1;}nl=0;}
            else break;
        }
        if(nl&&fs->len<FBUFSZ-2){fs->buf[fs->len++]='\n';fs->dirty=1;}
        return;
    }

    /* INPUT #n, var [, var ...] */
    if(kw("INPUT#")||kw("INPUT")) {
        if(*pp=='#') pp++;
        Val fno=expr(); if(err) return;
        sw(); if(*pp==',') pp++;
        int fi=(int)fno.i-1;
        if(fi<0||fi>=MAXFILES||!files[fi].open||files[fi].forwrite){berr("FILE NOT OPEN");return;}
        FileSlot *fs=&files[fi];
        for(;;){
            sw(); char nm9[NAMELEN];
            if(!readname(nm9,NAMELEN)){berr("SYNTAX");return;}
            while(fs->pos<fs->len&&(fs->buf[fs->pos]=='\r'||fs->buf[fs->pos]=='\n')) fs->pos++;
            if(isstrname(nm9)){
                int r=snew();if(r<0){berr("MEM");return;}
                int k=0;
                if(fs->pos<fs->len&&fs->buf[fs->pos]=='"'){
                    fs->pos++;
                    while(fs->pos<fs->len&&fs->buf[fs->pos]!='"'&&k<SLEN-1) spool[r][k++]=fs->buf[fs->pos++];
                    if(fs->pos<fs->len&&fs->buf[fs->pos]=='"') fs->pos++;
                } else {
                    while(fs->pos<fs->len&&fs->buf[fs->pos]!=','&&fs->buf[fs->pos]!='\n'&&k<SLEN-1)
                        spool[r][k++]=fs->buf[fs->pos++];
                }
                spool[r][k]=0;
                varset(nm9,vstr(r));
            } else {
                char tmp2[64]; int k=0;
                while(fs->pos<fs->len&&fs->buf[fs->pos]!=','&&fs->buf[fs->pos]!='\n'&&k<63)
                    tmp2[k++]=fs->buf[fs->pos++];
                tmp2[k]=0;
                const char *sv=pp; pp=tmp2; double n2=0; pnum(&n2); pp=sv;
                varset(nm9,n2!=(double)(int32_t)n2?vflt(n2):vint((int32_t)n2));
            }
            if(fs->pos<fs->len&&fs->buf[fs->pos]==',') fs->pos++;
            sw(); if(*pp!=',') break; pp++;
        }
        return;
    }

    /* DIR */
    if(kw("DIR")) {
        if(!fat_ready()){berr("DISK NOT READY");return;}
        term_set_color(VGA_CYAN,VGA_BLACK);
        term_puts("Name          Size\n");
        term_puts("------------- ------\n");
        term_set_color(VGA_LIGHT_GREY,VGA_BLACK);
        fat_dir(dircb);
        term_putchar('\n');
        return;
    }

    /* LOAD "file.bas" */
    if(kw("LOAD")) {
        sw(); Val fn=strexpr(); if(err) return;
        if(!fat_ready()){sfree(fn.si);berr("DISK NOT READY");return;}
        char nm11[12];
        toname11(spool[fn.si],nm11); sfree(fn.si);
        int n=fat_load(nm11,fbuf,(int)sizeof(fbuf)-1);
        if(n<0){berr("FILE NOT FOUND");return;}
        fbuf[n]=0;
        Pn=0;
        char *p=fbuf;
        while(*p) {
            while(*p=='\r'||*p=='\n') p++;
            if(!*p) break;
            if(*p>='0'&&*p<='9') {
                int ln=0;
                while(*p>='0'&&*p<='9') { ln=ln*10+(*p++-'0'); }
                while(*p==' '||*p=='\t') p++;
                char *start=p;
                while(*p&&*p!='\r'&&*p!='\n') p++;
                char saved=*p; *p=0;
                storeline(ln,start);
                *p=saved;
            } else {
                while(*p&&*p!='\r'&&*p!='\n') p++;
                if(*p) p++;
            }
        }
        term_set_color(VGA_DARK_GREY,VGA_BLACK);
        term_puti(Pn); term_puts(" lines\n");
        term_set_color(VGA_LIGHT_GREY,VGA_BLACK);
        return;
    }

    /* SAVE "file.bas" */
    if(kw("SAVE")) {
        sw(); Val fn=strexpr(); if(err) return;
        if(!fat_ready()){sfree(fn.si);berr("DISK NOT READY");return;}
        char nm11[12];
        toname11(spool[fn.si],nm11); sfree(fn.si);
        int pos=0;
        for(int i=0;i<Pn;i++) {
            uint16_t ln=P[i].num;
            char nb[8]; int ni=0;
            uint16_t tmp2=ln;
            if(!tmp2) { nb[ni++]='0'; }
            else { uint16_t t=tmp2; while(t){nb[ni++]='0'+(int)(t%10);t/=10;} }
            for(int k=ni-1;k>=0;k--) { if(pos<(int)sizeof(fbuf)-3) fbuf[pos++]=nb[k]; }
            if(pos<(int)sizeof(fbuf)-2) fbuf[pos++]=' ';
            const char *src=P[i].txt;
            while(*src&&pos<(int)sizeof(fbuf)-3) fbuf[pos++]=*src++;
            if(pos<(int)sizeof(fbuf)-2) fbuf[pos++]='\n';
        }
        int r=fat_save(nm11,fbuf,pos);
        if(r<0){berr("DISK FULL OR WRITE ERROR");return;}
        term_set_color(VGA_DARK_GREY,VGA_BLACK);
        term_puti(pos); term_puts(" bytes\n");
        term_set_color(VGA_LIGHT_GREY,VGA_BLACK);
        return;
    }

    /* BEEP [freq [, ms]] */
    if (kw("BEEP")) {
        uint32_t freq = 800, ms = 200;
        sw();
        if (*pp && *pp != '\n') {
            Val f = expr(); if (err) return;
            freq = (uint32_t)f.i;
            sw(); if (*pp == ',') {
                pp++;
                Val m = expr(); if (err) return;
                ms = (uint32_t)m.i;
            }
        }
        beep(freq, ms);
        return;
    }

    /* SAY expr -- speech synthesis not implemented yet */
    if (kw("SAY")) {
        sw();
        if (*pp) {
            int say_is_str = (*pp == '"');
            if (!say_is_str) {
                const char *peek = pp;
                while ((*peek>='A'&&*peek<='Z')||(*peek>='a'&&*peek<='z')||
                       (*peek>='0'&&*peek<='9')||*peek=='_') peek++;
                say_is_str = (peek > pp && peek[-1] == '$');
            }
            if (say_is_str) {
                Val v = strexpr(); if (err) return;
                sfree(v.si);
            } else {
                (void)expr(); if (err) return;
            }
        }
        term_set_color(VGA_DARK_GREY, VGA_BLACK);
        term_puts("SAY: WIP\n");
        term_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }
    /* BLOAD "file.bin", addr */
    if (kw("BLOAD")) {
        sw(); Val fn = strexpr(); if (err) return;
        sw(); if (*pp != ',') { berr("SYNTAX"); sfree(fn.si); return; } pp++;
        Val addr = expr(); if (err) { sfree(fn.si); return; }
        char nm11[12];
        toname11(spool[fn.si], nm11); sfree(fn.si);
        if (!fat_ready()) { berr("DISK NOT READY"); return; }
        /* 0x7F000 = 508 KB ceiling, keeps us below VGA/ROM at 0xA0000 */
        int n = fat_load(nm11, (void *)(uint32_t)addr.i, 0x7F000);
        if (n < 0) { berr("FILE NOT FOUND"); return; }
        term_set_color(VGA_DARK_GREY, VGA_BLACK);
        term_puti((int32_t)n); term_puts(" bytes -> 0x");
        uint32_t av = (uint32_t)addr.i;
        for (int _s = 28; _s >= 0; _s -= 4)
            term_putchar("0123456789ABCDEF"[(av >> _s) & 0xF]);
        term_putchar('\n');
        term_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }

    /* SYS addr */
    if (kw("SYS")) {
        Val addr = expr(); if (err) return;
        typedef void (*fn_t)(void);
        ((fn_t)(uintptr_t)(uint32_t)addr.i)();
        return;
    }

    /* HELP */
    if(kw("HELP")) {
        term_set_color(VGA_CYAN,VGA_BLACK);
        term_puts("BTBX BASIC command reference\n");
        term_set_color(VGA_LIGHT_GREY,VGA_BLACK);
        term_puts(
            "  PRINT/? expr[;expr,...]   INPUT [\"prompt\",] var\n"
            "  LET var=expr              REM / '\n"
            "  IF expr THEN ..|GOTO n    ELSE / ENDIF\n"
            "  FOR v=n TO n [STEP n]     NEXT [v]\n"
            "  WHILE expr / WEND         GOTO n / GOSUB n / RETURN\n"
            "  ON expr GOTO/GOSUB n,...  DATA / READ / RESTORE\n"
            "  DIM name(d[,d[,d]])       NEW / RUN / LIST / END\n"
            "  PEEK(addr) / POKE addr,v  INKEY$\n"
            "  SAVE/LOAD/DIR/OPEN/CLOSE/PRINT#/INPUT#\n"
            "  BEEP [freq[,ms]]          SAY expr (WIP)\n"
            "  BLOAD \"F.BIN\",addr        SYS addr\n"
            "  Math: SIN COS TAN ATN EXP LOG SQR ABS INT FIX SGN RND\n"
            "  Str:  LEFT$ RIGHT$ MID$ STR$ CHR$ VAL ASC LEN\n"
            "  HALT / CLEAR / HELP\n"
        );
        return;
    }

    berr("WHAT?");
}

/* ---- REPL ---------------------------------------------------------------- */
void basic_run(void) {
    for(;;) {
        if(running) {
            if(pc>=Pn){running=0;continue;}
            int here=pc; err=0;
            stmt(P[here].txt);
            if(running) { if(pc==here) pc++; }
            continue;
        }
        err=0; term_sync_cursor();
        term_set_color(VGA_LIGHT_GREEN,VGA_BLACK); term_puts("BTBX");
        term_set_color(VGA_DARK_GREY,VGA_BLACK); term_puts("> ");
        term_set_color(VGA_WHITE,VGA_BLACK);
        term_getline(ibuf,IBUF);
        upr(ibuf);
        const char *p=ibuf;
        while(*p==' '||*p=='\t') p++;
        if(!*p) continue;
        if(*p>='0'&&*p<='9') {
            int n=0;
            while(*p>='0'&&*p<='9') n=n*10+(*p++-'0');
            if(n<1||n>65535){berr("BAD LINE NUMBER");continue;}
            while(*p==' '||*p=='\t') p++;
            storeline(n,p);
        } else {
            stmt(p);
        }
    }
}
