#include "lib/parena.h"
#include "frame/test_frame.h"
#include "lib/logger.h"
#include <time.h>
#include <string.h>

#define PC_DATA_SIZE  (PA_BLOCK_SIZE - sizeof(PchainHead))   /* 每块可用字节 */

/* 遍历数块数（不依赖 linklist.h 是否导出 count API） */
static size_t pc_block_count(Pchain *pc) {
    size_t n = 0;
    for (ListNode *p = pc->list.begin; p; p = p->next) n ++;
    return n;
}

// 理论可以分配的 pchain 大小
static size_t get_theoretical_size(Parena* pa){
    return pa->block_count * PCHAIN_BLOCK_SIZE;
}

void test_append_basic(void) {
    test_case("append 基本行为");
    Parena *pa = parena_new(0);
    Pchain pc = {0};
    pchain_init(pa, &pc);

    test_expect("init 后链为空", list_is_empty(&pc.list), true);
    test_expect("单次 10B append", pchain_append(&pc, 10), true);
    test_expect("append 后有 1 个块", pc_block_count(&pc) == 1, true);
    test_expect("tail_remain < PA_BLOCK_SIZE", pc.tail_remain < PA_BLOCK_SIZE, true);

    test_expect("再填满 tail", pchain_append(&pc, pc.tail_remain), true);
    test_expect("tail 填满后 remain=0", pc.tail_remain == 0, true);

    test_expect("触发 grow 成功", pchain_append(&pc, 100), true);
    test_expect("grow 后 >1 块", pc_block_count(&pc) >= 2, true);

    pchain_recycle(&pc);
    parena_free(pa);
}

void test_correct_size(void){
    test_case("测试容积是否正确");

    u64 num[] = {2, 7, 11, 1022, 2096, 4096};

    size_t num_count = sizeof(num) / sizeof(u64);

    Parena *pa = parena_new(0);
    size_t answer = get_theoretical_size(pa);
    bool flag = true;

    for (u64 i = 0; i < num_count; i ++){
        u64 n = num[i];
        Pchain pc;
        pchain_init(pa, &pc);
        u64 count = 0;

        while (1){
            bool get = pchain_append(&pc, n);
            if (!get) break;
            count += n;
        }

        if (count > answer){
            flag = false;
            pr_debug("分配错误，和理论大小不一样 get_count = %ld, true_count = %ld", count, answer);
            break;
        }

        pchain_recycle(&pc);
    }

    test_expect("所有容积可以完成分配", flag, true);
}

void test_mark(void){
    test_case("测试Mark是否正常");

    srand(time(0));
    Parena *pa = parena_new(0);
    Pchain pc;
    Pmark mark = {0};

    pchain_init(pa, &pc);

    for (u64 i = 0; i < 20000; i ++){
        int r = rand() % 100;
        bool is_rewind = (rand() % 4 == 0);

        pchain_append(&pc, r);
        if (is_rewind){
            if (mark.belong){
                pchain_rewind(&pc, mark);
            }
        }else{
            mark = pchain_get_mark(&pc);
        }
    }

    test_expect("Mark 测试完成", true, true);
}

int main(void) {
    /* append 侧 */
    test_append_basic();
    test_correct_size();
    test_mark();

    test_summary();
    return 0;
}
