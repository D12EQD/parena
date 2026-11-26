#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include "lib/logger.h"
#include "lib/parena.h"
#include "frame/test_frame.h"

#define ARENA_SIZE      PA_LIMIT_SIZE
#define WARMUP_ITERS    200000
#define PURE_MAX        3000000       /* 纯分配上限，实际由 arena 容量限制 */
#define STEADY_ITERS    50000000      /* 每轮 alloc+free 算 2 次 op */
#define BURST_N         1000          /* 模拟 1000 个连接的突发 */
#define BURST_ROUNDS    20000

static volatile uintptr_t g_sink;

static inline double now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

static void pr(const char *name, size_t nops, double ns) {
    if (!nops || ns <= 0) return;
    pr_cont_debug("%-52s %10zu ops  %10.2f ms  %9.2f ns/op  %9.2f Mops/s\n",
           name, nops, ns / 1e6,
           ns / (double)nops,
           (double)nops / ns * 1e3);
}

/* 
 *  A. 
 *  纯分配：一次分配 N 个，不释放；然后再逐个释放
 *  测的是"全新 arena 的原始分配吞吐" 
 */
static void test_pure_alloc(size_t size, const char *tag) {
    Parena *a = parena_new(PA_LIMIT_SIZE);
    if (!a) { fprintf(stderr, "parena_new failed\n"); return; }

    void **ptrs = calloc(PURE_MAX, sizeof(void *));
    if (!ptrs) { 
        parena_free(a); 
        return; 
    }

    /* 预热：把块内 freelist 头部拉进 cache */
    for (size_t i = 0; i < WARMUP_ITERS; i++) {
        void *t = parena_alloc_slot(a, size);
        if (i == 32766){
            break;
        }
        if (t) parena_recycle_slot(a, t);
    }

    double t0 = now_ns();
    size_t ok = 0;
    for (size_t i = 0; i < PURE_MAX; i++) {
        ptrs[i] = parena_alloc_slot(a, size);
        if (!ptrs[i]) break;
        ok++;
    }
    double t1 = now_ns();
    if (ok) g_sink = (uintptr_t)ptrs[0];

    char lbl[128];
    snprintf(lbl, sizeof(lbl), "A1 pure alloc [%-5s size=%4zu]", tag, size);
    pr(lbl, ok, t1 - t0);
    if (ok < PURE_MAX)
        pr_cont_debug("   (arena exhausted after %zu allocs)\n", ok);

    t0 = now_ns();
    for (size_t i = 0; i < ok; i++)
        if (ptrs[i]) parena_recycle_slot(a, ptrs[i]);
    t1 = now_ns();
    snprintf(lbl, sizeof(lbl), "A2 pure free  [%-5s size=%4zu]", tag, size);
    pr(lbl, ok, t1 - t0);

    free(ptrs);
    parena_free(a);
}

/* B. 稳态：先填满 n_target，然后交替 free→alloc，始终维持 n_target 个存活 */
static void test_steady(size_t size, const char *tag, size_t n_target, size_t iters) {
    Parena *a = parena_new(PA_LIMIT_SIZE);

    if (!a) return;

    void **live = calloc(n_target, sizeof(void *));
    if (!live) { parena_free(a); return; }

    size_t filled = 0;
    for (size_t i = 0; i < n_target; i++) {
        live[i] = parena_alloc_slot(a, size);
        if (!live[i]) break;
        filled++;
    }
    if (!filled) { free(live); parena_free(a); return; }

    /* 预热 */
    for (size_t i = 0; i < WARMUP_ITERS && i < filled; i++) {
        parena_recycle_slot(a, live[i]);
        live[i] = parena_alloc_slot(a, size);
    }

    double t0 = now_ns();
    size_t idx = 0;
    for (size_t i = 0; i < iters; i++) {
        parena_recycle_slot(a, live[idx]);
        live[idx] = parena_alloc_slot(a, size);
        if (++idx >= filled) idx = 0;
    }

    double t1 = now_ns();
    if (filled) g_sink = (uintptr_t)live[0];

    char lbl[128];
    snprintf(lbl, sizeof(lbl), "B  steady a+f [%-5s size=%4zu n=%zu]", tag, size, filled);
    pr(lbl, iters * 2, t1 - t0);

    for (size_t i = 0; i < filled; i++)
        if (live[i]) parena_recycle_slot(a, live[i]);
    free(live);
    parena_free(a);
}

/* C. 突发：一次性 alloc N 个、一次性释放，重复 R 轮 */
static void test_burst(size_t size, const char *tag) {
    Parena *a = parena_new(PA_LIMIT_SIZE);
    if (!a) return;

    void **arr = calloc(BURST_N, sizeof(void *));
    if (!arr) { parena_free(a); return; }

    /* 预热 */
    for (int w = 0; w < 200; w++) {
        for (size_t i = 0; i < BURST_N; i++) arr[i] = parena_alloc_slot(a, size);
        for (size_t i = 0; i < BURST_N; i++) {
            if (arr[i]) parena_recycle_slot(a, arr[i]);
            arr[i] = NULL;
        }
    }

    double t0 = now_ns();
    for (int r = 0; r < BURST_ROUNDS; r++) {
        for (size_t i = 0; i < BURST_N; i++) arr[i] = parena_alloc_slot(a, size);
        for (size_t i = 0; i < BURST_N; i++) {
            if (arr[i]) parena_recycle_slot(a, arr[i]);
            arr[i] = NULL;
        }
    }
    double t1 = now_ns();
    if (BURST_N) g_sink = (uintptr_t)arr[0];

    char lbl[128];
    snprintf(lbl, sizeof(lbl), "C  burst      [%-5s size=%4zu N=%d]", tag, size, BURST_N);
    pr(lbl, (size_t)BURST_ROUNDS * BURST_N * 2, t1 - t0);

    free(arr);
    parena_free(a);
}

/* D. 混合大小：按权重模拟真实服务器对象分布 */
static void test_mixed(size_t iters) {
    static const struct { size_t size; int weight; } dist[] = {
        {16,   40}, {32,   25}, {64,   15}, {128,  10},
        {256,   5}, {512,   3}, {1024,  1}, {2048,  1},
    };
    enum { NDIST = sizeof(dist) / sizeof(dist[0]) };
    enum { SLOTS = 4096 };

    Parena *a = parena_new(PA_LIMIT_SIZE);
    if (!a) return;

    void *ring[SLOTS] = {0};

    /* 加权随机 */
    #define PICK_SIZE(dst) do {                                       \
        int _r = rand() % 100, _acc = 0;                              \
        (dst) = dist[0].size;                                         \
        for (int _k = 0; _k < NDIST; _k++) {                          \
            _acc += dist[_k].weight;                                  \
            if (_r < _acc) { (dst) = dist[_k].size; break; }          \
        }                                                             \
    } while (0)

    /* 预热填满 */
    for (size_t i = 0; i < SLOTS; i++) {
        size_t sz; PICK_SIZE(sz);
        ring[i] = parena_alloc_slot(a, sz);
    }

    double t0 = now_ns();
    size_t ops = 0;
    for (size_t i = 0; i < iters; i++) {
        size_t slot = i % SLOTS;
        if (ring[slot]) {
            parena_recycle_slot(a, ring[slot]);
            ops++;
        }
        size_t sz; PICK_SIZE(sz);
        ring[slot] = parena_alloc_slot(a, sz);
        ops++;
    }
    double t1 = now_ns();
    #undef PICK_SIZE

    pr("D  mixed size alloc+free", ops, t1 - t0);

    for (size_t i = 0; i < SLOTS; i++)
        if (ring[i]) parena_recycle_slot(a, ring[i]);
    parena_free(a);
}

/* E. 遍历所有 size class：逐个 class 跑一段稳态 */
static void test_all_classes(void) {
    static const size_t sizes[] = {
        16, 24, 32, 48, 64, 96, 128, 192,
        256, 384, 512, 768, 1024, 1536, 2048, 3072
    };
    enum { NSIZE = sizeof(sizes) / sizeof(sizes[0]) };

    for (int s = 0; s < NSIZE; s++) {
        char tag[16];
        snprintf(tag, sizeof(tag), "sc%-4zu", sizes[s]);
        test_steady(sizes[s], tag, 50000, 30000000);
    }
}

void *buffer[100000];

void test_mem_leak(){
    Parena * pa = parena_new(0);
    int top_size = -1;

    for (int i = 0; i < 10000; i ++){
        int idx = 0;
        while (1){
            buffer[idx] = parena_alloc_slot(pa, 510);
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
            idx ++;
        }

        for (int j = 0; j <= idx; j ++){
            parena_recycle_slot(pa, buffer[j]);
        }
    }

    test_note("内存未检测泄露");
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    srand(0x1234);   /* D 用固定 seed，保证可复现 */

    pr_cont_debug("=== parena single-thread benchmark (addr-only) ===\n");
    pr_cont_debug("arena=%dMB  pure_max=%d  steady=%d  burst=%dx%d\n\n",
           ARENA_SIZE, PURE_MAX, STEADY_ITERS, BURST_ROUNDS, BURST_N);

    pr_cont_debug("--- A. Pure alloc/free (cold, no interleave) ---\n");
    test_pure_alloc(16,  "tiny");
    test_pure_alloc(64,  "mid");
    test_pure_alloc(512, "big");

    pr_cont_debug("\n--- B. Steady state (alloc+free pair, live set fixed) ---\n");
    test_steady(16,  "tiny", 500000, STEADY_ITERS);
    test_steady(64,  "mid",  200000, STEADY_ITERS);
    test_steady(512, "big",  100000, STEADY_ITERS);

    pr_cont_debug("\n--- C. Burst (alloc N, free N, repeat) ---\n");
    test_burst(16,  "tiny");
    test_burst(64,  "mid");
    test_burst(512, "big");

    pr_cont_debug("\n--- D. Mixed size workload ---\n");
    test_mixed(20000000);

    pr_cont_debug("\n--- E. All size classes steady ---\n");
    test_all_classes();

    // 耗时过长
    // pr_cont_debug("\n--- F. test_mem_leak test_mem_leak ---\n");
    // test_mem_leak();

    pr_cont_debug("\nsink=%lx\n", (unsigned long)g_sink);
    return 0;
}
