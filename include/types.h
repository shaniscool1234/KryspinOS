#ifndef CURSOROS_TYPES_H
#define CURSOROS_TYPES_H

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;
typedef signed char        i8;
typedef signed short       i16;
typedef signed int         i32;
typedef signed long long   i64;

typedef u32 size_t;
typedef i32 ssize_t;
typedef u32 uintptr_t;
typedef u8  bool;

#define true  1
#define false 0
#define NULL  ((void *)0)

#define PACKED __attribute__((packed))
#define ALIGN(x) __attribute__((aligned(x)))
#define UNUSED __attribute__((unused))

#define KERNEL_CS 0x08
#define KERNEL_DS 0x10

#endif
