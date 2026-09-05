#include <rtc.h>
#include <ports.h>

#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

static u8 cmos_read(u8 reg) {
    outb(CMOS_ADDR, (u8)(0x80 | reg));
    return inb(CMOS_DATA);
}

static u8 decode(u8 value, u8 status_b) {
    if (status_b & 0x04) {
        return value;
    }
    return (u8)((value & 0x0F) + ((value >> 4) * 10));
}

static u8 normalize_hour(u8 hour, u8 status_b) {
    u8 h = decode((u8)(hour & 0x7F), status_b);
    if (status_b & 0x02) {
        return h;
    }
    {
        bool pm = (hour & 0x80) != 0;
        if (h == 12) {
            h = 0;
        }
        if (pm) {
            h = (u8)(h + 12);
        }
        return h;
    }
}

void rtc_read(struct rtc_time *out) {
    u8 status_b;
    if (!out) {
        return;
    }
    while (cmos_read(0x0A) & 0x80) {
    }
    status_b = cmos_read(0x0B);
    out->second = decode(cmos_read(0x00), status_b);
    out->minute = decode(cmos_read(0x02), status_b);
    out->hour = normalize_hour(cmos_read(0x04), status_b);
    out->day = decode(cmos_read(0x07), status_b);
    out->month = decode(cmos_read(0x08), status_b);
    out->year = (u16)(2000 + decode(cmos_read(0x09), status_b));
    if (out->month == 0 || out->day == 0) {
        out->month = 1;
        out->day = 1;
    }
}