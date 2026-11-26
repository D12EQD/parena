#pragma once
#include "def.h"

enum LogLevel{
    LogInfo,
    LogWarn,
    LogError,
    LogDebug,
    LogLvCount
};

void set_filter(u8 lv);
void clean_filter(u8 lv);
void set_level_fd(u8 lv,int fd);

__attribute__((format(printf, 2, 3))) 
void log_print(u8 lv, const char *fmt, ...);

#define pr_info(fmt, ...)   log_print(LogInfo,  fmt "\n", ##__VA_ARGS__)
#define pr_warn(fmt, ...)   log_print(LogWarn, fmt "\n", ##__VA_ARGS__)
#define pr_error(fmt, ...)  log_print(LogError, fmt "\n", ##__VA_ARGS__)
#define pr_debug(fmt, ...)  log_print(LogDebug, fmt "\n", ##__VA_ARGS__)

// 不换行 continue 的缩写
#define pr_cont_info(fmt, ...)   log_print(LogInfo,  fmt, ##__VA_ARGS__)
#define pr_cont_warn(fmt, ...)   log_print(LogWarn, fmt, ##__VA_ARGS__)
#define pr_cont_error(fmt, ...)  log_print(LogError, fmt, ##__VA_ARGS__)
#define pr_cont_debug(fmt, ...)  log_print(LogDebug, fmt, ##__VA_ARGS__)

static inline void pr_debug_bits(u8 *data, u64 size){
    pr_debug("show bits : ");
    for (u64 i = 0; i < size; i ++){
        if (i && i % 2 == 0) pr_cont_debug(" ");
        pr_cont_debug("%x", data[i]);
        if (i && i % 32 == 0) pr_debug("");
    }
}

#define DEVELOPMENT_MODE
#ifdef DEVELOPMENT_MODE
#define PANIC(fmt, ...)                 \
    do{                                 \
        pr_error(ANSI_RED "PANIC ERROR " ANSI_RESET " [ %s:%d ]", __FILE__, __LINE__); \
        pr_error(fmt, ##__VA_ARGS__);   \
        exit(69);                       \
    }while(0)

#define ASSERT(cond)                    \
    do {                                \
        if unlikely(!( cond )) {        \
            pr_error(ANSI_RED "ASSERT ERROR " ANSI_RESET "[ %s:%d ]\n", __FILE__, __LINE__); \
            exit(69);                   \
        }                               \
    } while (0)
#else

#define ASSERT(cond) ((void*)(0))
#define PANIC(cond) ((void*)(0))

#endif
