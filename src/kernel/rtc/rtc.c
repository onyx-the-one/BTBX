#include "rtc.h"
#include <stdint.h>

static inline void outb(uint16_t p,uint8_t v){__asm__ __volatile__("outb %0,%1"::"a"(v),"Nd"(p));}
static inline uint8_t inb(uint16_t p){uint8_t v;__asm__ __volatile__("inb %1,%0":"=a"(v):"Nd"(p));return v;}

typedef struct{
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint8_t year;
} RawRtc;

static uint8_t rtc_reg(uint8_t reg){outb(0x70,reg);return inb(0x71);}
static int rtc_updating(void){return (rtc_reg(0x0A)&0x80)!=0;}
static uint8_t bcd2bin(uint8_t v){return (uint8_t)((v&0x0F)+((v>>4)*10));}

static void rtc_read_raw(RawRtc *r){
    r->second=rtc_reg(0x00);
    r->minute=rtc_reg(0x02);
    r->hour=rtc_reg(0x04);
    r->day=rtc_reg(0x07);
    r->month=rtc_reg(0x08);
    r->year=rtc_reg(0x09);
}

static int rtc_same(const RawRtc *a,const RawRtc *b){
    return a->second==b->second&&a->minute==b->minute&&a->hour==b->hour&&a->day==b->day&&a->month==b->month&&a->year==b->year;
}

static void put2(uint8_t v,char *out){out[0]=(char)('0'+(v/10)%10);out[1]=(char)('0'+(v%10));}
static void put4(uint16_t v,char *out){out[0]=(char)('0'+(v/1000)%10);out[1]=(char)('0'+(v/100)%10);out[2]=(char)('0'+(v/10)%10);out[3]=(char)('0'+(v%10));}

int rtc_read(RtcTime *t){
    if(!t)return -1;
    RawRtc a,b;
    int ok=0;
    for(int tries=0;tries<8;tries++){
        while(rtc_updating()){}
        rtc_read_raw(&a);
        while(rtc_updating()){}
        rtc_read_raw(&b);
        if(rtc_same(&a,&b)){ok=1;break;}
    }
    if(!ok)b=a;

    uint8_t regb=rtc_reg(0x0B);
    uint8_t sec=b.second,min=b.minute,hour=b.hour,day=b.day,month=b.month,year=b.year;
    int is_pm=0;

    if(!(regb&0x02))is_pm=(hour&0x80)!=0;
    hour&=0x7F;

    if(!(regb&0x04)){
        sec=bcd2bin(sec);
        min=bcd2bin(min);
        hour=bcd2bin(hour);
        day=bcd2bin(day);
        month=bcd2bin(month);
        year=bcd2bin(year);
    }

    if(!(regb&0x02)){
        if(is_pm){if(hour<12)hour=(uint8_t)(hour+12);}else if(hour==12)hour=0;
    }

    t->year=(uint16_t)((year>=80)?(1900+year):(2000+year));
    t->month=month;
    t->day=day;
    t->hour=hour;
    t->minute=min;
    t->second=sec;
    return 0;
}

void rtc_format_date(const RtcTime *t,char *out,int max){
    if(!out||max<=0)return;
    if(!t||max<11){out[0]=0;return;}
    put4(t->year,out);
    out[4]='-';
    put2(t->month,out+5);
    out[7]='-';
    put2(t->day,out+8);
    out[10]=0;
}

void rtc_format_time(const RtcTime *t,char *out,int max){
    if(!out||max<=0)return;
    if(!t||max<9){out[0]=0;return;}
    put2(t->hour,out);
    out[2]=':';
    put2(t->minute,out+3);
    out[5]=':';
    put2(t->second,out+6);
    out[8]=0;
}
