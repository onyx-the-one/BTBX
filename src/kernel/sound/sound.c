/* sound.c – PC speaker audio for BTBX
 * BEEP : PIT channel 2, mode 3 square wave.
 * SAY  : Formant/phoneme synth — SAM port (MIT licence, s-macke/SAM).
 * PWM  : 22 050 Hz, 1-bit, PIT ch0 polling.
 */
#include "sound.h"
#include <stdint.h>

static inline void outb(uint16_t p,uint8_t v){__asm__ __volatile__("outb %0,%1"::"a"(v),"Nd"(p));}
static inline uint8_t inb(uint16_t p){uint8_t v;__asm__ __volatile__("inb %1,%0":"=a"(v):"Nd"(p));return v;}
static int sam_abs(int x){return x<0?-x:x;}
static int sam_strlen(const char *s){int n=0;while(s[n])n++;return n;}
static void sam_memset(void *p,int c,int n){unsigned char *b=(unsigned char*)p;while(n--)*b++=(unsigned char)c;}

/* ── BEEP ─────────────────────────────────────────────────────────── */
static void delay_ms(uint32_t ms){
    while(ms--){
        uint32_t ticks=1193u;
        outb(0x43,0x00);
        uint16_t t0=(uint16_t)(inb(0x40)|((uint16_t)inb(0x40)<<8));
        while(ticks){outb(0x43,0x00);uint16_t t1=(uint16_t)(inb(0x40)|((uint16_t)inb(0x40)<<8));
            uint16_t diff=(uint16_t)(t0-t1);if(diff>=ticks)break;ticks-=diff;t0=t1;}
    }
}
void speaker_on(uint32_t freq){if(!freq)return;uint16_t div=(uint16_t)(1193182u/freq);
    outb(0x43,0xB6);outb(0x42,(uint8_t)div);outb(0x42,(uint8_t)(div>>8));outb(0x61,inb(0x61)|0x03u);}
void speaker_off(void){outb(0x61,inb(0x61)&~0x03u);}
void beep(uint32_t freq,uint32_t ms){speaker_on(freq);delay_ms(ms);speaker_off();}

/* ── SAM TABLES ───────────────────────────────────────────────────── */
static const uint8_t tab48426[5]={0x18,0x1A,0x17,0x17,0x17};
static const uint8_t tab47492[]={0,0,0xE0,0xE6,0xEC,0xF3,0xF9,0,6,0xC,6};
static const uint8_t amplitudeRescale[]={0,1,2,2,2,3,3,4,4,5,6,8,9,0xB,0xD,0xF,0};
static const uint8_t blendRank[]={
    0,0x1F,0x1F,0x1F,0x1F,2,2,2,2,2,2,2,2,2,5,5,2,0xA,2,8,5,5,0xB,0xA,9,8,8,0xA0,8,8,0x17,0x1F,
    0x12,0x12,0x12,0x12,0x1E,0x1E,0x14,0x14,0x14,0x14,0x17,0x17,0x1A,0x1A,0x1D,0x1D,
    2,2,2,2,2,2,0x1A,0x1D,0x1B,0x1A,0x1D,0x1B,0x1A,0x1D,0x1B,0x1A,
    0x1D,0x1B,0x17,0x1D,0x17,0x17,0x1D,0x17,0x17,0x1D,0x17,0x17,0x1D,0x17,0x17,0x17};
static const uint8_t outBlendLength[]={
    0,2,2,2,2,4,4,4,4,4,4,4,4,4,4,4,4,4,3,2,4,4,2,2,2,2,2,1,1,1,1,1,1,1,1,1,1,1,2,2,2,1,0,1,0,1,0,5,
    5,5,5,5,4,4,2,0,1,2,0,1,2,0,1,2,0,1,2,0,2,2,0,1,3,0,2,3,0,2,0xA0,0xA0};
static const uint8_t inBlendLength[]={
    0,2,2,2,2,4,4,4,4,4,4,4,4,4,4,4,4,4,3,3,4,4,3,3,3,3,3,1,2,3,2,1,3,3,3,3,1,1,3,3,3,2,2,3,2,3,0,0,
    5,5,5,5,4,4,2,0,2,2,0,3,2,0,4,2,0,3,2,0,2,2,0,2,3,0,3,3,0,3,0xB0,0xA0};
static const uint8_t sampledConsonantFlags[]={
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0xF1,0xE2,0xD3,0xBB,0x7C,0x95,1,2,3,3,0,0x72,0,2,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0x1B,0,0,0x19,0,0,0,0,0,0,0,0,0};
static uint8_t freq1data[]={
    0x00,0x13,0x13,0x13,0x13,0x0A,0x0E,0x12,0x18,0x1A,0x16,0x14,0x10,0x14,0x0E,0x12,
    0x0E,0x12,0x12,0x10,0x0C,0x0E,0x0A,0x12,0x0E,0x0A,0x08,0x06,0x06,0x06,0x06,0x11,
    0x06,0x06,0x06,0x06,0x0E,0x10,0x09,0x0A,0x08,0x0A,0x06,0x06,0x06,0x05,0x06,0x00,
    0x12,0x1A,0x14,0x1A,0x12,0x0C,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,
    0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x0A,0x0A,0x06,0x06,0x06,0x2C,0x13};
static uint8_t freq2data[]={
    0x00,0x43,0x43,0x43,0x43,0x54,0x48,0x42,0x3E,0x28,0x2C,0x1E,0x24,0x2C,0x48,0x30,
    0x24,0x1E,0x32,0x24,0x1C,0x44,0x18,0x32,0x1E,0x18,0x52,0x2E,0x36,0x56,0x36,0x43,
    0x49,0x4F,0x1A,0x42,0x49,0x25,0x33,0x42,0x28,0x2F,0x4F,0x4F,0x42,0x4F,0x6E,0x00,
    0x48,0x26,0x1E,0x2A,0x1E,0x22,0x1A,0x1A,0x1A,0x42,0x42,0x42,0x6E,0x6E,0x6E,0x54,
    0x54,0x54,0x1A,0x1A,0x1A,0x42,0x42,0x42,0x6D,0x56,0x6D,0x54,0x54,0x54,0x7F,0x7F};
static uint8_t freq3data[]={
    0x00,0x5B,0x5B,0x5B,0x5B,0x6E,0x5D,0x5B,0x58,0x59,0x57,0x58,0x52,0x59,0x5D,0x3E,
    0x52,0x58,0x3E,0x6E,0x50,0x5D,0x5A,0x3C,0x6E,0x5A,0x6E,0x51,0x79,0x65,0x79,0x5B,
    0x63,0x6A,0x51,0x79,0x5D,0x52,0x5D,0x67,0x4C,0x5D,0x65,0x65,0x79,0x65,0x79,0x00,
    0x5A,0x58,0x58,0x58,0x58,0x52,0x51,0x51,0x51,0x79,0x79,0x79,0x70,0x6E,0x6E,0x5E,
    0x5E,0x5E,0x51,0x51,0x51,0x79,0x79,0x79,0x65,0x65,0x70,0x5E,0x5E,0x5E,0x08,0x01};
static const uint8_t ampl1data[]={
    0,0,0,0,0,0xD,0xD,0xE,0xF,0xF,0xF,0xF,0xF,0xC,0xD,0xC,
    0xF,0xF,0xD,0xD,0xD,0xE,0xD,0xC,0xD,0xD,0xD,0xC,9,9,0,0,
    0,0,0,0,0,0,0xB,0xB,0xB,0xB,0,0,1,0xB,0,2,
    0xE,0xF,0xF,0xF,0xF,0xD,2,4,0,2,4,0,1,4,0,1,
    4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
static const uint8_t ampl2data[]={
    0,0,0,0,0,0xA,0xB,0xD,0xE,0xD,0xC,0xC,0xB,9,0xB,0xB,
    0xC,0xC,0xC,8,8,0xC,8,0xA,8,8,0xA,3,9,6,0,0,
    0,0,0,0,0,0,3,5,3,4,0,0,0,5,0xA,2,
    0xE,0xD,0xC,0xD,0xC,8,0,1,0,0,1,0,0,1,0,0,
    1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
static const uint8_t ampl3data[]={
    0,0,0,0,0,8,7,8,8,1,1,0,1,0,7,5,
    1,0,6,1,0,7,0,5,1,0,8,0,0,3,0,0,
    0,0,0,0,0,0,0,1,0,0,0,0,0,1,0xE,1,
    9,1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
static const uint8_t phonemeLengthTable[]={
    0,0,0,0,0,8,8,8,8,11,6,11,8,8,8,8,8,8,7,9,9,8,6,8,8,6,10,5,5,5,5,1,
    5,5,5,5,12,6,7,6,7,7,6,6,6,7,7,1,8,9,7,7,9,9,4,4,4,4,4,4,4,4,4,4,
    4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4};
static const uint8_t phonemeStressedLengthTable[]={
    0,0,0,0,0,13,14,14,15,15,13,14,13,13,14,13,12,12,14,10,14,10,11,9,9,7,13,7,7,7,7,2,
    7,7,7,7,15,10,10,9,10,10,10,9,8,10,8,2,12,13,11,11,13,12,7,7,7,7,7,7,7,7,7,7,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7};
static const int8_t sinus[256]={
    0,3,6,9,12,16,19,22,25,28,31,34,37,40,43,46,49,51,54,57,60,63,65,68,71,73,76,78,81,83,85,88,
    90,92,94,96,98,100,102,104,106,107,109,111,112,113,115,116,117,118,120,121,122,122,123,124,125,125,126,126,126,127,127,127,
    127,127,127,127,126,126,126,125,125,124,123,122,122,121,120,118,117,116,115,113,112,111,109,107,106,104,102,100,98,96,94,92,
    90,88,85,83,81,78,76,73,71,68,65,63,60,57,54,51,49,46,43,40,37,34,31,28,25,22,19,16,12,9,6,3,
    0,-3,-6,-9,-12,-16,-19,-22,-25,-28,-31,-34,-37,-40,-43,-46,-49,-51,-54,-57,-60,-63,-65,-68,-71,-73,-76,-78,-81,-83,-85,-88,
    -90,-92,-94,-96,-98,-100,-102,-104,-106,-107,-109,-111,-112,-113,-115,-116,-117,-118,-120,-121,-122,-122,-123,-124,-125,-125,-126,-126,-126,-127,-127,-127,
    -127,-127,-127,-127,-126,-126,-126,-125,-125,-124,-123,-122,-122,-121,-120,-118,-117,-116,-115,-113,-112,-111,-109,-107,-106,-104,-102,-100,-98,-96,-94,-92,
    -90,-88,-85,-83,-81,-78,-76,-73,-71,-68,-65,-63,-60,-57,-54,-51,-49,-46,-43,-40,-37,-34,-31,-28,-25,-22,-19,-16,-12,-9,-6,-3};
static const uint8_t rectangle[256]={
    0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,
    0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,
    0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,
    0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,
    0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,
    0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,
    0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,
    0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,
    0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,
    0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,
    0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,
    0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,
    0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,
    0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,
    0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,
    0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70};
static const uint8_t sampleTable[0x500]={
    0x38,0x84,0x6B,0x19,0xC6,0x63,0x18,0x86,0x73,0x98,0xC6,0xB1,0x1C,0xCA,0x31,0x8C,
    0xC7,0x31,0x88,0xC2,0x30,0x98,0x46,0x31,0x18,0xC6,0x35,0x0C,0xCA,0x31,0x0C,0xC6,
    0x21,0x10,0x24,0x69,0x12,0xC2,0x31,0x14,0xC4,0x71,0x08,0x4A,0x22,0x49,0xAB,0x6A,
    0xA8,0xAC,0x49,0x51,0x32,0xD5,0x52,0x88,0x93,0x6C,0x94,0x22,0x15,0x54,0xD2,0x25,
    0x96,0xD4,0x50,0xA5,0x46,0x21,0x08,0x85,0x6B,0x18,0xC4,0x63,0x10,0xCE,0x6B,0x18,
    0x8C,0x71,0x19,0x8C,0x63,0x35,0x0C,0xC6,0x33,0x99,0xCC,0x6C,0xB5,0x4E,0xA2,0x99,
    0x46,0x21,0x28,0x82,0x95,0x2E,0xE3,0x30,0x9C,0xC5,0x30,0x9C,0xA2,0xB1,0x9C,0x67,
    0x31,0x88,0x66,0x59,0x2C,0x53,0x18,0x84,0x67,0x50,0xCA,0xE3,0x0A,0xAC,0xAB,0x30,
    0xAC,0x62,0x30,0x8C,0x63,0x10,0x94,0x62,0xB1,0x8C,0x82,0x28,0x96,0x33,0x98,0xD6,
    0x4A,0x22,0xA6,0x46,0x25,0x59,0x0C,0xC6,0x31,0x0C,0xC6,0x31,0x8C,0x62,0x33,0x18,
    0xCC,0x63,0x10,0x94,0x62,0xB1,0x8C,0x82,0x28,0x96,0x33,0x98,0xD6,0x4A,0x22,0xA6,
    0x46,0x25,0x59,0x0C,0xC6,0x31,0x0C,0xC6,0x31,0x8C,0x62,0x33,0x18,0xCC,0x63,0x10,
    0x94,0x62,0xB1,0x8C,0x82,0x28,0x96,0x33,0x98,0xD6,0x4A,0x22,0xA6,0x46,0x25,0x59,
    0x0C,0xC6,0x31,0x0C,0xC6,0x31,0x8C,0x62,0x33,0x18,0xCC,0x63,0x10,0x94,0x62,0xB1,
    0x8C,0x82,0x28,0x96,0x33,0x98,0xD6,0x4A,0x22,0xA6,0x46,0x25,0x59,0x0C,0xC6,0x31,
    0x0C,0xC6,0x31,0x8C,0x62,0x33,0x18,0xCC,0x63,0x10,0x94,0x62,0xB1,0x8C,0x82,0x28,
    0x62,0x8C,0x31,0x22,0x63,0x18,0x8C,0xC3,0x30,0x8C,0xE1,0x1B,0x86,0x71,0x18,0x8C,
    0x22,0x33,0x18,0x8A,0xC5,0x3A,0xC4,0x23,0x08,0xA5,0xAA,0xA6,0xA4,0x26,0x25,0x96,
    0xB5,0x5A,0x25,0xD0,0x9C,0x52,0x25,0x84,0xCE,0x31,0x0D,0x34,0xD0,0xC2,0x34,0x54,
    0x25,0x85,0x2D,0x75,0xD2,0x97,0x45,0x25,0x28,0x92,0x4D,0x34,0xD1,0x34,0xD0,0xC2,
    0x38,0x87,0x10,0x24,0x69,0x72,0xC8,0xA4,0x26,0x4A,0xA5,0x25,0x5A,0x52,0xA5,0x25,
    0x52,0x9A,0x25,0x5A,0x52,0xA5,0x25,0x52,0x9A,0x4C,0xA9,0x25,0x52,0xA5,0x49,0x96,
    0xB2,0xA4,0xDA,0xA5,0x69,0x56,0xA9,0x25,0x4E,0x69,0x4A,0xAD,0xA6,0x96,0xBD,0x26,
    0x14,0x4A,0x41,0xA5,0x59,0xB6,0x85,0x69,0x56,0x14,0x4A,0x85,0x89,0xB6,0x85,0x69,
    0x56,0x14,0x4A,0x85,0x89,0xB6,0x85,0x69,0x56,0x14,0x4A,0x85,0x89,0xB6,0x85,0x69,
    0x56,0x14,0x4A,0x85,0x89,0xB6,0x85,0x69,0x56,0x14,0x4A,0x85,0x89,0xB6,0x85,0x69,
    0x56,0x14,0x4A,0x85,0x89,0xB6,0x85,0x69,0x56,0x14,0x4A,0x85,0x89,0xB6,0x85,0x69,
    0x56,0x14,0x4A,0x85,0x89,0xB6,0x85,0x69,0x56,0x14,0x4A,0x85,0x89,0xB6,0x85,0x69,
    0x56,0x14,0x4A,0x85,0x89,0xB6,0x85,0x69,0x56,0x14,0x4A,0x85,0x89,0xB6,0x85,0x69,
    0x56,0x14,0x4A,0x85,0x89,0xB6,0x85,0x69,0x56,0x14,0x4A,0x85,0x89,0xB6,0x85,0x69,
    0x56,0x14,0x4A,0x85,0x89,0xB6,0x85,0x69,0x56,0x14,0x4A,0x85,0x89,0xB6,0x85,0x69,
    0x56,0x14,0x4A,0x85,0x89,0xB6,0x85,0x69,0x56,0x14,0x4A,0x85,0x89,0xB6,0x85,0x69};
static const uint8_t stressInputTable[]={'*','1','2','3','4','5','6','7','8'};
static const uint8_t signInputTable1[]={
    ' ','.','?',',','-','I','I','E','A','A','A','A','U','A','I','E',
    'U','O','R','L','W','Y','W','R','L','W','Y','M','N','N','D','Q',
    'S','S','F','T','/','/','/','Z','Z','V','D','C','*','J','*','*','*',
    'E','A','O','A','O','U','B','*','*','D','*','*','G','*','*','G',
    '*','*','P','*','*','T','*','*','K','*','*','K','*','*','U','U','U'};
static const uint8_t signInputTable2[]={
    '*','*','*','*','*','Y','H','H','E','A','H','O','H','X','X','R',
    'X','H','X','X','X','X','H','*','*','*','*','*','*','X','X','*',
    '*','H','*','H','H','X','X','H','*','H','H','*','*','*','*','*',
    'Y','Y','Y','W','W','W','*','*','*','*','*','*','*','*','*','X',
    '*','*','*','*','*','*','*','*','*','*','*','X','*','*','L','M','N'};
static const uint8_t flags[]={
    0x00,0x00,0x00,0x00,0x00,0xA4,0xA4,0xA4,0xA4,0xA4,0xA4,0x84,0x84,0xA4,0xA4,0x84,
    0x84,0x84,0x84,0x84,0x84,0x84,0x44,0x44,0x44,0x44,0x44,0x4C,0x4C,0x4C,0x48,0x4C,
    0x40,0x40,0x40,0x40,0x40,0x40,0x44,0x44,0x44,0x44,0x48,0x40,0x4C,0x44,0x00,0x00,
    0xB4,0xB4,0xB4,0x94,0x94,0x94,0x4E,0x4E,0x4E,0x4E,0x4E,0x4E,0x4E,0x4E,0x4E,0x4E,
    0x4E,0x4E,0x4B,0x4B,0x4B,0x4B,0x4B,0x4B,0x4B,0x4B,0x4B,0x4B,0x4B,0x4B,0x80,0xC1,0xC1};
static const uint8_t flags2[]={
    0x00,0x02,0x02,0x02,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x10,0x10,0x10,0x10,
    0x08,0x04,0x08,0x08,0x00,0x04,0x08,0x04,0x08,0x04,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,
    0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x00,0x00,0x00,0x00,0x00};

/* ── SAM STATE ───────────────────────────────────────────────────── */
#define SAM_BUFSIZE (22050*5)
static char sam_rawbuf[SAM_BUFSIZE];
static char *buffer=sam_rawbuf;
static int bufferpos=0;
static char input[256];
static uint8_t speed=72,pitch=64,mouth=128,throat=128;
static int singmode=0;
static uint8_t mem39,mem44,mem47,mem49,mem50,mem51,mem53,mem56;
static uint8_t mem59=0;
static uint8_t A,X,Y;
static uint8_t stress[256],phonemeLength[256],phonemeindex[256];
static uint8_t phonemeIndexOutput[60],stressOutput[60],phonemeLengthOutput[60];
static uint8_t pitches[256];
static uint8_t frequency1[256],frequency2[256],frequency3[256];
static uint8_t amplitude1[256],amplitude2[256],amplitude3[256];
static uint8_t sampledConsonantFlag[256];
static int timetable[5][5]={{162,167,167,127,128},{226,60,60,0,0},{225,60,59,0,0},{200,0,0,54,55},{199,0,0,54,54}};
static unsigned int oldtimetableindex=0;

static void Output8BitAry(int index,uint8_t ary[5]){
    int k; bufferpos+=timetable[oldtimetableindex][index]; oldtimetableindex=(unsigned int)index;
    for(k=0;k<5;k++){int pos=bufferpos/50+k;if(pos>=0&&pos<SAM_BUFSIZE)buffer[pos]=(char)ary[k];}
}
static void Output8Bit(int index,uint8_t val){uint8_t ary[5]={val,val,val,val,val};Output8BitAry(index,ary);}
static uint8_t ReadTable(uint8_t p,uint8_t yi){
    switch(p){case 168:return pitches[yi];case 169:return frequency1[yi];case 170:return frequency2[yi];
    case 171:return frequency3[yi];case 172:return amplitude1[yi];case 173:return amplitude2[yi];case 174:return amplitude3[yi];}return 0;}
static void WriteTable(uint8_t p,uint8_t yi,uint8_t v){
    switch(p){case 168:pitches[yi]=v;return;case 169:frequency1[yi]=v;return;case 170:frequency2[yi]=v;return;
    case 171:frequency3[yi]=v;return;case 172:amplitude1[yi]=v;return;case 173:amplitude2[yi]=v;return;case 174:amplitude3[yi]=v;return;}}
static uint8_t trans(uint8_t a,uint8_t b);
static void AddInflection(uint8_t mem48,uint8_t phase1);
static void Render(void);

static void RenderSample(uint8_t *mem66){
    int tempA; mem49=Y; A=mem39&7; X=A-1; mem56=X; mem53=tab48426[X]; mem47=X;
    A=mem39&248;
    if(A==0){
        Y=mem49; A=pitches[mem49]>>4;
        uint8_t phase1=A^255; Y=*mem66;
        do{ mem56=8; A=sampleTable[mem47*256+Y];
            do{ tempA=A; A=(uint8_t)(A<<1);
                if((tempA&128)!=0){X=26;Output8Bit(3,(uint8_t)((X&0xf)*16));}
                else{X=6;Output8Bit(4,(uint8_t)((X&0xf)*16));}
                mem56--;
            }while(mem56!=0);
            Y++; phase1++;
        }while(phase1!=0);
        A=1; mem44=1; *mem66=Y; Y=mem49; return;
    }
    Y=A^255;
    do{ mem56=8; A=sampleTable[mem47*256+Y];
        do{ tempA=A; A=(uint8_t)(A<<1);
            if((tempA&128)==0){X=mem53;Output8Bit(1,(uint8_t)((X&0x0f)*16));if(X!=0)goto pos48296;}
            Output8Bit(2,(uint8_t)(5*16));
            pos48296: X=0; mem56--;
        }while(mem56!=0);
        Y++;
    }while(Y!=0);
    mem44=1; Y=mem49;
}

static void Render(void){
    uint8_t phase1=0,phase2=0,phase3=0,mem66=0,mem38=0,mem40=0,speedcounter=0,mem48=0;
    int i; if(phonemeIndexOutput[0]==255)return;
    A=0;X=0;mem44=0;
    do{
        Y=mem44; A=phonemeIndexOutput[mem44]; mem56=A;
        if(A==255)break;
        if(A==1){A=1;mem48=1;AddInflection(mem48,phase1);}
        if(A==2){mem48=255;AddInflection(mem48,phase1);}
        phase1=tab47492[stressOutput[Y]+1]; phase2=phonemeLengthOutput[Y]; Y=mem56;
        do{frequency1[X]=freq1data[Y];frequency2[X]=freq2data[Y];frequency3[X]=freq3data[Y];
            amplitude1[X]=ampl1data[Y];amplitude2[X]=ampl2data[Y];amplitude3[X]=ampl3data[Y];
            sampledConsonantFlag[X]=sampledConsonantFlags[Y];pitches[X]=pitch+phase1;X++;phase2--;}while(phase2!=0);
        mem44++;
    }while(mem44!=0);
    A=0;mem44=0;mem49=0;X=0;
    while(1){
        Y=phonemeIndexOutput[X]; A=phonemeIndexOutput[X+1]; X++; if(A==255)break;
        X=A; mem56=blendRank[A]; A=blendRank[Y];
        if(A==mem56){phase1=outBlendLength[Y];phase2=outBlendLength[X];}
        else if(A<mem56){phase1=inBlendLength[X];phase2=outBlendLength[X];}
        else{phase1=outBlendLength[Y];phase2=inBlendLength[Y];}
        Y=mem44; A=mem49+phonemeLengthOutput[mem44]; mem49=A;
        A=A+phase2; speedcounter=A; mem47=168; phase3=mem49-phase1;
        A=phase1+phase2; mem38=A; X=A; X-=2;
        if((X&128)==0){
            do{
                mem40=mem38;
                if(mem47==168){
                    uint8_t mem36,mem37;
                    mem36=phonemeLengthOutput[mem44]>>1; mem37=phonemeLengthOutput[mem44+1]>>1;
                    mem40=mem36+mem37; mem37+=mem49; mem36=mem49-mem36;
                    A=ReadTable(mem47,mem37); Y=mem36; mem53=A-ReadTable(mem47,mem36);
                }else{
                    A=ReadTable(mem47,speedcounter); Y=phase3; mem53=A-ReadTable(mem47,phase3);}
                int8_t m53=(int8_t)mem53;
                mem50=mem53&128; uint8_t m53abs=(uint8_t)sam_abs((int)m53);
                mem51=m53abs%mem40; mem53=(uint8_t)((int8_t)m53/(int8_t)(int)mem40);
                X=mem40; Y=phase3; mem56=0;
                while(1){
                    A=ReadTable(mem47,Y)+mem53; mem48=A; Y++;X--;if(X==0)break;
                    mem56+=mem51;
                    if(mem56>=mem40){mem56-=mem40;if((mem50&128)==0){if(mem48!=0)mem48++;}else mem48--;}
                    WriteTable(mem47,Y,mem48);}
                mem47++;
            }while(mem47!=175);
            mem44++; X=mem44; mem48=mem49+phonemeLengthOutput[mem44];
        }
    }
    if(!singmode) for(i=0;i<256;i++) pitches[i]-=(frequency1[i]>>1);
    phase1=0;phase2=0;phase3=0;mem49=0;speedcounter=72;
    for(i=255;i>=0;i--){
        amplitude1[i]=amplitudeRescale[amplitude1[i]];
        amplitude2[i]=amplitudeRescale[amplitude2[i]];
        amplitude3[i]=amplitudeRescale[amplitude3[i]];}
    Y=0;A=pitches[0];mem44=A;X=A;mem38=A-(A>>2);
    while(1){
        A=sampledConsonantFlag[Y]; mem39=A; A=A&248;
        if(A!=0){RenderSample(&mem66);Y+=2;mem48-=2;}
        else{
            uint8_t ary[5]; unsigned int p1=phase1*256u,p2=phase2*256u,p3=phase3*256u;
            int k;
            for(k=0;k<5;k++){
                int8_t sp1=sinus[0xff&(p1>>8)],sp2=sinus[0xff&(p2>>8)];
                int8_t rp3=(int8_t)rectangle[0xff&(p3>>8)];
                int sin1=sp1*(int)((uint8_t)amplitude1[Y]&0x0f);
                int sin2=sp2*(int)((uint8_t)amplitude2[Y]&0x0f);
                int rect=rp3*(int)((uint8_t)amplitude3[Y]&0x0f);
                int mux=(sin1+sin2+rect)/32+128; ary[k]=(uint8_t)mux;
                p1+=frequency1[Y]*256u/4u;p2+=frequency2[Y]*256u/4u;p3+=frequency3[Y]*256u/4u;}
            Output8BitAry(0,ary); speedcounter--; if(speedcounter!=0)goto pos48155;
            Y++;mem48--; if(mem48==0)return; speedcounter=speed;
            pos48155: mem44--;
            if(mem44==0){
                pos48159: A=pitches[Y]; mem44=A; A=A-(A>>2); mem38=A;
                phase1=0;phase2=0;phase3=0; continue;}
            mem38--; if((mem38!=0)||(mem39==0)){phase1+=frequency1[Y];phase2+=frequency2[Y];phase3+=frequency3[Y];continue;}
            RenderSample(&mem66); goto pos48159;}
    }
}

static void AddInflection(uint8_t mem48,uint8_t phase1){
    uint8_t saveX; mem49=X; A=X;
    int Atemp=(int)(unsigned int)A; A=(uint8_t)(Atemp>30?Atemp-30:0); X=A;
    while((A=pitches[X])==127)X++;
    for(;;){A+=mem48;phase1=A;pitches[X]=A;saveX=X;X++;if(X==mem49)return;if(pitches[X]!=255){A=phase1;}}
    (void)saveX;
}
static uint8_t trans(uint8_t a,uint8_t b){
    uint8_t carry,result=0; int temp; X=8;
    do{carry=a&1;a>>=1;if(carry){carry=0;temp=(int)result+(int)b;result=(uint8_t)(result+b);if(temp>255)carry=1;}
       temp=result&1;result=(result>>1)|(carry?128:0);carry=(uint8_t)temp;X--;}while(X!=0);
    return result;
}
static void SetMouthThroat(uint8_t m,uint8_t t){
    static const uint8_t mf5_29[30]={0,0,0,0,0,10,14,19,24,27,23,21,16,20,14,18,14,18,18,16,13,15,11,18,14,11,9,6,6,6};
    static const uint8_t tf5_29[30]={255,255,255,255,255,84,73,67,63,40,44,31,37,45,73,49,36,30,51,37,29,69,24,50,30,24,83,46,54,86};
    static const uint8_t mf48_53[6]={19,27,21,27,18,13};
    static const uint8_t tf48_53[6]={72,39,31,43,30,34};
    uint8_t pos=5,nf=0,init;
    while(pos!=30){init=mf5_29[pos];if(init)nf=trans(m,init);freq1data[pos]=nf;
        init=tf5_29[pos];if(init)nf=trans(t,init);freq2data[pos]=nf;pos++;}
    Y=0;pos=48;
    while(pos!=54){freq1data[pos]=trans(m,mf48_53[Y]);freq2data[pos]=trans(t,tf48_53[Y]);Y++;pos++;}
}

/* ── SAM.C phoneme parser ────────────────────────────────────────── */
static void Insert(uint8_t pos,uint8_t p60,uint8_t p59,uint8_t p58){
    int i;for(i=253;i>=(int)pos;i--){phonemeindex[i+1]=phonemeindex[i];phonemeLength[i+1]=phonemeLength[i];stress[i+1]=stress[i];}
    phonemeindex[pos]=p60;phonemeLength[pos]=p59;stress[pos]=p58;}
static void InsertBreath(void){
    uint8_t mem54=255,mem55=0,index,mem66=0; X++;
    while(1){X=mem66;index=phonemeindex[X];if(index==255)return;
        mem55+=phonemeLength[X];
        if(mem55<232){
            if(index!=254){A=flags2[index]&1;if(A){X++;mem55=0;Insert(X,254,mem59,0);mem66++;mem66++;continue;}}
            if(index==0)mem54=X; mem66++;continue;}
        X=mem54;phonemeindex[X]=31;phonemeLength[X]=4;stress[X]=0;X++;mem55=0;Insert(X,254,mem59,0);X++;mem66=X;}
}
static void CopyStress(void){
    uint8_t pos=0;
    while(1){Y=phonemeindex[pos];if(Y==255)return;if((flags[Y]&64)==0){pos++;continue;}
        Y=phonemeindex[pos+1];if(Y==255){pos++;continue;}if((flags[Y]&128)==0){pos++;continue;}
        Y=stress[pos+1];if(Y==0){pos++;continue;}if((Y&128)!=0){pos++;continue;}
        stress[pos]=Y+1;pos++;}
}
static void SetPhonemeLength(void){
    int position=0;
    while(phonemeindex[position]!=255){
        uint8_t av=stress[position];
        phonemeLength[position]=((av==0)||(av&128))?phonemeLengthTable[phonemeindex[position]]:phonemeStressedLengthTable[phonemeindex[position]];
        position++;}
}
static void Code41240(void){
    uint8_t pos=0;
    while(phonemeindex[pos]!=255){
        uint8_t index=phonemeindex[pos]; X=pos;
        if((flags[index]&2)==0){pos++;continue;}
        if((flags[index]&1)==0){Insert(pos+1,index+1,phonemeLengthTable[index+1],stress[pos]);Insert(pos+2,index+2,phonemeLengthTable[index+2],stress[pos]);pos+=3;continue;}
        do{X++;A=phonemeindex[X];}while(A==0);
        if(A!=255&&!((flags[A]&8)!=0)&&A!=36&&A!=37){Insert(pos+1,index+1,phonemeLengthTable[index+1],stress[pos]);Insert(pos+2,index+2,phonemeLengthTable[index+2],stress[pos]);pos+=3;continue;}
        pos++;}
}
static void AdjustLengths(void){
    uint8_t index,loopIndex; X=0;loopIndex=0;
    while(1){index=phonemeindex[X];if(index==255)break;if((flags2[index]&1)==0){X++;continue;}
        loopIndex=X;
        for(;;){if(X==0)break;X--;index=phonemeindex[X];if(index!=255&&(flags[index]&128)!=0)break;}
        do{index=phonemeindex[X];
            if(index!=255)if(((flags2[index]&32)==0)||((flags[index]&4)!=0)){A=phonemeLength[X];A=(A>>1)+A+1;phonemeLength[X]=A;}
            X++;}while(X!=loopIndex);
        X++;}
    loopIndex=0;
    while(1){
        X=loopIndex;index=phonemeindex[X];if(index==255)return;
        if((flags[index]&128)!=0){
            X++;index=phonemeindex[X];uint8_t mem56v=(index==255)?65:flags[index];
            if((mem56v&64)==0){
                if(index==18||index==19){X++;index=phonemeindex[X];if((flags[index]&64)!=0)phonemeLength[loopIndex]--;}
                loopIndex++;continue;}
            if((mem56v&4)==0){
                if((mem56v&1)==0){loopIndex++;continue;}
                X--;mem56v=phonemeLength[X]>>3;phonemeLength[X]-=mem56v;loopIndex++;continue;}
            A=phonemeLength[X-1];phonemeLength[X-1]=(A>>2)+A+1;loopIndex++;continue;}
        if((flags2[index]&8)!=0){
            X++;index=phonemeindex[X];
            uint8_t av2=(index==255)?65&2:flags[index]&2;
            if(av2){phonemeLength[X]=6;phonemeLength[X-1]=5;loopIndex++;continue;}}
        if((flags[index]&2)!=0){
            do{X++;index=phonemeindex[X];}while(index==0);
            if(index==255){loopIndex++;continue;}if((flags[index]&2)==0){loopIndex++;continue;}
            phonemeLength[X]=(phonemeLength[X]>>1)+1;phonemeLength[X-1]=(phonemeLength[X-1]>>1)+1;loopIndex++;continue;}
        if((flags2[index]&16)!=0){index=phonemeindex[X-1];if((flags[index]&2)!=0)phonemeLength[X]-=2;loopIndex++;continue;}
        loopIndex++;}
}
static void Parser2(void){
    uint8_t pos=0,mem58=0;
    while(1){
        X=pos;A=phonemeindex[pos];if(A==0){pos++;continue;}if(A==255)return;
        Y=A;
        if((flags[A]&16)==0)goto pos41457;
        mem58=stress[pos];A=(flags[Y]&32)?21:20;Insert(pos+1,A,mem59,mem58);X=pos;goto pos41749;
        pos41457: A=phonemeindex[X];
        if(A==78){A=24;goto pos41466;}if(A==79){A=27;goto pos41466;}if(A==80){A=28;goto pos41466;}goto pos41503;
        pos41466: mem58=stress[X];phonemeindex[X]=13;Insert(X+1,A,mem59,mem58);pos++;continue;
        pos41503: Y=A;A=flags[A]&128;
        if(A!=0){A=stress[X];if(A!=0){X++;A=phonemeindex[X];
            if(A==0){X++;Y=phonemeindex[X];A=(Y==255)?(65&128):(flags[Y]&128);}
            if(A!=0){A=stress[X];if(A!=0){Insert(X,31,mem59,0);pos++;continue;}}}}
        X=pos;A=phonemeindex[pos];
        if(A==23){X--;A=phonemeindex[pos-1];if(A==69){phonemeindex[pos-1]=42;goto pos41779;}
            if(A==57){phonemeindex[pos-1]=44;goto pos41788;}if((flags[A]&128)!=0)phonemeindex[pos]=18;pos++;continue;}
        if(A==24){if((flags[phonemeindex[pos-1]]&128)==0){pos++;continue;}phonemeindex[X]=19;pos++;continue;}
        if(A==32){if(phonemeindex[pos-1]!=60){pos++;continue;}phonemeindex[pos]=38;pos++;continue;}
        if(A==72){Y=phonemeindex[pos+1];if(Y==255)phonemeindex[pos]=75;else if((flags[Y]&32)==0)phonemeindex[pos]=75;}
        else if(A==60){uint8_t idx2=phonemeindex[pos+1];if(idx2==255){pos++;continue;}if((flags[idx2]&32)!=0){pos++;continue;}phonemeindex[pos]=63;pos++;continue;}
        Y=phonemeindex[pos];
        A=flags[Y]&1;if(A==0)goto pos41749;
        A=phonemeindex[pos-1];if(A!=32){A=Y;goto pos41812;}
        phonemeindex[pos]=Y-12;pos++;continue;
        pos41749: A=phonemeindex[X];
        if(A==53){Y=phonemeindex[X-1];if((flags2[Y]&4)==0){pos++;continue;}phonemeindex[X]=16;pos++;continue;}
        pos41779: if(A==42){Insert(X+1,A+1,mem59,stress[X]);pos++;continue;}
        pos41788: if(A==44){Insert(X+1,A+1,mem59,stress[X]);pos++;continue;}
        pos41812: if(A!=69&&A!=57){pos++;continue;}
        if((flags[phonemeindex[X-1]]&128)==0){pos++;continue;}
        X++;A=phonemeindex[X];
        if(A!=0){if((flags[A]&128)==0){pos++;continue;}if(stress[X]!=0){pos++;continue;}phonemeindex[pos]=30;}
        else{A=phonemeindex[X+1];A=(A==255)?(65&128):(flags[A]&128);if(A!=0)phonemeindex[pos]=30;}
        pos++;}
}
static int Parser1(void){
    int i; uint8_t sign1,sign2,position=0; X=0;A=0;Y=0;
    for(i=0;i<256;i++)stress[i]=0;
    while(1){
        sign1=(uint8_t)input[X];if(sign1==155){phonemeindex[position]=255;return 1;}
        X++;sign2=(uint8_t)input[X];Y=0;
        pos41095: A=signInputTable1[Y];
        if(A==sign1){A=signInputTable2[Y];if(A!='*'&&A==sign2){phonemeindex[position]=Y;position++;X++;continue;}}
        Y++;if(Y!=81)goto pos41095;
        Y=0;
        pos41134: if(signInputTable2[Y]=='*'&&signInputTable1[Y]==sign1){phonemeindex[position]=Y;position++;continue;}
        Y++;if(Y!=81)goto pos41134;
        Y=8;while((sign1!=stressInputTable[Y])&&(Y>0))Y--;
        if(Y==0)return 0;stress[position-1]=Y;}
}
static void PrepareOutput(void){
    A=0;X=0;Y=0;
    while(1){
        A=phonemeindex[X];
        if(A==255){phonemeIndexOutput[Y]=255;Render();return;}
        if(A==254){X++;{int t=X;phonemeIndexOutput[Y]=255;Render();X=t;Y=0;continue;}}
        if(A==0){X++;continue;}
        phonemeIndexOutput[Y]=A;phonemeLengthOutput[Y]=phonemeLength[X];stressOutput[Y]=stress[X];X++;Y++;}
}
static void Init(void){
    int i; SetMouthThroat(mouth,throat);
    for(i=0;i<256;i++){stress[i]=0;phonemeLength[i]=0;}
    for(i=0;i<60;i++){phonemeIndexOutput[i]=0;stressOutput[i]=0;phonemeLengthOutput[i]=0;}
    phonemeindex[255]=255;
}
static void SetInput(const char *s){
    int i,l=sam_strlen(s);if(l>254)l=254;
    for(i=0;i<l;i++)input[i]=s[i];input[l]=155;
}
static int SAMMain(void){
    int pos=0; Init(); if(!Parser1())return 0;
    CopyStress();SetPhonemeLength();AdjustLengths();Code41240();
    do{if(phonemeindex[pos]==255)break;if(phonemeindex[pos]>80){phonemeindex[pos]=255;break;}pos++;}while(pos!=0);
    InsertBreath();PrepareOutput();return 1;
}

/* ── Letter phoneme strings ──────────────────────────────────────── */
static const char * const letter_phoneme[36]={
    "EY \x9b","BIY \x9b","SIY \x9b","DIY \x9b","IY \x9b","EHFF \x9b","JIIY \x9b","EYCH \x9b",
    "AY \x9b","JIEY \x9b","KIEY \x9b","EHLL \x9b","EHMM \x9b","EHNN \x9b","OW \x9b","PIY \x9b",
    "KYUW \x9b","AAR \x9b","EHSS \x9b","TIY \x9b","YUW \x9b","VIY \x9b","DAHBLLIYUW \x9b",
    "EHKSS \x9b","WAY \x9b","ZIIY \x9b","ZIYROH \x9b","WAHN \x9b","TUW \x9b","THRIY \x9b",
    "FOR \x9b","FAYV \x9b","SIHKS \x9b","SEHVAHN \x9b","EYT \x9b","NAYN \x9b"};

/* ── 1-bit PWM playback ───────────────────────────────────────────── */
#define TICKS_PER_SAMPLE 54u
static void play_buffer(void){
    int n=bufferpos/50,i; uint32_t eflags; uint16_t t0,t1; uint8_t port61;
    if(n<=0)return;
    __asm__ __volatile__("pushfl; popl %0; cli":"=r"(eflags));
    port61=inb(0x61)&~0x02u;
    for(i=0;i<n;i++){
        uint8_t sample=(uint8_t)buffer[i];
        uint16_t duty=(uint16_t)(((uint32_t)sample*TICKS_PER_SAMPLE)>>8);
        if(duty==0)duty=1; uint16_t rest=(uint16_t)(TICKS_PER_SAMPLE-duty);
        outb(0x61,port61|0x02u);outb(0x43,0x00);
        t0=(uint16_t)(inb(0x40)|((uint16_t)inb(0x40)<<8));
        for(;;){outb(0x43,0x00);t1=(uint16_t)(inb(0x40)|((uint16_t)inb(0x40)<<8));if((uint16_t)(t0-t1)>=duty)break;}
        outb(0x61,port61&~0x02u);outb(0x43,0x00);
        t0=(uint16_t)(inb(0x40)|((uint16_t)inb(0x40)<<8));
        for(;;){outb(0x43,0x00);t1=(uint16_t)(inb(0x40)|((uint16_t)inb(0x40)<<8));if((uint16_t)(t0-t1)>=rest)break;}
    }
    outb(0x61,port61&~0x02u);
    __asm__ __volatile__("pushl %0; popfl"::"r"(eflags));
}

/* ── PUBLIC API ──────────────────────────────────────────────────── */
void say_string(const char *s){
    if(!s||!*s)return;
    bufferpos=0;oldtimetableindex=0;sam_memset(sam_rawbuf,128,SAM_BUFSIZE);
    while(*s){
        char c=*s++; int idx=-1;
        if(c>='A'&&c<='Z') idx=c-'A';
        else if(c>='a'&&c<='z') idx=c-'a';
        else if(c>='0'&&c<='9') idx=26+(c-'0');
        if(idx>=0){SetInput(letter_phoneme[idx]);SAMMain();}
        else if(c==' '||c==','){
            int gap=(int)(22050u*80u/1000u);bufferpos+=50*gap;
            if(bufferpos/50>=SAM_BUFSIZE)bufferpos=(SAM_BUFSIZE-1)*50;}
    }
    if(bufferpos>0)play_buffer();
}
