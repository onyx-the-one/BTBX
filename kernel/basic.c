#include "basic.h"
#include "fat12.h"

/* ── x87 math ─────────────────────────────────────────────────────────── */
static double x87_sin(double x)  { double r; __asm__("fsin"  :"=t"(r):"0"(x)); return r; }
static double x87_cos(double x)  { double r; __asm__("fcos"  :"=t"(r):"0"(x)); return r; }
static double x87_sqrt(double x) { double r; __asm__("fsqrt" :"=t"(r):"0"(x)); return r; }
static double x87_log(double x)  { double r; __asm__("fldln2\nfxch\nfyl2x":"=t"(r):"0"(x)); return r; }
static double x87_exp(double x) {
    double r;
    __asm__("fldl2e\nfmulp\nfld1\nfscale\nfxch\nfprem\nf2xm1\nfld1\nfaddp\nfscale\nfstp %%st(1)"
            :"=t"(r):"0"(x));
    return r;
}
static double  x87_tan(double x) { return x87_sin(x)/x87_cos(x); }
static double  x87_atn(double x) { double r; __asm__("fpatan":"=t"(r):"0"(x),"u"(1.0)); return r; }
static double  x87_abs(double x) { return x < 0.0 ? -x : x; }
static double  x87_int(double x) { double r; __asm__("frndint":"=t"(r):"0"(x)); return r; }
static double  x87_fix(double x) { return x < 0.0 ? -x87_int(-x) : x87_int(x); }
static int32_t x87_sgn(double x) { return (x > 0.0) - (x < 0.0); }

static uint32_t rng_state = 12345;
static double rnd(void) {
    rng_state = rng_state * 1664525u + 1013904223u;
    return (double)(rng_state >> 1) / 2147483648.0;
}

/* ── Value type ──────────────────────────────────────────────────────── */
#define TY_INT   0
#define TY_FLOAT 1
#define TY_STR   2
typedef struct { int ty; int32_t i; double f; int si; } Val;

/* ── String pool ─────────────────────────────────────────────────────── */
#define NSTRS 64
#define SLEN  128
static char spool[NSTRS][SLEN];
static int  sused[NSTRS];

static int snew(void) {
    for (int i = 0; i < NSTRS; i++) if (!sused[i]) { sused[i]=1; spool[i][0]=0; return i; }
    return -1;
}
static void sfree(int i) { if (i >= 0 && i < NSTRS) sused[i] = 0; }
static Val vint(int32_t i) { Val v; v.ty=TY_INT;   v.i=i; v.f=(double)i; v.si=-1; return v; }
static Val vflt(double f)  { Val v; v.ty=TY_FLOAT; v.f=f; v.i=(int32_t)f; v.si=-1; return v; }
static Val vstr(int si)    { Val v; v.ty=TY_STR;   v.si=si; v.i=0; v.f=0; return v; }

/* ── Arithmetic ──────────────────────────────────────────────────────── */
static Val vadd(Val a, Val b) {
    if (a.ty==TY_STR || b.ty==TY_STR) return vint(0);
    if (a.ty==TY_FLOAT || b.ty==TY_FLOAT)
        return vflt((a.ty==TY_FLOAT?a.f:(double)a.i)+(b.ty==TY_FLOAT?b.f:(double)b.i));
    return vint(a.i + b.i);
}
static Val vsub(Val a, Val b) {
    if (a.ty==TY_STR || b.ty==TY_STR) return vint(0);
    if (a.ty==TY_FLOAT || b.ty==TY_FLOAT)
        return vflt((a.ty==TY_FLOAT?a.f:(double)a.i)-(b.ty==TY_FLOAT?b.f:(double)b.i));
    return vint(a.i - b.i);
}
static Val vmul(Val a, Val b) {
    if (a.ty==TY_STR || b.ty==TY_STR) return vint(0);
    if (a.ty==TY_FLOAT || b.ty==TY_FLOAT)
        return vflt((a.ty==TY_FLOAT?a.f:(double)a.i)*(b.ty==TY_FLOAT?b.f:(double)b.i));
    return vint(a.i * b.i);
}
static Val vdiv(Val a, Val b) {
    double da = (a.ty==TY_FLOAT)?a.f:(double)a.i;
    double db = (b.ty==TY_FLOAT)?b.f:(double)b.i;
    return vflt(da / db);
}
static Val vmod(Val a, Val b) {
    if (!b.i && b.ty==TY_INT) return vint(0);
    return vint(a.i % b.i);
}
static int vcmp(Val a, Val b) {
    if (a.ty==TY_STR && b.ty==TY_STR) {
        const char *p=spool[a.si], *q=spool[b.si];
        while (*p && *p==*q) { p++; q++; }
        return (*p>*q)-(*p<*q);
    }
    double d = (a.ty==TY_FLOAT?a.f:(double)a.i) - (b.ty==TY_FLOAT?b.f:(double)b.i);
    return (d>0)-(d<0);
}
static void vprint(Val v) {
    if (v.ty==TY_STR) { term_puts(spool[v.si]); return; }
    if (v.ty==TY_INT)  term_puti(v.i); else term_putf(v.f);
}

/* ── Variable table ──────────────────────────────────────────────────── */
#define MAXVARS 128
#define NAMELEN  32
typedef struct { char name[NAMELEN]; Val val; int is_str; } Var;
static Var vars[MAXVARS]; static int nvars = 0;

static int bstreq(const char *a, const char *b) {
    while (*a && *a==*b) { a++; b++; } return !*a && !*b;
}
static void bstrcpy(char *d, const char *s, int max) {
    int i=0; while (*s && i<max-1) *d++=*s++; *d=0;
}
static void upr(char *s) { while (*s) { if (*s>='a'&&*s<='z') *s-=32; s++; } }

static Var *var_get(const char *nm) {
    for (int i=0;i<nvars;i++) if (bstreq(vars[i].name,nm)) return &vars[i];
    return 0;
}
static Var *var_set(const char *nm, Val v) {
    Var *vp = var_get(nm);
    if (vp) { if (vp->val.ty==TY_STR && v.ty!=TY_STR) sfree(vp->val.si); vp->val=v; return vp; }
    if (nvars>=MAXVARS) return 0;
    Var *nv = &vars[nvars++];
    int i=0; while (nm[i] && i<NAMELEN-1) { nv->name[i]=nm[i]; i++; } nv->name[i]=0;
    nv->is_str = (nm[i-1]=='$');
    nv->val = v;
    return nv;
}

/* ── Program store ───────────────────────────────────────────────────── */
#define MAX_LINES 1024
#define LINE_LEN  128
typedef struct { uint16_t num; char txt[LINE_LEN]; } Line;
static Line P[MAX_LINES]; static int Pn = 0;

/* ── Shared file I/O buffer (avoids double static in SAVE/LOAD) ──────── */
/* MAX_LINES * LINE_LEN = 1024 * 128 = 128 KB — keep as static global     */
static char fbuf[MAX_LINES * LINE_LEN];

/* ── Runtime state ───────────────────────────────────────────────────── */
#define CALL_DEPTH  64
#define FOR_DEPTH   32
#define WHILE_DEPTH 32
#define IBUF       256
typedef struct { char var[NAMELEN]; Val lim, step; int ret; } ForFrame;
typedef struct { int ret; } WhileFrame;

static int        pc=0, running=0, err=0;
static int        cstk[CALL_DEPTH];  static int csp=0;
static ForFrame   fstk[FOR_DEPTH];   static int fsp=0;
static WhileFrame wstk[WHILE_DEPTH]; static int wsp=0;
static char       ibuf[IBUF];
static const char *pp;
static int        data_line=0, data_col=0;

static void sw(void) { while (*pp==' '||*pp=='\t') pp++; }

static void berr(const char *m) {
    if (err) return;
    err=1; running=0;
    term_set_color(VGA_LIGHT_RED, VGA_BLACK);
    term_puts("\n? "); term_puts(m); term_putchar('\n');
    term_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

static int kw(const char *k) {
    const char *p = pp;
    while (*k) { char a=*p, b=*k; if(a>='a'&&a<='z') a-=32; if(a!=b) return 0; p++; k++; }
    char nx = *p;
    if ((nx>='A'&&nx<='Z')||(nx>='a'&&nx<='z')||(nx>='0'&&nx<='9')||nx=='_') return 0;
    pp = p; return 1;
}

static int read_name(char *out, int max) {
    sw();
    char c = *pp;
    if (!((c>='A'&&c<='Z')||(c>='a'&&c<='z'))) return 0;
    int i = 0;
    while ((*pp>='A'&&*pp<='Z')||(*pp>='a'&&*pp<='z')||(*pp>='0'&&*pp<='9')||*pp=='_'||
           (*pp=='$'&&i>0&&pp[1]!='(')) {
        char ch = *pp++;
        if (ch>='a'&&ch<='z') ch-=32;
        if (i<max-1) out[i++]=ch;
    }
    out[i]=0; return i>0;
}

static Val expr(void);
static Val strexpr(void);

static int pnum(double *o) {
    sw();
    if (!(*pp>='0'&&*pp<='9') && !(*pp=='.'&&pp[1]>='0'&&pp[1]<='9')) return 0;
    double v=0; int fdiv=1, frac=0;
    while (*pp>='0'&&*pp<='9') v=v*10+(*pp++-'0');
    if (*pp=='.') { pp++; while (*pp>='0'&&*pp<='9') { v=v*10+(*pp++-'0'); fdiv*=10; frac=1; } }
    if (frac) v/=fdiv;
    if (*pp=='E'||*pp=='e') {
        pp++; int neg=0, ex=0;
        if (*pp=='-') { neg=1; pp++; } else if (*pp=='+') pp++;
        while (*pp>='0'&&*pp<='9') ex=ex*10+(*pp++-'0');
        double m=1; for (int i=0;i<ex;i++) m*=10.0;
        if (neg) v/=m; else v*=m;
    }
    *o=v; return 1;
}

/* ── String functions ────────────────────────────────────────────────── */
static Val call_strfn(const char *nm);
static Val numfn(const char *nm);

static Val call_strfn(const char *nm) {
    sw(); pp++;  /* consume '(' */
    if (bstreq(nm,"LEFT$")) {
        Val s=strexpr(); if(err) return vstr(-1);
        sw(); if(*pp!=','){berr("SYNTAX");return vstr(-1);} pp++;
        Val n=expr(); if(err) return vstr(-1);
        sw(); if(*pp!=')'){berr("SYNTAX");return vstr(-1);} pp++;
        int r=snew(); if(r<0){berr("MEM");return vstr(-1);}
        const char *src=spool[s.si];
        int i=0; while(*src&&i<n.i&&i<SLEN-1) spool[r][i++]=*src++;
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
        const char *src=spool[s.si]; int l=0; const char *p=src; while(*p++) l++;
        int start=l-n.i; if(start<0) start=0;
        int i=0; while(src[start]&&i<SLEN-1) spool[r][i++]=src[start++];
        spool[r][i]=0;
        if(s.si>=0&&s.ty==TY_STR) sfree(s.si);
        return vstr(r);
    }
    if (bstreq(nm,"MID$")) {
        Val s=strexpr(); if(err) return vstr(-1);
        sw(); if(*pp!=','){berr("SYNTAX");return vstr(-1);} pp++;
        Val start=expr(); if(err) return vstr(-1);
        int cnt=-1;
        sw(); if(*pp==','){pp++; Val n=expr(); if(err) return vstr(-1); cnt=n.i;}
        sw(); if(*pp!=')'){berr("SYNTAX");return vstr(-1);} pp++;
        int r=snew(); if(r<0){berr("MEM");return vstr(-1);}
        const char *src=spool[s.si]; int st=start.i-1; if(st<0) st=0;
        int i=0; while(*src&&i<st) { src++; }
        while(*src&&(cnt<0||i<cnt)&&i<SLEN-1) spool[r][i++]=*src++;
        spool[r][i]=0;
        if(s.si>=0&&s.ty==TY_STR) sfree(s.si);
        return vstr(r);
    }
    if (bstreq(nm,"STR$")) {
        Val n=expr(); if(err) return vstr(-1);
        sw(); if(*pp!=')'){berr("SYNTAX");return vstr(-1);} pp++;
        int r=snew(); if(r<0){berr("MEM");return vstr(-1);}
        char *d=spool[r]; int i=0;
        if(n.ty==TY_FLOAT) {
            double f=n.f; if(f<0){*d++='-';f=-f;i++;}
            int32_t ip=(int32_t)f; double fp=f-(double)ip;
            char tmp[12]; int ti=0;
            if(!ip){tmp[ti++]='0';} else {int32_t t=ip;while(t){tmp[ti++]='0'+(t%10);t/=10;}}
            while(ti>0&&i<SLEN-2) spool[r][i++]=tmp[--ti];
            char fb[12]; int fi=0;
            for(int k=0;k<6;k++){fp*=10.0;int dg=(int)fp;fb[fi++]='0'+dg;fp-=(double)dg;}
            while(fi>1&&fb[fi-1]=='0') fi--;
            if(fi>0&&i<SLEN-2){spool[r][i++]='.';for(int k=0;k<fi&&i<SLEN-2;k++)spool[r][i++]=fb[k];}
        } else {
            int32_t iv=n.i; int neg=0;
            if(iv<0){neg=1;iv=-iv;}
            char tmp[12]; int ti=0;
            if(!iv){tmp[ti++]='0';} else {int32_t t=iv;while(t){tmp[ti++]='0'+(t%10);t/=10;}}
            if(neg&&i<SLEN-2) spool[r][i++]='-';
            while(ti>0&&i<SLEN-2) spool[r][i++]=tmp[--ti];
        }
        spool[r][i]=0;
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
        const char *p=spool[s.si];
        int neg=0; double n=0; int fdiv=1,frac=0;
        if(*p=='-'){neg=1;p++;}
        while(*p>='0'&&*p<='9') n=n*10+(*p++-'0');
        if(*p=='.'){p++;while(*p>='0'&&*p<='9'){n=n*10+(*p++-'0');fdiv*=10;frac=1;}}
        if(frac) n/=fdiv;
        if(neg) n=-n;
        if(s.ty==TY_STR) sfree(s.si);
        return (n==(double)(int32_t)n)?vint((int32_t)n):vflt(n);
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
    sw(); pp++;  /* consume '(' */
    Val a=expr(); if(err) return vint(0);
    Val second; int has_second=0;
    sw(); if(*pp==','){pp++;second=expr();if(err)return vint(0);has_second=1;}
    sw(); if(*pp!=')'){berr("SYNTAX");return vint(0);} pp++;
    if(bstreq(nm,"MOD"))  { if(!has_second){berr("SYNTAX");return vint(0);} return vmod(a,second); }
    if(bstreq(nm,"SIN"))  return vflt(x87_sin(a.f));
    if(bstreq(nm,"COS"))  return vflt(x87_cos(a.f));
    if(bstreq(nm,"TAN"))  return vflt(x87_tan(a.f));
    if(bstreq(nm,"ATN"))  return vflt(x87_atn(a.f));
    if(bstreq(nm,"EXP"))  return vflt(x87_exp(a.f));
    if(bstreq(nm,"LOG"))  { if(a.f<=0){berr("MATH");return vint(0);} return vflt(x87_log(a.f)); }
    if(bstreq(nm,"SQR"))  { if(a.f<0){berr("MATH");return vint(0);}  return vflt(x87_sqrt(a.f)); }
    if(bstreq(nm,"ABS"))  return (a.ty==TY_FLOAT)?vflt(x87_abs(a.f)):vint(a.i<0?-a.i:a.i);
    if(bstreq(nm,"INT"))  return vflt(x87_int(a.f));
    if(bstreq(nm,"FIX"))  return vflt(x87_fix(a.f));
    if(bstreq(nm,"SGN"))  return vint(x87_sgn(a.f));
    if(bstreq(nm,"RND"))  return vflt(rnd());
    if(bstreq(nm,"CINT")) return vint((int32_t)(a.f+0.5));
    if(bstreq(nm,"CDBL")) return vflt(a.f);
    berr("UNKNOWN FUNCTION"); return vint(0);
}

static int isstrname(const char *nm) {
    int i=0; while(nm[i]) i++; return i>0&&nm[i-1]=='$';
}

static Val strexpr(void) {
    sw();
    Val a;
    if (*pp=='"') {
        pp++;
        int r=snew(); if(r<0){berr("MEM");return vstr(-1);}
        int i=0; while(*pp&&*pp!='"'&&i<SLEN-1) spool[r][i++]=*pp++;
        spool[r][i]=0;
        if(*pp=='"') pp++;
        a=vstr(r);
    } else {
        const char *save=pp;
        char nm[NAMELEN];
        if(read_name(nm,NAMELEN)&&isstrname(nm)) {
            sw();
            if(*pp=='(') { a=call_strfn(nm); }
            else {
                Var *vp=var_get(nm);
                if(!vp||vp->val.ty!=TY_STR) {
                    int r=snew(); if(r<0){berr("MEM");return vstr(-1);}
                    spool[r][0]=0; a=vstr(r);
                } else {
                    int r=snew(); if(r<0){berr("MEM");return vstr(-1);}
                    bstrcpy(spool[r],spool[vp->val.si],SLEN); a=vstr(r);
                }
            }
        } else { pp=save; berr("TYPE MISMATCH"); return vstr(-1); }
    }
    /* string concatenation */
    for(;;) {
        sw(); if(*pp!='+') break;
        pp++; Val b=strexpr(); if(err) break;
        Val c=vadd(a,b); sfree(a.si); sfree(b.si); a=c;
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
    while(!err) {
        sw();
        if(*pp=='*'){pp++;a=vmul(a,fact());}
        else if(*pp=='/'){pp++;Val b=fact();if(b.ty==TY_INT&&b.i==0){berr("DIV/0");return vint(0);}a=vdiv(a,b);}
        else if(*pp=='\\'){pp++;Val b=fact();if(!b.i&&b.ty==TY_INT){berr("DIV/0");return vint(0);}a=vint(a.i/b.i);}
        else break;
    }
    return a;
}

static Val expr(void) {
    Val a=term_expr();
    while(!err) { sw(); if(*pp=='+'){pp++;a=vadd(a,term_expr());} else if(*pp=='-'){pp++;a=vsub(a,term_expr());} else break; }
    return a;
}

static int relop(void) {
    sw();
    if(*pp=='='){pp++;return 1;}
    if(*pp=='<'){pp++;if(*pp=='>'){pp++;return 2;}if(*pp=='='){pp++;return 4;}return 3;}
    if(*pp=='>'){pp++;if(*pp=='='){pp++;return 6;}return 5;}
    return 0;
}
static int applyrel(int op,Val a,Val b) {
    int c=vcmp(a,b);
    switch(op){case 1:return c==0;case 2:return c!=0;case 3:return c<0;
               case 4:return c<=0;case 5:return c>0;case 6:return c>=0;}
    return 0;
}
static int findge(int n) {
    for(int i=0;i<Pn;i++) if((int)P[i].num>=(int)n) return i;
    return Pn;
}
static void storeline(int n, const char *t) {
    int lo=0,hi=Pn,idx=0,found=0;
    while(lo<=hi) { int m=(lo+hi)/2; if(m<Pn&&(int)P[m].num==(int)n){idx=m;found=1;break;} if(m>=Pn||(int)P[m].num>n){hi=m-1;idx=m;} else {idx=m+1;lo=m+1;} }
    if(found) { if(!t[0]){for(int i=idx;i<Pn-1;i++) P[i]=P[i+1];Pn--;return;} bstrcpy(P[idx].txt,t,LINE_LEN); return; }
    if(!t[0]) return;
    if(Pn>=MAX_LINES){berr("MEM");return;}
    for(int i=Pn;i>idx;i--) P[i]=P[i-1];
    Pn++;
    P[idx].num=(uint16_t)n; bstrcpy(P[idx].txt,t,LINE_LEN);
}

/* ── name11 helper: "file.bas" → "FILE    BAS" ───────────────────────── */
static void to_name11(const char *s, char *nm11) {
    char base[8], ext[3];
    for(int i=0;i<8;i++) base[i]=' ';
    for(int i=0;i<3;i++) ext[i]=' ';
    int ni=0, ei=0;
    while(*s && *s!='.') { if(ni<8){char c=*s;if(c>='a'&&c<='z')c-=32;base[ni++]=c;} s++; }
    if(*s=='.') { s++; while(*s){if(ei<3){char c=*s;if(c>='a'&&c<='z')c-=32;ext[ei++]=c;}s++;} }
    for(int i=0;i<8;i++) nm11[i]=base[i];
    for(int i=0;i<3;i++) nm11[8+i]=ext[i];
    nm11[11]=0;
}

/* ── DIR callback ────────────────────────────────────────────────────── */
static void dir_cb(const char *name11, uint32_t size) {
    /* print 8.3 name nicely: strip trailing spaces from name & ext */
    char out[13]; int oi=0;
    int nlen=0;
    for(int i=7;i>=0;i--) { if(name11[i]!=' '){nlen=i+1;break;} }
    for(int i=0;i<nlen;i++) out[oi++]=name11[i];
    int elen=0;
    for(int i=10;i>=8;i--) { if(name11[i]!=' '){elen=i-8+1;break;} }
    if(elen>0) { out[oi++]='.'; for(int i=0;i<elen;i++) out[oi++]=name11[8+i]; }
    out[oi]=0;
    term_puts("  "); term_puts(out);
    /* pad to 13 chars */
    int pad=13-oi; while(pad-->0) term_putchar(' ');
    term_puti((int32_t)size);
    term_puts(" bytes\n");
}

/* ── Statement dispatcher ─────────────────────────────────────────────── */
static void stmt(const char *line);

static void stmt(const char *line) {
    pp=line; sw(); if(!*pp) return;
    if(kw("REM")) return;

    if(kw("KPAN")) { kpanic("KPAN"); }

    /* CLEAR — redraw banner, reset screen */
    if(kw("CLEAR")) { draw_banner(); return; }

    /* HALT — freeze */
    if(kw("HALT")) {
        term_set_color(VGA_YELLOW,VGA_BLACK);
        term_puts("\nSystem halted.\n");
        __asm__ volatile("cli");
        for(;;) __asm__ volatile("hlt");
    }

    /* NEW */
    if(kw("NEW")) {
        Pn=0; nvars=0; csp=0; fsp=0; wsp=0; data_line=0; data_col=0;
        for(int i=0;i<NSTRS;i++) sused[i]=0;
        return;
    }

    /* END */
    if(kw("END")) { running=0; return; }

    /* LIST */
    if(kw("LIST")) {
        for(int i=0;i<Pn;i++) {
            term_set_color(VGA_DARK_GREY,VGA_BLACK); term_puti(P[i].num); term_putchar(' ');
            term_set_color(VGA_LIGHT_GREY,VGA_BLACK); term_puts(P[i].txt); term_putchar('\n');
        }
        return;
    }

    /* RUN */
    if(kw("RUN")) { pc=0; running=1; csp=0; fsp=0; wsp=0; data_line=0; data_col=0;
        for(int i=0;i<NSTRS;i++) sused[i]=0;
        nvars=0; return; }

    /* LET or bare assignment */
    if(kw("LET") || 1) {
        const char *save=pp;
        char nm[NAMELEN];
        if(read_name(nm,NAMELEN)) {
            sw();
            if(*pp=='=') {
                pp++;
                if(isstrname(nm)) { Val v=strexpr(); if(!err) var_set(nm,v); return; }
                Val v=expr(); if(!err) var_set(nm,v); return;
            }
        }
        pp=save;
    }

    if(kw("PRINT")||kw("?")) {
        int nl=1;
        if(!*pp||*pp=='\n'||*pp=='\r') { term_putchar('\n'); return; }
        for(;;) {
            sw(); if(!*pp||*pp=='\n'||*pp=='\r') break;
            const char *save=pp;
            int is_s=0;
            char nm[NAMELEN];
            if(read_name(nm,NAMELEN)) {
                sw();
                if(*pp=='(' || isstrname(nm) || (*pp=='"') || (*nm>='A'&&*nm<='Z'))
                    is_s = isstrname(nm)||*pp=='"';
                pp=save;
            } else pp=save;
            if(*pp=='"') is_s=1;
            if(is_s){ Val v=strexpr(); if(!err){ term_puts(spool[v.si]); sfree(v.si); } }
            else { Val v=expr(); if(!err) vprint(v); }
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
        if(*pp=='"') {
            pp++; while(*pp&&*pp!='"') term_putchar(*pp++);
            if(*pp=='"') pp++;
            sw(); if(*pp==','||*pp==';') pp++;
        } else { term_putchar('?'); term_putchar(' '); }
        for(;;) {
            sw(); char nm[NAMELEN];
            if(!read_name(nm,NAMELEN)){berr("SYNTAX");return;}
            char lb[IBUF]; term_get_line(lb,IBUF);
            if(isstrname(nm)) {
                int r=snew(); if(r<0){berr("MEM");return;}
                bstrcpy(spool[r],lb,SLEN); var_set(nm,vstr(r));
            } else {
                upr(lb);
                const char *sv=pp; pp=lb; sw();
                double n=0; pnum(&n);
                var_set(nm,(n==(double)(int32_t)n)?vint((int32_t)n):vflt(n));
                pp=sv;
            }
            sw(); if(*pp!=',') break; pp++; term_puts("? ");
        }
        return;
    }

    if(kw("DATA")) return;

    if(kw("READ")) {
        for(;;) {
            sw(); char nm[NAMELEN];
            if(!read_name(nm,NAMELEN)){berr("SYNTAX");return;}
            int found=0;
            for(int i=data_line;i<Pn&&!found;i++) {
                const char *dp=P[i].txt;
                const char *save2=pp; pp=dp;
                if(kw("DATA")) {
                    /* skip to data_col */
                    int col=0;
                    while(*pp&&col<data_col) { while(*pp&&*pp!=',') pp++; if(*pp==','){pp++;col++;} }
                    sw();
                    if(*pp) {
                        if(isstrname(nm)) {
                            int r=snew(); if(r<0){berr("MEM");pp=save2;return;}
                            int k=0; while(*pp&&*pp!=','&&k<SLEN-1) spool[r][k++]=*pp++;
                            spool[r][k]=0; var_set(nm,vstr(r));
                        } else {
                            double n=0; pnum(&n);
                            var_set(nm,(n==(double)(int32_t)n)?vint((int32_t)n):vflt(n));
                        }
                        data_line=i; data_col++;
                        found=1;
                    }
                }
                pp=save2;
            }
            if(!found){berr("OUT OF DATA");return;}
            sw(); if(*pp!=',') break; pp++;
        }
        return;
    }

    if(kw("RESTORE")) { data_line=0; data_col=0; return; }

    if(kw("GOTO")) {
        sw(); Val n=expr(); if(err) return;
        int i=findge((int)n.i);
        if(i>=Pn||(int)P[i].num!=(int)n.i){berr("LINE NOT FOUND");return;}
        pc=i; return;
    }

    if(kw("GOSUB")) {
        sw(); Val n=expr(); if(err) return;
        int i=findge((int)n.i);
        if(i>=Pn||(int)P[i].num!=(int)n.i){berr("LINE NOT FOUND");return;}
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
        if(!go) {
            int depth=0;
            while(pc<Pn) {
                const char *t=P[pc++].txt;
                const char *sv=pp; pp=t;
                if(kw("FOR")) depth++;
                else if(kw("NEXT")) { if(!depth) break; depth--; }
                pp=sv;
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
        if(cond.i||cond.f!=0.0) {
            if(wsp>=WHILE_DEPTH){berr("WHILE OVERFLOW");return;}
            wstk[wsp++].ret=pc-1;
        } else {
            int depth=0;
            while(pc<Pn) {
                const char *t=P[pc++].txt;
                const char *sv=pp; pp=t;
                if(kw("WHILE")) depth++;
                else if(kw("WEND")) { if(!depth) break; depth--; }
                pp=sv;
            }
        }
        return;
    }

    if(kw("WEND")) {
        if(wsp<=0){berr("WEND WITHOUT WHILE");return;}
        pc=wstk[wsp-1].ret;
        /* re-evaluate condition at WHILE line */
        const char *sv=pp; pp=P[pc++].txt;
        if(!kw("WHILE")){berr("WEND MISMATCH");pp=sv;return;}
        Val cond=expr(); pp=sv;
        if(!cond.i&&cond.f==0.0) wsp--;
        return;
    }

    if(kw("IF")) {
        Val cond;
        const char *save=pp;
        if(*pp=='"'||((*pp>='A'&&*pp<='Z')||(*pp>='a'&&*pp<='z'))) {
            char nm[NAMELEN]; const char *sv=pp;
            if(read_name(nm,NAMELEN)&&isstrname(nm)) {
                pp=sv;
                Val a=strexpr(); if(err) return;
                int op=relop(); if(!op){berr("SYNTAX");return;}
                Val b=strexpr(); if(err) return;
                cond=vint(applyrel(op,a,b));
                sfree(a.si); sfree(b.si);
            } else { pp=save; cond=expr(); if(err) return; }
        } else { cond=expr(); if(err) return; }
        sw(); if(!kw("THEN")){berr("SYNTAX");return;}
        sw();
        if(*pp>='0'&&*pp<='9') {
            if(!cond.i&&!cond.f) return;
            Val n=expr(); if(err) return;
            int i=findge((int)n.i);
            if(i>=Pn||(int)P[i].num!=(int)n.i){berr("LINE NOT FOUND");return;}
            pc=i; return;
        }
        if(*pp&&*pp!='\0') { if(cond.i||cond.f!=0.0) stmt(pp); return; }
        /* multi-line IF/ELSE/ENDIF */
        if(!cond.i&&cond.f==0.0) {
            int depth=0;
            while(pc<Pn) {
                const char *t=P[pc++].txt; const char *sv=pp; pp=t;
                if(kw("IF")) depth++;
                else if(!depth&&kw("ELSE")) { pp=sv; break; }
                else if(!depth&&kw("ENDIF")) { pp=sv; break; }
                else if(kw("ENDIF")&&depth) depth--;
                pp=sv;
            }
        }
        return;
    }

    if(kw("ELSE")||kw("ENDIF")) {
        /* skip to ENDIF */
        int depth=0;
        while(pc<Pn) {
            const char *t=P[pc++].txt; const char *sv=pp; pp=t;
            if(kw("IF")) depth++;
            else if(kw("ENDIF")&&!depth) { pp=sv; return; }
            else if(kw("ENDIF")) depth--;
            pp=sv;
        }
        return;
    }

    if(kw("DIR")) {
        if(!fat_ready()) { berr("DISK NOT READY"); return; }
        term_set_color(VGA_CYAN, VGA_BLACK);
        term_puts("  Name          Size\n");
        term_puts("  ------------ ------\n");
        term_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        fat_dir(dir_cb);
        term_putchar('\n');
        return;
    }

    if(kw("LOAD")) {
        sw();
        Val fn=strexpr(); if(err) return;
        if(!fat_ready()) { sfree(fn.si); berr("DISK NOT READY"); return; }
        char nm11[12];
        to_name11(spool[fn.si], nm11);
        sfree(fn.si);

        int n = fat_load(nm11, fbuf, (int)sizeof(fbuf)-1);
        if(n<0){ berr("FILE NOT FOUND"); return; }
        fbuf[n]=0;

        /* replace program */
        Pn=0;
        char *p=fbuf;
        while(*p) {
            while(*p=='\r'||*p=='\n') p++;
            if(!*p) break;
            if(*p>='0'&&*p<='9') {
                int ln=0; while(*p>='0'&&*p<='9') ln=ln*10+(*p++-'0');
                while(*p==' '||*p=='\t') p++;
                char *start=p;
                while(*p&&*p!='\r'&&*p!='\n') p++;
                char saved=*p; *p=0;
                storeline(ln,start);
                *p=saved;
            } else {
                while(*p&&*p!='\r'&&*p!='\n') p++;
            }
        }
        term_set_color(VGA_DARK_GREY,VGA_BLACK);
        term_puti(Pn); term_puts(" lines\n");
        term_set_color(VGA_LIGHT_GREY,VGA_BLACK);
        return;
    }

    if(kw("SAVE")) {
        sw();
        Val fn=strexpr(); if(err) return;
        if(!fat_ready()) { sfree(fn.si); berr("DISK NOT READY"); return; }
        char nm11[12];
        to_name11(spool[fn.si], nm11);
        sfree(fn.si);

        /* serialise to fbuf */
        int pos=0;
        for(int i=0;i<Pn;i++) {
            uint16_t ln=P[i].num;
            /* line number as ascii */
            char nb[8]; int ni2=0; uint16_t tmp2=ln;
            if(!tmp2) nb[ni2++]='0';
            else { uint16_t t=tmp2; while(t){nb[ni2++]='0'+(t%10);t/=10;} }
            for(int k=ni2-1;k>=0&&pos<(int)sizeof(fbuf)-3;k--) fbuf[pos++]=nb[k];
            if(pos<(int)sizeof(fbuf)-2) fbuf[pos++]=' ';
            const char *src=P[i].txt;
            while(*src&&pos<(int)sizeof(fbuf)-3) fbuf[pos++]=*src++;
            if(pos<(int)sizeof(fbuf)-2) fbuf[pos++]='\r';
            if(pos<(int)sizeof(fbuf)-1) fbuf[pos++]='\n';
        }

        int r=fat_save(nm11, fbuf, pos);
        if(r<0){ berr("DISK FULL OR WRITE ERROR"); return; }
        term_set_color(VGA_DARK_GREY,VGA_BLACK);
        term_puti(pos); term_puts(" bytes\n");
        term_set_color(VGA_LIGHT_GREY,VGA_BLACK);
        return;
    }

    berr("WHAT?");
}

/* ── REPL ─────────────────────────────────────────────────────────────── */
void basic_run(void) {
    for(;;) {
        if(running) {
            if(pc>=Pn){running=0;continue;}
            int here=pc++; err=0;
            stmt(P[here].txt);
            continue;
        }
        err=0;
        term_sync_cursor();
        term_set_color(VGA_LIGHT_GREEN,VGA_BLACK); term_puts("BTBX");
        term_set_color(VGA_DARK_GREY,VGA_BLACK);   term_puts("> ");
        term_set_color(VGA_WHITE,VGA_BLACK);
        term_get_line(ibuf,IBUF); upr(ibuf);
        const char *p=ibuf;
        while(*p==' '||*p=='\t') p++;
        if(!*p) continue;
        if(*p>='0'&&*p<='9') {
            int n=0; while(*p>='0'&&*p<='9') n=n*10+(*p++-'0');
            if(n<1||n>65535){berr("BAD LINE NUMBER");continue;}
            while(*p==' '||*p=='\t') p++;
            storeline(n,p);
        } else {
            stmt(p);
        }
    }
}
