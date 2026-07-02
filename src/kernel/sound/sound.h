#ifndef SOUND_H
#define SOUND_H
#include <stdint.h>
void beep(uint32_t freq, uint32_t ms);
void speaker_on(uint32_t freq);
void speaker_off(void);
void say_string(const char *s);
#endif
