#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "frame/test_frame.h"
#include "lib/logger.h"
#include "lib/parena.h"

#define BLOCK_PER_ARENA 10
#define ARENA_BYTES     (4096 * BLOCK_PER_ARENA)

/* 1. alloc 返回的指针必须能完整写入请求的 size 字节 */
bool test_alloc_returns_writable(bool info){
    if (info) test_case("alloc 返回可写内存");
    Parena *a = parena_new(ARENA_BYTES);
    if (info)  test_expect("arena 创建成功", a != NULL, true);
    if (!a) return false;

    unsigned char *p = parena_alloc_slot(a, 64);
    if (info) test_expect("64B 分配成功", p != NULL, true);
    if (!p) { parena_free(a); return false; }

    memset(p, 0xAB, 64);
    if (info) test_expect("64B 内首字节可读回", p[0]  == 0xAB, true);
    if (info) test_expect("64B 内尾字节可读回", p[63] == 0xAB, true);

    void *q = parena_alloc_slot(a, 64);
    if (info) test_expect("第二次分配成功", q != NULL, true);
    if (info) test_expect("两次分配地址不同", q != p, true);

    parena_free(a);
    return true;
}

/* 2. 多次分配返回的指针两两不同 */
void test_slots_no_overlap(void){
    test_case("多次分配槽位不重叠");
    Parena *a = parena_new(ARENA_BYTES);
    test_expect("arena 创建成功", a != NULL, true);
    if (!a) return;

    enum { N_ALLOC = 200 };
    void *ptrs[N_ALLOC];
    size_t got = 0;

    for (size_t i = 0; i < N_ALLOC; i++) {
        void *p = parena_alloc_slot(a, 64);
        if (!p) break;
        ptrs[got++] = p;
    }

    test_expect("至少分配成功 1 个",  got >= 1,     true);
    test_expect("分配数量不超过请求", got <= N_ALLOC, true);

    bool no_dup = true;
    for (size_t i = 0; i < got && no_dup; i++) {
        for (size_t j = i + 1; j < got; j++) {
            if (ptrs[i] == ptrs[j]) { no_dup = false; break; }
        }
    }
    test_expect("所有返回指针两两不同", no_dup, true);

    parena_free(a);
}

/* 3. 每槽写入唯一 pattern，读回一致 => 槽间互不干扰 */
void test_slots_write_isolation(void){
    test_case("槽位写入互不干扰");
    Parena *a = parena_new(ARENA_BYTES);
    test_expect("arena 创建成功", a != NULL, true);
    if (!a) return;

    enum { N_ALLOC = 100, PATTERN_LEN = 16 };
    unsigned char *ptrs[N_ALLOC];
    size_t got = 0;

    for (size_t i = 0; i < N_ALLOC; i++) {
        unsigned char *p = parena_alloc_slot(a, 64);
        if (!p) break;
        ptrs[got] = p;
        memset(p, (unsigned char)(i & 0xFF), PATTERN_LEN);
        got++;
    }
    test_expect("至少分配成功 1 个", got >= 1, true);

    bool all_ok = true;
    for (size_t i = 0; i < got && all_ok; i++) {
        for (size_t k = 0; k < PATTERN_LEN; k++) {
            if (ptrs[i][k] != (unsigned char)(i & 0xFF)) {
                all_ok = false;
                break;
            }
        }
    }
    test_expect("所有槽内容未被邻居覆盖", all_ok, true);

    parena_free(a);
}

/* 4. 超过最大槽尺寸的请求返回 NULL，且失败后 arena 仍可用 */
void test_alloc_oversize_fail(void){
    test_case("超尺寸分配返回 NULL");
    Parena *a = parena_new(ARENA_BYTES);
    test_expect("arena 创建成功", a != NULL, true);
    if (!a) return;

    test_expect("4096B 分配成功",   parena_alloc_slot(a, 4096) != NULL, true);
    test_expect("8192B 分配失败",   parena_alloc_slot(a, 8192) == NULL, true);
    test_expect("1MB  分配失败",    parena_alloc_slot(a, (size_t)1<<20) == NULL, true);

    void *p = parena_alloc_slot(a, 16);
    test_expect("超界失败后仍可小分配", p != NULL, true);

    parena_free(a);
}

/* 5. 固定容量下分配数量落在合理范围 */
void test_alloc_capacity_bound(void){
    test_case("固定容量下分配数量范围");
    Parena *a = parena_new(ARENA_BYTES);
    test_expect("arena 创建成功", a != NULL, true);
    if (!a) return;

    size_t cnt = 0;
    for (cnt = 1; cnt <= 2562; cnt ++){
        if (parena_alloc_slot(a, 16) == NULL){
            cnt --;
            break;
        }
    }

    test_expect("分配数量 == 2560", cnt == (size_t)BLOCK_PER_ARENA * 4096 / 16, true);

    void *extra = parena_alloc_slot(a, 16);
    test_expect("耗尽后再分配返回 NULL", extra == NULL, true);

    parena_free(a);
}

/* 6. 多个 arena 完全独立 */
void test_multi_parena_independence(void){
    test_case("多 arena 相互独立");
    Parena *a1 = parena_new(4096 * 4);
    Parena *a2 = parena_new(4096 * 4);
    test_expect("a1 创建成功",  a1 != NULL, true);
    test_expect("a2 创建成功",  a2 != NULL, true);
    test_expect("a1 != a2",     a1 != a2,  true);

    if (!a1 || !a2) { parena_free(a1); parena_free(a2); return; }

    while (parena_alloc_slot(a1, 16) != NULL) { }
    test_expect("a1 耗尽后分配失败", parena_alloc_slot(a1, 16) == NULL, true);

    void *p2 = parena_alloc_slot(a2, 16);
    test_expect("a2 不受 a1 影响", p2 != NULL, true);

    parena_free(a1);
    parena_free(a2);
}

void test_recycle(void){
    test_case("测试回收");

    Parena* a = parena_new(0);
    ASSERT(a);

    void *buffer[128];
    memset(buffer, 0, sizeof(buffer));

    bool flag = true;

    for (int i = 0; i < 20; i ++){
        buffer[i] = parena_alloc_slot(a, 20 * i + 1);
        if (!buffer[i]){
            flag = false;
            break;
        }
    }

    test_expect("正常分配内存成功", flag, true);
    if (!flag) return;
    
    for (int i = 0; i < 20; i ++){
        parena_recycle_slot(buffer[i]);
    }

    test_note("释放正常");

    test_note("再次尝试分配");
    memset(buffer, 0, sizeof(buffer));

    for (int i = 0; i < 127; i ++){
        buffer[i] = parena_alloc_slot(a, 200);
        if (!buffer[i]){
            flag = false;
            break;
        }
    }

    test_expect("正常分配内存成功 * 2", flag, true);
    if (!flag) return;

    for (int i = 0; i < 127; i ++){
        if (!buffer[i]) break;
        parena_recycle_slot(buffer[i]);
    }

    test_note("释放正常 * 2");
}

void test_global_arena_behavior(){
    test_case("全局 arena 测试");
    bool flag = true;
    for (int i = 0; i < 20; i ++){
        if (!test_alloc_returns_writable(false)){
            flag = false;
        }
    }

    test_expect("global arena 分配释放正常", flag, true);

    Parena *pa[200];
    int i = 0;
    for (i = 0; i < 200; i ++){
        pa[i] = parena_new(0);
        if (!pa[i]) break;
    }

    for (int j = 0; j < i; j ++){
        parena_free(pa[j]);
    }

    test_note("global arena 没有出现异常");
}

int main(void){
    test_expect("sizeof(Block) == 4096", sizeof(Block) == 4096, true);

    test_alloc_returns_writable(true);
    test_global_arena_behavior();
    test_slots_no_overlap();
    test_slots_write_isolation();
    test_alloc_oversize_fail();
    test_multi_parena_independence();
    test_alloc_capacity_bound();
    test_recycle();

    test_summary();
    return 0;
}
