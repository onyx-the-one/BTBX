#ifndef SOUND_H
#define SOUND_H

#include <stdint.h>

/* Beep at freq Hz for ms milliseconds. */
void beep(uint32_t freq, uint32_t ms);

/* Enable/disable speaker tone (also usable from SYS-loaded code). */
void speaker_on(uint32_t freq);
void speaker_off(void);

#endif /* SOUND_H */
