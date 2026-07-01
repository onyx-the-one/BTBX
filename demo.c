/* demo.c - SYS demo for BTBX: draws a blinking colour border then returns */

#define VGA  ((volatile unsigned short *)0xB8000)
#define COLS 80
#define ROWS 25

static void delay(void) {
    for (volatile int i = 0; i < 2000000; i++) {}
}

void _start(void) {
    unsigned char colors[] = { 0x1F, 0x2F, 0x4F, 0x6F, 0x1F };

    for (int c = 0; c < 5; c++) {
        unsigned short attr = (unsigned short)colors[c] << 8 | ' ';

        /* top and bottom rows */
        for (int x = 0; x < COLS; x++) {
            VGA[x]              = attr;
            VGA[(ROWS-1)*COLS+x] = attr;
        }
        /* left and right columns */
        for (int y = 0; y < ROWS; y++) {
            VGA[y*COLS]        = attr;
            VGA[y*COLS+COLS-1] = attr;
        }
        delay();
    }
    /* clear border */
    for (int x = 0; x < COLS; x++) {
        VGA[x]              = 0x0700 | ' ';
        VGA[(ROWS-1)*COLS+x] = 0x0700 | ' ';
    }
    for (int y = 0; y < ROWS; y++) {
        VGA[y*COLS]        = 0x0700 | ' ';
        VGA[y*COLS+COLS-1] = 0x0700 | ' ';
    }
    /* plain ret — returns to BASIC */
}
