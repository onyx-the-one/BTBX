/*
 * BTBX basic.c
 * TinyBASIC interpreter for bare-metal x86
 *
 * Features:
 *   Variables : A-Z  (32-bit signed integers)
 *   Storage   : up to 512 numbered lines, sorted
 *   Statements: LET  IF/THEN  GOTO  GOSUB  RETURN  FOR/NEXT
 *               PRINT  INPUT  REM  LIST  RUN  NEW  END
 *   Functions : ABS()  SQR()
 *   PRINT     : "strings" and numeric exprs, ; and , separators
 *   IF expr relop expr THEN statement-or-lineno
 *   FOR v=e1 TO e2 [STEP e3] ... NEXT [v]
 */

#include "basic.h"

/* ======================================================================
 * Configuration
 * ====================================================================== */
#define MAX_LINES    512      /* stored program lines */
#define LINE_LEN     128      /* max chars per line text */
#define GOSUB_DEPTH   64
#define FOR_DEPTH      32
#define IBUF_LEN     256

/* ======================================================================
 * Types
 * ====================================================================== */
typedef int32_t val_t;

typedef struct {
	    uint16_t num;
    char     text[LINE_LEN];
} Line;

typedef struct {
	    int      var;       /* 0-25 */
    val_t    limit;
    val_t    step;
    int      next_idx;  /* index of line AFTER this FOR line */
} ForFrame;

/* ======================================================================
 * Global state
 * ====================================================================== */
static Line      prog[MAX_LINES];
static int       prog_n   = 0;

static val_t     vars[26];

/* Run-time */
static int       run_pc   = 0;   /* index into prog[] */
static int       running  = 0;

static int       gosub_stk[GOSUB_DEPTH];
static int       gosub_sp = 0;

static ForFrame  for_stk[FOR_DEPTH];
static int       for_sp   = 0;

/* Input / parse cursor */
static char      ibuf[IBUF_LEN];
static const char *parse_ptr;

/* Error state */
static int       err_flag = 0;

/* ======================================================================
 * Simple string helpers (no libc)
 * ====================================================================== */
static int bstrlen(const char *s) {
	    int n = 0; while (*s++) n++; return n;
}
static void bstrcpy(char *d, const char *s) {
	    while ((*d++ = *s++));
}
static int bstrcmp(const char *a, const char *b) {
	    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
static void bstrupr(char *s) {
	    while (*s) {
	        if (*s >= 'a' && *s <= 'z') *s -= 32;
        s++;
    }
}

/* ======================================================================
 * isqrt – integer square root
 * ====================================================================== */
static val_t isqrt(val_t n) {
	    if (n <= 0) return 0;
    val_t x = n, y = 1;
    while (x > y) { x = (x + y) / 2; y = n / x; }
    return x;
}

/* ======================================================================
 * Error reporting
 * ====================================================================== */
static void basic_error(const char *msg) {
	    if (err_flag) return;
    err_flag = 1;
    running  = 0;
    term_set_color(VGA_LIGHT_RED, VGA_BLACK);
    term_puts("\n  ? ");
    term_puts(msg);
    term_putchar('\n');
    term_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

/* ======================================================================
 * Parser helpers
 * ====================================================================== */
static void skip_ws(void) {
	    while (*parse_ptr == ' ' || *parse_ptr == '\t') parse_ptr++;
}

/* Match keyword kw (uppercase). Returns 1 and advances if matched,
   0 otherwise.  Boundary: next char must not be alphanumeric. */
static int match_kw(const char *kw) {
	    const char *p = parse_ptr;
    while (*kw) {
	        if (*p != *kw) return 0;
        p++; kw++;
    }
    /* boundary check */
    char bc = *p;
    if ((bc >= 'A' && bc <= 'Z') || (bc >= '0' && bc <= '9') ||
        (bc >= 'a' && bc <= 'z'))
        return 0;
    parse_ptr = p;
    return 1;
}

/* Parse unsigned integer. Returns 1+value in *out on success, else 0. */
static int parse_uint(val_t *out) {
	    if (*parse_ptr < '0' || *parse_ptr > '9') return 0;
    val_t v = 0;
    while (*parse_ptr >= '0' && *parse_ptr <= '9')
        v = v * 10 + (*parse_ptr++ - '0');
    *out = v;
    return 1;
}

/* Forward declarations for recursive descent */
static val_t parse_expr(void);
static val_t parse_term(void);
static val_t parse_factor(void);

static val_t parse_factor(void) {
	    skip_ws();
    if (err_flag) return 0;

    /* ABS(expr) */
    if (match_kw("ABS")) {
	        skip_ws();
        if (*parse_ptr != '(') { basic_error("SYNTAX"); return 0; }
        parse_ptr++;
        val_t v = parse_expr();
        skip_ws();
        if (*parse_ptr != ')') { basic_error("SYNTAX"); return 0; }
        parse_ptr++;
        return v < 0 ? -v : v;
    }

    /* SQR(expr) */
    if (match_kw("SQR")) {
	        skip_ws();
        if (*parse_ptr != '(') { basic_error("SYNTAX"); return 0; }
        parse_ptr++;
        val_t v = parse_expr();
        skip_ws();
        if (*parse_ptr != ')') { basic_error("SYNTAX"); return 0; }
        parse_ptr++;
        if (v < 0) { basic_error("MATH"); return 0; }
        return isqrt(v);
    }

    /* Parenthesized expression */
    if (*parse_ptr == '(') {
	        parse_ptr++;
        val_t v = parse_expr();
        skip_ws();
        if (*parse_ptr != ')') { basic_error("SYNTAX"); return 0; }
        parse_ptr++;
        return v;
    }

    /* Unary minus/plus */
    if (*parse_ptr == '-') { parse_ptr++; return -parse_factor(); }
    if (*parse_ptr == '+') { parse_ptr++; return  parse_factor(); }

    /* Variable */
    if (*parse_ptr >= 'A' && *parse_ptr <= 'Z') {
	        int idx = *parse_ptr - 'A';
        parse_ptr++;
        /* not followed by alphanumeric (simple variable, not keyword) */
        char nx = *parse_ptr;
        if ((nx >= 'A' && nx <= 'Z') || (nx >= 'a' && nx <= 'z') ||
            (nx >= '0' && nx <= '9')) {
	            basic_error("SYNTAX");
            return 0;
        }
        return vars[idx];
    }

    /* Numeric literal */
    val_t v;
    if (parse_uint(&v)) return v;

    basic_error("SYNTAX");
    return 0;
}

static val_t parse_term(void) {
	    val_t a = parse_factor();
    while (!err_flag) {
	        skip_ws();
        if (*parse_ptr == '*') {
	            parse_ptr++;
            a *= parse_factor();
        } else if (*parse_ptr == '/') {
	            parse_ptr++;
            val_t b = parse_factor();
            if (b == 0) { basic_error("DIV/0"); return 0; }
            a /= b;
        } else break;
    }
    return a;
}

static val_t parse_expr(void) {
	    val_t a = parse_term();
    while (!err_flag) {
	        skip_ws();
        if (*parse_ptr == '+') { parse_ptr++; a += parse_term(); }
        else if (*parse_ptr == '-') { parse_ptr++; a -= parse_term(); }
        else break;
    }
    return a;
}

/* Parse a relational operator. Returns 1-6 or 0 on failure.
   1== 2<> 3< 4<= 5> 6>= */
static int parse_relop(void) {
	    skip_ws();
    if (*parse_ptr == '=') { parse_ptr++; return 1; }
    if (*parse_ptr == '<') {
	        parse_ptr++;
        if (*parse_ptr == '>') { parse_ptr++; return 2; }
        if (*parse_ptr == '=') { parse_ptr++; return 4; }
        return 3;
    }
    if (*parse_ptr == '>') {
	        parse_ptr++;
        if (*parse_ptr == '=') { parse_ptr++; return 6; }
        return 5;
    }
    return 0;
}

/* Apply relational operator */
static int apply_relop(int op, val_t a, val_t b) {
	    switch (op) {
	        case 1: return a == b;
        case 2: return a != b;
        case 3: return a <  b;
        case 4: return a <= b;
        case 5: return a >  b;
        case 6: return a >= b;
    }
    return 0;
}

/* ======================================================================
 * Program storage helpers
 * ====================================================================== */

/* Find the index of line number n, or where it would be inserted.
   Returns index; *found=1 if line exists. */
static int find_line(int n, int *found) {
	    int lo = 0, hi = prog_n;
    while (lo < hi) {
	        int mid = (lo + hi) / 2;
        if (prog[mid].num == (uint16_t)n) { *found = 1; return mid; }
        if (prog[mid].num  < (uint16_t)n) lo = mid + 1;
        else                              hi = mid;
    }
    *found = 0;
    return lo;
}

/* Find the index of the first line >= n, or prog_n if none. */
static int find_line_ge(int n) {
	    for (int i = 0; i < prog_n; i++)
        if (prog[i].num >= (uint16_t)n) return i;
    return prog_n;
}

/* Store or delete a line. text=NULL or empty deletes. */
static void store_line(int n, const char *text) {
	    int found = 0;
    int idx   = find_line(n, &found);

    if (text == NULL || *text == '\0') {
	        if (!found) return;
        /* delete: shift left */
        for (int i = idx; i < prog_n - 1; i++)
            prog[i] = prog[i+1];
        prog_n--;
        return;
    }

    if (!found) {
	        if (prog_n >= MAX_LINES) { basic_error("MEM"); return; }
        /* insert: shift right */
        for (int i = prog_n; i > idx; i--)
            prog[i] = prog[i-1];
        prog_n++;
    }
    prog[idx].num = (uint16_t)n;
    bstrcpy(prog[idx].text, text);
}

/* ======================================================================
 * Statement execution
 * ====================================================================== */
static void exec_stmt(const char *line);

/* Execute a FOR-body jump or finish.
   Called from NEXT: increments var, checks condition, jumps back or pops. */
static void do_next(const char *rest) {
	    if (for_sp <= 0) { basic_error("NEXT WITHOUT FOR"); return; }
    ForFrame *f = &for_stk[for_sp - 1];

    /* Optional variable name check */
    parse_ptr = rest;
    skip_ws();
    if (*parse_ptr >= 'A' && *parse_ptr <= 'Z') {
	        int idx = *parse_ptr - 'A';
        if (idx != f->var) { basic_error("NEXT MISMATCH"); return; }
    }

    vars[f->var] += f->step;

    int cont;
    if (f->step > 0) cont = (vars[f->var] <= f->limit);
    else             cont = (vars[f->var] >= f->limit);

    if (cont) {
	        run_pc = f->next_idx;   /* jump to first line of body */
    } else {
	        for_sp--;               /* pop frame, continue after NEXT */
    }
}

/* Execute one statement given its text (already uppercased, past lineno) */
static void exec_stmt(const char *line) {
	    parse_ptr = line;
    skip_ws();
    if (!*parse_ptr || *parse_ptr == '\0') return;

    /* ---- REM ---- */
    if (match_kw("REM")) return;

    /* ---- NEW ---- */
    if (match_kw("NEW")) {
	        prog_n = 0;
        for (int i = 0; i < 26; i++) vars[i] = 0;
        gosub_sp = 0; for_sp = 0;
        return;
    }

    /* ---- LIST ---- */
    if (match_kw("LIST")) {
	        if (prog_n == 0) {
	            term_set_color(VGA_DARK_GREY, VGA_BLACK);
            term_puts("  (program is empty)\n");
            term_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        }
        for (int i = 0; i < prog_n; i++) {
	            term_set_color(VGA_YELLOW, VGA_BLACK);
            term_puti(prog[i].num);
            term_putchar(' ');
            term_set_color(VGA_LIGHT_GREY, VGA_BLACK);
            term_puts(prog[i].text);
            term_putchar('\n');
        }
        return;
    }

    /* ---- RUN ---- */
    if (match_kw("RUN")) {
	        for (int i = 0; i < 26; i++) vars[i] = 0;
        gosub_sp = 0; for_sp = 0;
        run_pc = 0;
        running = 1;
        return;
    }

    /* ---- END ---- */
    if (match_kw("END")) {
	        running = 0;
        return;
    }

    /* ---- GOTO ---- */
    if (match_kw("GOTO")) {
	        skip_ws();
        val_t n = parse_expr();
        if (err_flag) return;
        int idx = find_line_ge((int)n);
        if (idx >= prog_n || prog[idx].num != (uint16_t)n) {
	            basic_error("LINE NOT FOUND"); return;
        }
        run_pc = idx;
        return;
    }

    /* ---- GOSUB ---- */
    if (match_kw("GOSUB")) {
	        skip_ws();
        val_t n = parse_expr();
        if (err_flag) return;
        int idx = find_line_ge((int)n);
        if (idx >= prog_n || prog[idx].num != (uint16_t)n) {
	            basic_error("LINE NOT FOUND"); return;
        }
        if (gosub_sp >= GOSUB_DEPTH) { basic_error("GOSUB OVERFLOW"); return; }
        gosub_stk[gosub_sp++] = run_pc;
        run_pc = idx;
        return;
    }

    /* ---- RETURN ---- */
    if (match_kw("RETURN")) {
	        if (gosub_sp <= 0) { basic_error("RETURN WITHOUT GOSUB"); return; }
        run_pc = gosub_stk[--gosub_sp];
        return;
    }

    /* ---- FOR ---- */
    if (match_kw("FOR")) {
	        skip_ws();
        if (*parse_ptr < 'A' || *parse_ptr > 'Z') {
	            basic_error("SYNTAX"); return;
        }
        int var = *parse_ptr - 'A';
        parse_ptr++;
        skip_ws();
        if (*parse_ptr != '=') { basic_error("SYNTAX"); return; }
        parse_ptr++;

        val_t init = parse_expr();
        if (err_flag) return;
        skip_ws();
        if (!match_kw("TO")) { basic_error("SYNTAX"); return; }
        val_t limit = parse_expr();
        if (err_flag) return;

        val_t step = 1;
        skip_ws();
        if (match_kw("STEP")) {
	            step = parse_expr();
            if (err_flag) return;
        }
        if (step == 0) { basic_error("STEP=0"); return; }

        vars[var] = init;

        /* Check if body runs at all */
        int runs = (step > 0) ? (init <= limit) : (init >= limit);
        if (!runs) {
	            /* skip past matching NEXT */
            int depth = 0;
            while (run_pc < prog_n) {
	                parse_ptr = prog[run_pc].text;
                skip_ws();
                if (match_kw("FOR"))  depth++;
                else {
	                    parse_ptr = prog[run_pc].text;
                    skip_ws();
                    if (match_kw("NEXT")) {
	                        if (depth == 0) { run_pc++; break; }
                        depth--;
                    }
                }
                run_pc++;
            }
            return;
        }

        if (for_sp >= FOR_DEPTH) { basic_error("FOR OVERFLOW"); return; }
        ForFrame *f = &for_stk[for_sp++];
        f->var      = var;
        f->limit    = limit;
        f->step     = step;
        f->next_idx = run_pc;   /* body starts at current run_pc (next line) */
        return;
    }

    /* ---- NEXT ---- */
    if (match_kw("NEXT")) {
	        do_next(parse_ptr);
        return;
    }

    /* ---- IF ---- */
    if (match_kw("IF")) {
	        val_t a = parse_expr();
        if (err_flag) return;
        int op = parse_relop();
        if (!op) { basic_error("SYNTAX"); return; }
        val_t b = parse_expr();
        if (err_flag) return;
        skip_ws();
        if (!match_kw("THEN")) { basic_error("SYNTAX"); return; }
        if (!apply_relop(op, a, b)) return;
        /* THEN: either a line number or a statement */
        skip_ws();
        if (*parse_ptr >= '0' && *parse_ptr <= '9') {
	            val_t n = 0;
            while (*parse_ptr >= '0' && *parse_ptr <= '9')
                n = n * 10 + (*parse_ptr++ - '0');
            int idx = find_line_ge((int)n);
            if (idx >= prog_n || prog[idx].num != (uint16_t)n) {
	                basic_error("LINE NOT FOUND"); return;
            }
            run_pc = idx;
        } else {
	            exec_stmt(parse_ptr);
        }
        return;
    }

    /* ---- LET ---- */
    /* Explicit or implicit (omit keyword) */
    {
	        const char *save = parse_ptr;
        int explicit_let = match_kw("LET");
        skip_ws();
        if (*parse_ptr >= 'A' && *parse_ptr <= 'Z') {
	            int idx = *parse_ptr - 'A';
            const char *after_var = parse_ptr + 1;
            char nx = *after_var;
            if (!((nx >= 'A' && nx <= 'Z') || (nx >= '0' && nx <= '9'))) {
	                /* looks like a variable assignment */
                parse_ptr++;
                skip_ws();
                if (*parse_ptr == '=') {
	                    parse_ptr++;
                    val_t v = parse_expr();
                    if (!err_flag) vars[idx] = v;
                    return;
                }
            }
        }
        if (!explicit_let) parse_ptr = save;
    }

    /* ---- PRINT ---- */
    if (match_kw("PRINT")) {
	        skip_ws();
        int first = 1;
        while (!err_flag) {
	            skip_ws();
            if (!*parse_ptr) break;
            if (!first) {
	                /* separator consumed below */
            }
            first = 0;

            if (*parse_ptr == '"') {
	                /* string literal */
                parse_ptr++;
                while (*parse_ptr && *parse_ptr != '"')
                    term_putchar(*parse_ptr++);
                if (*parse_ptr == '"') parse_ptr++;
            } else {
	                val_t v = parse_expr();
                if (err_flag) break;
                term_puti(v);
            }

            skip_ws();
            if (*parse_ptr == ';') {
	                parse_ptr++;
                /* no separator */
            } else if (*parse_ptr == ',') {
	                parse_ptr++;
                term_putchar(' ');  /* simple column sep */
            } else {
	                break;
            }
        }
        if (!err_flag) term_putchar('\n');
        return;
    }

    /* ---- INPUT ---- */
    if (match_kw("INPUT")) {
	        /* Optional prompt string */
        skip_ws();
        if (*parse_ptr == '"') {
	            parse_ptr++;
            while (*parse_ptr && *parse_ptr != '"')
                term_putchar(*parse_ptr++);
            if (*parse_ptr == '"') parse_ptr++;
            skip_ws();
            if (*parse_ptr == ',') parse_ptr++;
            else if (*parse_ptr == ';') parse_ptr++;
        } else {
	            term_putchar('?');
            term_putchar(' ');
        }

        /* Collect variable list */
        while (!err_flag) {
	            skip_ws();
            if (*parse_ptr < 'A' || *parse_ptr > 'Z') {
	                basic_error("SYNTAX"); return;
            }
            int idx = *parse_ptr - 'A';
            parse_ptr++;

            char lbuf[IBUF_LEN];
            term_get_line(lbuf, IBUF_LEN);
            bstrupr(lbuf);
            const char *saved = parse_ptr;
            parse_ptr = lbuf;
            skip_ws();
            vars[idx] = parse_expr();
            if (err_flag) { parse_ptr = saved; return; }
            parse_ptr = saved;

            skip_ws();
            if (*parse_ptr != ',') break;
            parse_ptr++;
            term_puts("? ");
        }
        return;
    }

    basic_error("WHAT?");
}

/* ======================================================================
 * REPL
 * ====================================================================== */
void basic_run(void) {
	    for (;;) {
	        if (running) {
	            /* ---- run one stored line ---- */
            if (run_pc >= prog_n) {
	                running = 0;
            } else {
	                int this_pc = run_pc++;
                /* exec_stmt may change run_pc (GOTO/GOSUB/IF/FOR/NEXT) */
                /* We need to detect if run_pc was modified */
                int before = run_pc;
                err_flag = 0;

                /* Parse the stored line: it's already uppercased text */
                exec_stmt(prog[this_pc].text);

                /* If exec_stmt didn't touch run_pc, the ++ above stands.
                   If it did (GOTO/GOSUB/etc), run_pc is already set. */
                (void)before;
            }
            continue;
        }

        /* ---- REPL prompt ---- */
        err_flag = 0;
        term_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
        term_puts("BTBX");
        term_set_color(VGA_DARK_GREY, VGA_BLACK);
        term_puts("> ");
        term_set_color(VGA_WHITE, VGA_BLACK);

        term_get_line(ibuf, IBUF_LEN);
        bstrupr(ibuf);

        const char *p = ibuf;
        /* skip leading whitespace */
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) continue;

        /* Is it a line number? */
        if (*p >= '0' && *p <= '9') {
	            int n = 0;
            while (*p >= '0' && *p <= '9') n = n * 10 + (*p++ - '0');
            if (n < 1 || n > 65535) {
	                basic_error("BAD LINE NUMBER");
                continue;
            }
            while (*p == ' ' || *p == '\t') p++;
            store_line(n, p);    /* empty body = delete */
        } else {
	            /* direct statement */
            term_set_color(VGA_LIGHT_GREY, VGA_BLACK);
            err_flag = 0;

            /* Check for RUN here in direct mode -- needs run loop */
            parse_ptr = p;
            if (match_kw("RUN")) {
	                for (int i = 0; i < 26; i++) vars[i] = 0;
                gosub_sp = 0; for_sp = 0;
                run_pc = 0; running = 1;
            } else {
	                exec_stmt(p);
            }
        }
    }
}
