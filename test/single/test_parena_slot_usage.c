/*
 * Parena 内存使用率 / 记账一致性检测
 *
 * 与 test_parena_cost.c（纯性能压测）分开：
 *   - 本文件是功能 + 记账正确性测试，跑得很快，失败即代表统计/回收逻辑出错；
 *   - 通过 Parena 暴露的内部统计（empty_block_count、RefBlockHeader 的
 *     free_count/total_count/slot_level）汇总出使用率快照并断言。
 *
 * 运行：
 *   ./build/nob run test/single/test_parena_usage.c
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "lib/parena.h"
#include "frame/test_frame.h"

/* ------------------------------------------------------------------ *
 *  使用率快照：从 Parena 内部统计数据汇总得到
 * ------------------------------------------------------------------ */
typedef struct {
    u64 block_count;        /* 总块数 */
    u64 free_blocks;        /* 空闲块数 (empty_block_count) */
    u64 used_blocks;        /* 已占用块数 = total - free */
    u64 header_blocks;      /* 被 RefBlockHeader 预占的块数（创建时一次性划走） */
    u64 data_blocks;        /* 真正承载 slot 的数据块数 */

    u64 total_slots;        /* 所有数据块内 slot 总数 */
    u64 free_slots;         /* 空闲 slot 总数 */
    u64 used_slots;         /* 已分配 slot 总数 */

    u64 total_bytes;        /* arena 总容量字节 */
    u64 slot_bytes;         /* 数据块中 slot 容量字节 */
    u64 used_slot_bytes;    /* 已分配 slot 占用的字节 */
    u64 free_block_bytes;   /* 空闲块字节 */

    u64 lv_total[slot_level_count];
    u64 lv_free[slot_level_count];
    u64 lv_used[slot_level_count];
    u64 lv_bytes[slot_level_count];
} PaUsage;

static inline u64 ceil_div(u64 a, u64 b) { return (a + b - 1) / b; }

static void pa_usage_collect(Parena *a, PaUsage *u) {
    memset(u, 0, sizeof(*u));

    u->block_count  = a->block_count;
    u->free_blocks  = a->empty_block_count;
    u->used_blocks  = a->block_count - a->empty_block_count;
    u->header_blocks = ceil_div(a->block_count * (u64)sizeof(RefBlockHeader), PA_BLOCK_SIZE);
    u->data_blocks  = u->used_blocks > u->header_blocks
                    ? u->used_blocks - u->header_blocks : 0;

    u->total_bytes      = a->block_count * (u64)PA_BLOCK_SIZE;
    u->free_block_bytes = u->free_blocks * (u64)PA_BLOCK_SIZE;

    for (u64 lv = 0; lv < slot_level_count; lv++) {
        /* slot_list[lv] 里挂的是 RefBlockHeader，node 是首成员，可直接转型 */
        for (ListNode *n = a->slot_list[lv].begin; n; n = n->next) {
            RefBlockHeader *h = (RefBlockHeader *)n;
            u->lv_total[lv] += h->total_count;
            u->lv_free[lv]  += h->free_count;
        }
        u->lv_used[lv]  = u->lv_total[lv] - u->lv_free[lv];
        u->lv_bytes[lv] = u->lv_total[lv] * (u64)slot_level[lv];

        u->total_slots     += u->lv_total[lv];
        u->free_slots      += u->lv_free[lv];
        u->used_slots      += u->lv_used[lv];
        u->slot_bytes      += u->lv_bytes[lv];
        u->used_slot_bytes += u->lv_used[lv] * (u64)slot_level[lv];
    }
}

static double pct(u64 part, u64 whole) {
    return whole ? 100.0 * (double)part / (double)whole : 0.0;
}

static void pa_usage_print(const char *title, const PaUsage *u) {
    printf("\n+----- [%s] -----\n", title);

    printf("  blocks : total=%llu free=%llu used=%llu (header=%llu data=%llu)\n",
           (unsigned long long)u->block_count,
           (unsigned long long)u->free_blocks,
           (unsigned long long)u->used_blocks,
           (unsigned long long)u->header_blocks,
           (unsigned long long)u->data_blocks);

    printf("  bytes  : total=%llu  free_block=%llu  slot_cap=%llu  slot_used=%llu\n",
           (unsigned long long)u->total_bytes,
           (unsigned long long)u->free_block_bytes,
           (unsigned long long)u->slot_bytes,
           (unsigned long long)u->used_slot_bytes);

    printf("  slots  : total=%llu used=%llu free=%llu (slot util %.2f%%)\n",
           (unsigned long long)u->total_slots,
           (unsigned long long)u->used_slots,
           (unsigned long long)u->free_slots,
           pct(u->used_slots, u->total_slots));

    printf("  usage  : vs arena %.2f%%  vs data-block %.2f%%\n",
           pct(u->used_slot_bytes, u->total_bytes),
           pct(u->used_slot_bytes, u->data_blocks * (u64)PA_BLOCK_SIZE));

    for (u64 lv = 0; lv < slot_level_count; lv++) {
        if (u->lv_total[lv] == 0) continue;
        printf("    L%llu slot=%4u  used=%4llu/%4llu  bytes=%7llu  util=%6.2f%%\n",
               (unsigned long long)lv,
               (unsigned)slot_level[lv],
               (unsigned long long)u->lv_used[lv],
               (unsigned long long)u->lv_total[lv],
               (unsigned long long)(u->lv_used[lv] * (u64)slot_level[lv]),
               pct(u->lv_used[lv], u->lv_total[lv]));
    }
    printf("+-----\n");
}

/* ------------------------------------------------------------------ *
 *  测试用例
 * ------------------------------------------------------------------ */

/* 1. 刚创建时：没有任何 slot，数据块为 0，只被 RefBlockHeader 预占若干块 */
static void test_initial_state(void) {
    test_case("初始状态记账");

    enum { BLOCKS = 16 };
    Parena *a = parena_new(BLOCKS * PA_BLOCK_SIZE);
    if (!a) { test_expect("arena 创建成功", 0, 1); return; }

    PaUsage u;
    pa_usage_collect(a, &u);
    pa_usage_print("initial", &u);

    u64 expect_header = ceil_div(BLOCKS * (u64)sizeof(RefBlockHeader), PA_BLOCK_SIZE);

    test_expect("初始 used_slots == 0",        (i64)u.used_slots,      0);
    test_expect("初始 data_blocks == 0",       (i64)u.data_blocks,     0);
    test_expect("header_blocks 与公式一致",    (i64)u.header_blocks,   (i64)expect_header);
    test_expect("初始空闲块数正确",            (i64)u.free_blocks,     (i64)(BLOCKS - expect_header));
    test_expect("used + free == block_count",  (i64)(u.used_blocks + u.free_blocks),
                                               (i64)u.block_count);

    parena_free(a);
}

/* 2. 每个等级分别分配若干，lv_used 必须与实际存活数一致；全部回收后必须归零 */
static void test_level_accounting(void) {
    test_case("各等级分配/回收计数一致");

    enum { PER_LV = 40, MAXP = PER_LV * 8 };
    Parena *a = parena_new(64 * PA_BLOCK_SIZE);
    if (!a) { test_expect("arena 创建成功", 0, 1); return; }

    static void *ptrs[MAXP];
    u64 live[slot_level_count] = {0};
    size_t got = 0;

    for (u16 lv = 0; lv < slot_level_count; lv++) {
        for (int k = 0; k < PER_LV; k++) {
            void *p = parena_alloc_slot(a, slot_level[lv]);
            if (!p) break;
            ptrs[got] = p;
            live[lv]++;
            got++;
        }
    }

    PaUsage u;
    pa_usage_collect(a, &u);
    pa_usage_print("after alloc", &u);

    test_expect("实际分配数与统计 used_slots 一致",
                (i64)u.used_slots, (i64)got);

    bool lv_ok = true;
    for (u16 lv = 0; lv < slot_level_count; lv++)
        if (u.lv_used[lv] != live[lv]) lv_ok = false;
    test_expect("每个等级 lv_used == 存活数", lv_ok, true);

    bool inv_ok = true;
    for (u16 lv = 0; lv < slot_level_count; lv++)
        if (u.lv_free[lv] + u.lv_used[lv] != u.lv_total[lv]) inv_ok = false;
    test_expect("各等级 free+used == total", inv_ok, true);
    test_expect("全局 free_slots+used_slots == total_slots",
                (i64)(u.free_slots + u.used_slots), (i64)u.total_slots);

    /* 全部回收 */
    for (size_t i = 0; i < got; i++)
        parena_recycle_slot(a, ptrs[i]);

    pa_usage_collect(a, &u);
    pa_usage_print("after recycle", &u);

    bool zero_ok = true;
    for (u16 lv = 0; lv < slot_level_count; lv++)
        if (u.lv_used[lv] != 0) zero_ok = false;
    test_expect("回收后每等级 used 归零", zero_ok, true);
    test_expect("回收后 used_slots == 0", (i64)u.used_slots, 0);
    test_expect("回收后 free_slots == total_slots",
                (i64)u.free_slots, (i64)u.total_slots);

    parena_free(a);
}

/* 3. 满载→全释放→再满载：容量不能缩水（无块泄漏） */
static void test_recycle_no_leak(void) {
    test_case("满释放后容量不缩水");

    enum { BLOCKS = 64, N = 500 };
    Parena *a = parena_new(BLOCKS * PA_BLOCK_SIZE);
    if (!a) { test_expect("arena 创建成功", 0, 1); return; }

    static void *ptrs[N];
    size_t ok1 = 0;
    for (size_t i = 0; i < N; i++) {
        ptrs[i] = parena_alloc_slot(a, 100);   /* -> L3 (127) */
        if (!ptrs[i]) break;
        ok1++;
    }

    PaUsage u;
    pa_usage_collect(a, &u);
    pa_usage_print("peak (before free)", &u);
    test_expect("峰值为分配全部成功", (i64)ok1, N);

    for (size_t i = 0; i < ok1; i++)
        parena_recycle_slot(a, ptrs[i]);

    pa_usage_collect(a, &u);
    pa_usage_print("after full free", &u);
    test_expect("全释放后 used_slots == 0", (i64)u.used_slots, 0);

    /* 再分配同样数量，必须全部成功（否则说明块被泄漏或 keep_alive 过度占用） */
    size_t ok2 = 0;
    for (size_t i = 0; i < N; i++) {
        ptrs[i] = parena_alloc_slot(a, 100);
        if (!ptrs[i]) break;
        ok2++;
    }
    test_expect("二次分配容量不缩水", (i64)ok2, (i64)ok1);

    for (size_t i = 0; i < ok2; i++)
        parena_recycle_slot(a, ptrs[i]);

    parena_free(a);
}

/* 4. 内部碎片报告：已用 slot 字节 vs 用户真实请求字节 */
static void test_fragmentation_report(void) {
    test_case("内部碎片/利用率报告");

    Parena *a = parena_new(64 * PA_BLOCK_SIZE);
    if (!a) { test_expect("arena 创建成功", 0, 1); return; }

    static const size_t sizes[] = { 20, 100, 300, 600, 1000 };
    enum { NSIZE = sizeof(sizes) / sizeof(sizes[0]), COPIES = 16 };

    static void *ptrs[NSIZE * COPIES];
    size_t got = 0;
    u64 requested_bytes = 0;

    for (int s = 0; s < NSIZE; s++) {
        for (int c = 0; c < COPIES; c++) {
            void *p = parena_alloc_slot(a, sizes[s]);
            if (!p) break;
            ptrs[got++] = p;
            requested_bytes += sizes[s];
        }
    }

    PaUsage u;
    pa_usage_collect(a, &u);
    pa_usage_print("fragmentation", &u);

    u64 overhead = u.used_slot_bytes >= requested_bytes
                 ? u.used_slot_bytes - requested_bytes : 0;
    printf("  requested=%llu  slot_used=%llu  overhead=%llu (%.2f%%)  efficiency=%.2f%%\n",
           (unsigned long long)requested_bytes,
           (unsigned long long)u.used_slot_bytes,
           (unsigned long long)overhead,
           pct(overhead, requested_bytes),
           pct(requested_bytes, u.used_slot_bytes));

    test_expect("统计 used_slots 与分配数一致", (i64)u.used_slots, (i64)got);
    test_expect("slot_used >= 真实请求字节",
                (i64)(u.used_slot_bytes >= requested_bytes), 1);
    test_expect("slot_used 不超过 slot 总容量",
                (i64)(u.used_slot_bytes <= u.slot_bytes), 1);

    for (size_t i = 0; i < got; i++)
        parena_recycle_slot(a, ptrs[i]);
    parena_free(a);
}

/* 5. 填满一个 arena，报告峰值使用率，随后全释放并确认可再次填满 */
static void test_fill_report(void) {
    test_case("填满使用率报告");

    enum { BLOCKS = 256, MAXP = 20000 };    /* 1 MiB，L2 最多约 1.4 万槽 */
    Parena *a = parena_new(BLOCKS * PA_BLOCK_SIZE);
    if (!a) { test_expect("arena 创建成功", 0, 1); return; }

    static void *ptrs[MAXP];
    size_t cnt = 0;
    for (size_t i = 0; i < MAXP; i++) {
        void *p = parena_alloc_slot(a, 64); /* -> L2 (73) */
        if (!p) break;
        ptrs[cnt++] = p;
    }

    PaUsage u;
    pa_usage_collect(a, &u);
    pa_usage_print("full", &u);

    test_expect("填满后 used_slots == 分配次数", (i64)u.used_slots, (i64)cnt);
    test_expect("填满后 free_slots == 0", (i64)u.free_slots, 0);

    for (size_t i = 0; i < cnt; i++)
        parena_recycle_slot(a, ptrs[i]);

    pa_usage_collect(a, &u);
    pa_usage_print("drained", &u);
    test_expect("全释放后 used_slots == 0", (i64)u.used_slots, 0);

    /* 再次填满，容量应与第一次一致 */
    size_t cnt2 = 0;
    for (size_t i = 0; i < MAXP; i++) {
        void *p = parena_alloc_slot(a, 64);
        if (!p) break;
        ptrs[cnt2++] = p;
    }
    test_expect("二次填满容量不缩水", (i64)cnt2, (i64)cnt);

    for (size_t i = 0; i < cnt2; i++)
        parena_recycle_slot(a, ptrs[i]);

    parena_free(a);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("=== parena usage / accounting test ===\n");
    printf("sizeof(RefBlockHeader)=%zu  slot_level_count=%zu\n",
           sizeof(RefBlockHeader), slot_level_count);

    test_expect("sizeof(RefBlockHeader) == 40", sizeof(RefBlockHeader), 40);

    test_initial_state();
    test_level_accounting();
    test_recycle_no_leak();
    test_fragmentation_report();
    test_fill_report();

    test_summary();
    return 0;
}
