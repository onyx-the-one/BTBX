#ifndef BASIC_H
#define BASIC_H

#include <stdint.h>

/* VGA colour constants */
#define VGA_BLACK        0
#define VGA_BLUE         1
#define VGA_GREEN        2
#define VGA_CYAN         3
#define VGA_RED          4
#define VGA_MAGENTA      5
#define VGA_BROWN        6
#define VGA_LIGHT_GREY   7
#define VGA_DARK_GREY    8
#define VGA_LIGHT_BLUE   9
#define VGA_LIGHT_GREEN  10
#define VGA_LIGHT_CYAN   11
#define VGA_LIGHT_RED    12
#define VGA_LIGHT_MAGENTA 13
#define VGA_YELLOW       14
#define VGA_WHITE        15

/* Terminal API — implemented in kernel.c */
void term_set_color(uint8_t fg, uint8_t bg);
void term_clear(void);
void term_putchar(char c);
void term_puts(const char *s);
void term_puti(int32_t n);
void term_putf(double f);
int  term_getchar(void);
void term_sync_cursor(void);
void term_get_line(char *buf, int max);

/* Kernel services */
void draw_banner(void);
void kpanic(const char *msg);   /* print panic message and halt */

/* BASIC interpreter entry point */
void basic_run(void);

#endif /* BASIC_H */
