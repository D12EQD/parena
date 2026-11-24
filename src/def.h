#pragma once

#define likely(x)	(__builtin_expect(!!(x), 1))
#define unlikely(x)	(__builtin_expect(!!(x), 0))

#ifndef _ASM
#define _ASM __asm__ __volatile__
#endif

#define max(a, b) (( a ) > ( b ) ? ( a ) : ( b ))
#define min(a, b) (( a ) < ( b ) ? ( a ) : ( b ))

#define get_bit(b, l, r) (((b) >> (l)) & ((1ULL << ((r) - (l) + 1)) - 1))
#define set_bit(b, l, r) ((b) |= (((1ULL << ((r) - (l) + 1)) - 1) << (l)))
#define lowbit(x) ((x) & (-x))

#define ANSI_RESET          "\033[0m"
#define ANSI_RED            "\033[31m"
#define ANSI_GREEN          "\033[1;32m"
#define ANSI_YELLOW         "\033[33m"
#define ANSI_BLUE           "\033[34m"
#define ANSI_MAGENTA        "\033[35m"
#define ANSI_CYAN           "\033[36m"
#define ANSI_BRIGHT_RED     "\033[1;31m"
#define ANSI_BRIGHT_YELLOW  "\033[1;33m"
#define ANSI_BRIGHT_GREEN   "\033[1;32m"

#define AUTO_CLEAN(func)  __attribute__((cleanup(func)))
typedef signed char         i8;
typedef unsigned char       u8;

typedef signed short        i16;
typedef unsigned short      u16;

typedef signed int          i32;
typedef unsigned int        u32;

typedef signed long         i64;
typedef unsigned long       u64;
