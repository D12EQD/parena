#pragma once
#include "def.h"
#include <stdio.h>

typedef struct {
    u32 total;
    u32 pass;
    u32 fail;
    u32 note;
} TestCtx;

extern TestCtx testctx;

void   test_ctx_reset();
void   test_case(const char *name);
void   test_note(const char *what, ...);
void   test_summary();
double test_nstime_get();

#define test_expect(what, cond)                                                                    \
    do {                                                                                           \
        bool __test_res = (cond);                                                                  \
        test_expect_imple((what), __test_res);                                                     \
        if (!__test_res) {                                                                         \
            printf(ANSI_RED " Reason : " ANSI_RESET #cond "\n");                                   \
        } else {                                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

void test_expect_imple(const char *what, bool cond);

#define test_get_func_cost_time(func)                                                              \
    ({                                                                                             \
        double time = test_nstime_get();                                                           \
        func;                                                                                      \
        test_nstime_get() - time;                                                                  \
    })
