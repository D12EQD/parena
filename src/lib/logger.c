#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include "logger.h"
#include "def.h"

int log_fd[LogLvCount];
char log_temp[512];

u64 log_filter_level;

void set_filter(u8 lv){
    log_filter_level |= 1 << lv;
}

void clean_filter(u8 lv){
    log_filter_level &= ~(1 << lv);
}

void set_level_fd(u8 lv, int fd){
    log_fd[lv] = fd;
}

__attribute__((format(printf, 2, 3)))
void log_print(u8 lv, const char *fmt, ...){
    if (( (1 << lv) & log_filter_level )) return;
    va_list args;
    va_start(args, fmt);

    u64 res = vsnprintf(log_temp, sizeof(log_temp), fmt, args);

    u64 cost = 0;
    while (cost < res){
        i64 flag = write(log_fd[lv], log_temp, res);
        if (flag < 0) return;
        cost += flag;
    }

    va_end(args);
}

__attribute__((constructor))
void log_init(){
    for (int i = 0; i < LogLvCount; i ++){
        log_fd[i] = 2; // stderr
    }
}
