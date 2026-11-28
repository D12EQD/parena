#pragma once
#include "def.h"

enum LogLevel { LogInfo, LogWarn, LogError, LogDebug, LogLvCount };

void set_filter(u8 lv);
void clean_filter(u8 lv);
void set_level_fd(u8 lv, int fd);

__attribute__((format(printf, 2, 3))) void log_print(u8 lv, const char *fmt, ...);

#define pr_info(fmt, ...) log_print(LogInfo, fmt "\n", ##__VA_ARGS__)
#define pr_warn(fmt, ...) log_print(LogWarn, fmt "\n", ##__VA_ARGS__)
#define pr_error(fmt, ...) log_print(LogError, fmt "\n", ##__VA_ARGS__)
#define pr_debug(fmt, ...) log_print(LogDebug, fmt "\n", ##__VA_ARGS__)

// 不换行 continue 的缩写
#define pr_cont_info(fmt, ...) log_print(LogInfo, fmt, ##__VA_ARGS__)
#define pr_cont_warn(fmt, ...) log_print(LogWarn, fmt, ##__VA_ARGS__)
#define pr_cont_error(fmt, ...) log_print(LogError, fmt, ##__VA_ARGS__)
#define pr_cont_debug(fmt, ...) log_print(LogDebug, fmt, ##__VA_ARGS__)

static inline void pr_debug_bits(u8 *data, u64 size) {
    pr_debug("show bits : ");
    for (u64 i = 0; i < size; i++) {
        if (i && i % 2 == 0)
            pr_cont_debug(" ");
        pr_cont_debug("%x", data[i]);
        if (i && i % 32 == 0)
            pr_debug("");
    }
}

// #define DEVELOPMENT_MODE

#ifdef DEVELOPMENT_MODE
#define PANIC(fmt, ...)                                                                            \
    do {                                                                                           \
        pr_error(ANSI_RED "PANIC ERROR " ANSI_RESET " [ %s:%d ]", __FILE__, __LINE__);             \
        pr_error(fmt, ##__VA_ARGS__);                                                              \
        exit(69);                                                                                  \
    } while (0)

#define ASSERT(cond)                                                                               \
    do {                                                                                           \
        if unlikely (!(cond)) {                                                                    \
            pr_error(ANSI_RED "ASSERT ERROR " ANSI_RESET "[ %s:%d ]\n", __FILE__, __LINE__);       \
            exit(69);                                                                              \
        }                                                                                          \
    } while (0)

#define DBG(var)                                                                                   \
    do {                                                                                           \
        pr_cont_debug(#var " = ");                                                                 \
        char debug_res[20];                                                                        \
        u64  debug_num = ((u64)(var));                                                             \
        i64  debug_idx = 0;                                                                        \
        if (debug_num == 0) {                                                                      \
            pr_cont_debug("0");                                                                    \
        } else {                                                                                   \
            while (debug_num) {                                                                    \
                debug_res[debug_idx] = (debug_num % 10) + '0';                                     \
                debug_num /= 10;                                                                   \
                debug_idx++;                                                                       \
            }                                                                                      \
            for (i64 i = debug_idx - 1; i >= 0; i--) {                                             \
                pr_cont_debug("%c", debug_res[i]);                                                 \
            }                                                                                      \
        }                                                                                          \
        pr_cont_debug("\n");                                                                       \
    } while (0);

#else

#define DBG(var) ((void *)(0))
#define ASSERT(cond) ((void *)(0))
#define PANIC(cond) ((void *)(0))

#endif
