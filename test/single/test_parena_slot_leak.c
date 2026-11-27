#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include "lib/logger.h"
#include "lib/parena.h"
#include "frame/test_frame.h"
void *buffer[1000000];

void test_mem_leak(){
    Parena * pa = parena_new(0);
    int top_size = -1;
    srand(52);

    for (int i = 0; i < 10000; i ++){
        int idx = 0;
        while (1){
            size_t sz = rand() % 64 + 1;
            buffer[idx] = parena_alloc_slot(pa, sz);
            // pr_debug("idx = %d", idx);
            if (!buffer[idx]){
                if (top_size == -1) top_size = idx;
                else{
                    if (top_size != idx){
                        test_note("内存泄露");
                        return;
                    }
                }
                break;
            }

            memset(buffer[idx], rand() % 16, sz);
            idx ++;
        }

        for (int j = 0; j < idx; j ++){
            parena_recycle_slot(buffer[j]);
        }
    }

    test_note("内存未检测泄露");
}

int main(void) {
    // set_filter(LogDebug);
    setvbuf(stdout, NULL, _IONBF, 0);
    srand(0x1234);   /* D 用固定 seed，保证可复现 */

    test_note("内存泄漏测试");
    double time = get_func_cost_time(test_mem_leak());
    pr_debug("test_mem_leak cost time = %lf", time / 1e9);

    return 0;
}
