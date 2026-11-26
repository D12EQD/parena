#define _GNU_SOURCE
#include "test_frame.h"
#include <stdio.h>
#include <time.h>

TestCtx testctx = {0};
void (*test_func[1024])(void);
u32 test_fun_count = 0;

void test_ctx_reset() {
    testctx.total = 0;
    testctx.pass = 0;
    testctx.fail = 0;
    testctx.note = 0;
}

double test_nstime_get(){
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}


/* 执行测试函数 */
void test_case(const char *name) {
    printf(ANSI_CYAN "\n[ CASE ] %s" ANSI_RESET "\n", name);
}

/* 判断got是否等于want */
void test_expect(const char *what, i64 got, i64 want) {
    testctx.total ++;

    if (got == want) {
        testctx.pass ++;
        printf(ANSI_GREEN "[ PASS ]" ANSI_RESET " %s\n", what);
        return;
    }

    testctx.fail ++;
    printf(
        ANSI_RED "[ FAIL ]" ANSI_RESET " %s got=%ld want=%ld\n",
        what, got, want
    );
}

/* 注意输出 */
void test_note(const char *what) {
    testctx.note ++;
    printf(ANSI_YELLOW "[ NOTE ]" ANSI_RESET " %s\n", what);
}

void test_summary() {
    printf("\n==== TEST SUMMARY pass=%u fail=%u note=%u total=%u ====\n",
        testctx.pass, testctx.fail, testctx.note, testctx.total
    );

    if (testctx.fail == 0) {
        printf(ANSI_GREEN "ALL GREEN" ANSI_RESET "\n");
    } else {
        printf(ANSI_RED "FAILED" ANSI_RESET "\n");
    }
}

