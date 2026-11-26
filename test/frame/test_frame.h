#pragma once
#include "def.h"

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

typedef struct {
    u32 total;
    u32 pass;
    u32 fail;
    u32 note;
} TestCtx;

extern TestCtx testctx;

void test_ctx_reset();
void test_case(const char *name);
void test_expect(const char *what, i64 got, i64 want);
void test_note(const char *what);
void test_summary();
double test_nstime_get();

#define get_func_cost_time(func) ({       \
    double time = test_nstime_get();    \
    func;                               \
    test_nstime_get() - time;           \
 })
