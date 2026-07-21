#ifndef GFX_H
#define GFX_H
#include <stdint.h>

/* VGA mode 13h: 320x200, 256 colors, linear framebuffer at 0xA0000 */
#define GFX_WIDTH 320
#define GFX_HEIGHT 200
#define GFX_FB_ADDR 0xA0000

int gfx_enter(void);
int gfx_leave(void);
int gfx_active(void);

void gfx_cls(uint8_t color);
void gfx_pset(int x, int y, uint8_t color);
uint8_t gfx_getpixel(int x, int y);
void gfx_line(int x0, int y0, int x1, int y1, uint8_t color);
void gfx_rect(int x0, int y0, int x1, int y1, uint8_t color, int fill);
void gfx_circle(int cx, int cy, int r, uint8_t color, int fill);
void gfx_paint(int x, int y, uint8_t color);

/* Palette control via VGA DAC ports (0x3C8/0x3C9), no BIOS needed. */
void gfx_set_palette(uint8_t index, uint8_t r, uint8_t g, uint8_t b);

#endif
