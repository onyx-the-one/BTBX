/* sound.c - PC speaker beep support for BTBX */

#include "sound.h"
#include <stdint.h>

static inline void outb(uint16_t p, uint8_t v) {
    __asm__ __volatile__("outb %0,%1" :: "a"(v), "Nd"(p));
}

static inline uint8_t inb(uint16_t p) {
    uint8_t v;
    __asm__ __volatile__("inb %1,%0" : "=a"(v) : "Nd"(p));
    return v;
}

static void delay_ms(uint32_t ms) {
    while (ms--) {
        uint32_t ticks = 1193u;
        outb(0x43, 0x00);
        uint16_t t0 = (uint16_t)(inb(0x40) | ((uint16_t)inb(0x40) << 8));
        while (ticks) {
            outb(0x43, 0x00);
            uint16_t t1 = (uint16_t)(inb(0x40) | ((uint16_t)inb(0x40) << 8));
            uint16_t diff = (uint16_t)(t0 - t1);
            if (diff >= ticks) break;
            ticks -= diff;
            t0 = t1;
        }
    }
}

void speaker_on(uint32_t freq) {
    if (!freq) return;
    uint16_t div = (uint16_t)(1193182u / freq);
    outb(0x43, 0xB6);
    outb(0x42, (uint8_t)div);
    outb(0x42, (uint8_t)(div >> 8));
    outb(0x61, inb(0x61) | 0x03u);
}

void speaker_off(void) {
    outb(0x61, inb(0x61) & (uint8_t)~0x03u);
}

void beep(uint32_t freq, uint32_t ms) {
    speaker_on(freq);
    delay_ms(ms);
    speaker_off();
}
