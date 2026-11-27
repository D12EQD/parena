/*
 * Pbuffer 功能测试
 *
 * 覆盖：
 *   - slot: 固定容量、部分写、peek 不推进 offset
 *   - chain: 跨块读写、order 正确、默认不增长、grow 到 max_size 截断
 *   - reset 复用、deinit/free 归还
 *
 * 运行：
 *   ./build/nob run test/single/test_pbuffer.c
 */
#include "lib/pbuffer.h"
#include "lib/logger.h"
#include "frame/test_frame.h"
#include <string.h>

static void fill_pattern(u8 *p, size_t n, u8 seed){
    for (size_t i = 0; i < n; i++) p[i] = (u8)(seed + i * 31);
}

static bool check_pattern(const u8 *p, size_t n, u8 seed){
    for (size_t i = 0; i < n; i++){
        if (p[i] != (u8)(seed + i * 31)) return false;
    }
    return true;
}

void test_slot_basic(void){
    test_case("slot 固定容量 + 部分写");

    Parena *pa = parena_new(4096 * 8);
    test_expect("arena 创建成功", pa != NULL, true);
    if (!pa) return;

    Pbuffer b;
    test_expect("init 成功", pbuffer_init(&b, pa, PbufferSlot, 100), true);
    test_expect("cap 对齐到 slot 等级 128", b.cap == 128, true);
    test_expect("初始 len == 0", pbuffer_len(&b) == 0, true);

    u8 src[256];
    fill_pattern(src, sizeof(src), 1);

    test_expect("写 50B", pbuffer_write(&b, src, 50) == 50, true);
    test_expect("count == 50", b.count == 50, true);

    u8 peeked[50];
    test_expect("peek 50B", pbuffer_peek(&b, peeked, 50) == 50, true);
    test_expect("peek 不推进 offset", b.offset == 0, true);
    test_expect("peek 内容正确", check_pattern(peeked, 50, 1), true);

    test_expect("越界写被截断", pbuffer_write(&b, src, 200) == 128 - 50, true);
    test_expect("写满后再写返回 0", pbuffer_write(&b, src, 1) == 0, true);

    u8 got[128];
    test_expect("一次读 100B", pbuffer_read(&b, got, 100) == 100, true);
    test_expect("读后 offset == 100", b.offset == 100, true);
    test_expect("读内容与首次写入一致", check_pattern(got, 50, 1), true);
    test_expect("读掉剩余 28B", pbuffer_read(&b, got, 100) == 28, true);
    test_expect("读空返回 0", pbuffer_read(&b, got, 10) == 0, true);

    pbuffer_deinit(&b);
    parena_free(pa);
}

void test_chain_no_grow(void){
    test_case("chain 默认不增长");

    Parena *pa = parena_new(4096 * 8);
    test_expect("arena 创建成功", pa != NULL, true);
    if (!pa) return;

    Pbuffer b;
    test_expect("init 成功", pbuffer_init(&b, pa, PbufferChain, 100), true);
    test_expect("cap == 100", b.cap == 100, true);

    u8 src[200];
    fill_pattern(src, sizeof(src), 7);

    test_expect("写 200B 只接受 100B", pbuffer_write(&b, src, 200) == 100, true);
    test_expect("count == 100", b.count == 100, true);

    u8 got[200];
    test_expect("读回 100B", pbuffer_read(&b, got, 200) == 100, true);
    test_expect("内容正确", check_pattern(got, 100, 7), true);

    pbuffer_deinit(&b);
    parena_free(pa);
}

void test_chain_cross_block(void){
    test_case("chain 跨块读写");

    Parena *pa = parena_new(4096 * 16);
    test_expect("arena 创建成功", pa != NULL, true);
    if (!pa) return;

    Pbuffer b;
    test_expect("init 成功",
        pbuffer_init(&b, pa, PbufferChain, 16, .grow = true, .max_size = 4096 * 4), true);

    enum { N = 10000 };
    u8 *src = malloc(N);
    u8 *got = malloc(N);
    fill_pattern(src, N, 3);

    test_expect("写 10000B", pbuffer_write(&b, src, N) == N, true);
    test_expect("count == 10000", b.count == N, true);

    test_expect("peek 10000B", pbuffer_peek(&b, got, N) == N, true);
    test_expect("peek 内容跨块正确", check_pattern(got, N, 3), true);

    test_expect("读 10000B", pbuffer_read(&b, got, N) == N, true);
    test_expect("读后内容跨块正确", check_pattern(got, N, 3), true);
    test_expect("读空返回 0", pbuffer_read(&b, got, 1) == 0, true);

    free(src);
    free(got);
    pbuffer_deinit(&b);
    parena_free(pa);
}

void test_chain_grow_limit(void){
    test_case("chain 增长受 max_size 限制");

    Parena *pa = parena_new(4096 * 16);
    test_expect("arena 创建成功", pa != NULL, true);
    if (!pa) return;

    Pbuffer b;
    test_expect("init 成功",
        pbuffer_init(&b, pa, PbufferChain, 100, .grow = true, .max_size = 10000), true);

    enum { N = 12000 };
    u8 *src = malloc(N);
    fill_pattern(src, N, 9);

    test_expect("写 12000B 截断到 10000", pbuffer_write(&b, src, N) == 10000, true);
    test_expect("cap == 10000", b.cap == 10000, true);

    u8 *got = malloc(10000);
    test_expect("读回 10000B", pbuffer_read(&b, got, 10000) == 10000, true);
    test_expect("内容正确", check_pattern(got, 10000, 9), true);

    free(src);
    free(got);
    pbuffer_deinit(&b);
    parena_free(pa);
}

void test_extend_and_reset(void){
    test_case("extend + reset 复用");

    Parena *pa = parena_new(4096 * 8);
    test_expect("arena 创建成功", pa != NULL, true);
    if (!pa) return;

    Pbuffer b;
    test_expect("init 成功", pbuffer_init(&b, pa, PbufferChain, 32), true);

    test_expect("grow=0 时 extend 失败", pbuffer_extend(&b, 64) == false, true);
    test_expect("cap 不变", b.cap == 32, true);

    u8 src[32];
    fill_pattern(src, sizeof(src), 5);
    test_expect("写满", pbuffer_write(&b, src, 32) == 32, true);

    test_expect("reset 成功", (pbuffer_reset(&b), b.count == 0 && b.offset == 0), true);
    test_expect("reset 后 cap == 0", b.cap == 0, true);
    test_expect("reset 后 grow=0 不可写", pbuffer_write(&b, src, 1) == 0, true);

    pbuffer_deinit(&b);
    parena_free(pa);
}

void test_new_free(void){
    test_case("pbuffer_new / free 归还");

    Parena *pa = parena_new(4096 * 8);
    test_expect("arena 创建成功", pa != NULL, true);
    if (!pa) return;

    u64 empty_before = pa->empty_block_count;

    Pbuffer *b = pbuffer_new(pa, PbufferChain, 500, .grow = true, .max_size = 8192);
    test_expect("new 成功", b != NULL, true);
    if (b){
        u8 src[500];
        fill_pattern(src, sizeof(src), 11);
        test_expect("new 后写入", pbuffer_write(b, src, sizeof(src)) == 500, true);
        pbuffer_free(b);
    }

    test_expect("free 后块全部归还", pa->empty_block_count == empty_before, true);

    parena_free(pa);
}

int main(void){
    test_slot_basic();
    test_chain_no_grow();
    test_chain_cross_block();
    test_chain_grow_limit();
    test_extend_and_reset();
    test_new_free();

    test_summary();
    return 0;
}
