#ifndef KRYSPINOS_RTC_H
#define KRYSPINOS_RTC_H

#include <types.h>

struct rtc_time {
    u8 second;
    u8 minute;
    u8 hour;
    u8 day;
    u8 month;
    u16 year;
};

void rtc_read(struct rtc_time *out);

#endif