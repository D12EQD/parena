// 标准测试 for Pbuffer
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include "lib/logger.h"
#include "lib/parena.h"
#include "lib/pbuffer.h"
#include "frame/test_frame.h"

#define ARENA_SIZE  PA_LIMIT_SIZE

static volatile uintptr_t g_sink;

static inline double now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

static void print_buf(const char *name, size_t nops, size_t nbytes, double ns) {
    if (!ns || ns <= 0) return;
    double mbs = nbytes ? (double)nbytes / ns * 1e3 : 0.0;
    pr_cont_debug("%-52s %10zu ops  %10.2f ms  %9.2f ns/op  %8.1f MB/s\n",
        name, nops, ns / 1e6,
        nops ? ns / (double)nops : 0.0, mbs);
}

static u8 g_src[4 * 1024 * 1024];
static u8 g_dst[4 * 1024 * 1024];

static void fill_pattern(u8 *p, size_t n, u8 seed) {
    for (size_t i = 0; i < n; i++) p[i] = (u8)(seed + (i * 31));
}

static bool check_pattern(const u8 *p, size_t n, u8 seed) {
    for (size_t i = 0; i < n; i++)
        if (p[i] != (u8)(seed + (i * 31))) return false;
    return true;
}

// ==================== 正确性 ====================

void test_buf_basic(void) {
    test_case("buffer 基本写读");
    Parena *pa = parena_new(PA_LIMIT_SIZE);
    test_expect("arena 创建", pa != NULL);
    if (!pa) return;

    Pbuffer b;
    test_expect("init(100)", pbuffer_init(&b, pa, 100));
    test_expect("cap == 100", b.cap == 100);
    test_expect("count 全 0", b.write_count == 0 && b.read_count == 0);
    test_expect("off 全 0", b.write_off == 0 && b.read_off == 0);
    test_expect("write_ptr 已定位", b.write_ptr != NULL);
    test_expect("read_ptr 已定位", b.read_ptr != NULL);

    u8 src[50]; fill_pattern(src, 50, 1);
    test_expect("写 50B", pbuffer_write(&b, src, 50) == 50);
    test_expect("write_count == 50", b.write_count == 50);

    u8 peeked[50];
    test_expect("peek 50B", pbuffer_peek(&b, peeked, 50) == 50);
    test_expect("peek 内容正确", check_pattern(peeked, 50, 1));
    test_expect("peek 不动 read_count", b.read_count == 0);

    u8 got[50];
    test_expect("read 50B", pbuffer_read(&b, got, 50) == 50);
    test_expect("read 内容正确", check_pattern(got, 50, 1));
    test_expect("read_count == 50", b.read_count == 50);

    test_expect("读空返回 0", pbuffer_read(&b, got, 10) == 0);

    pbuffer_deinit(&b);
    parena_free(pa);
}

void test_buf_cross_block(void) {
    test_case("buffer 跨块读写");
    Parena *pa = parena_new(PA_LIMIT_SIZE);
    if (!pa) { test_note("arena 创建失败"); return; }

    enum { N = 40000 };
    fill_pattern(g_src, N, 3);

    Pbuffer b;
    test_expect("init(16) 写 40KB 应 grow", pbuffer_init(&b, pa, 16));
    test_expect("写 40KB", pbuffer_write(&b, g_src, N) == N);
    test_expect("cap >= 40KB", b.cap >= N);

    test_expect("peek 40KB", pbuffer_peek(&b, g_dst, N) == N);
    test_expect("peek 跨块内容正确", check_pattern(g_dst, N, 3));

    test_expect("read 40KB", pbuffer_read(&b, g_dst, N) == N);
    test_expect("read 跨块内容正确", check_pattern(g_dst, N, 3));
    test_expect("读空返回 0", pbuffer_read(&b, g_dst, 1) == 0);

    pbuffer_deinit(&b);
    parena_free(pa);
}

void test_buf_peek_no_advance(void) {
    test_case("buffer peek 不推进读游标");
    Parena *pa = parena_new(PA_LIMIT_SIZE);
    if (!pa) return;

    Pbuffer b;
    pbuffer_init(&b, pa, 500);
    u8 src[100]; fill_pattern(src, 100, 7);
    pbuffer_write(&b, src, 100);

    u64 rc = b.read_count;
    for (int i = 0; i < 10; i++) {
        pbuffer_peek(&b, g_dst, 100);
    }
    test_expect("10 次 peek 后 read_count 不变", b.read_count == rc);

    u8 got[100];
    test_expect("read 内容正确", pbuffer_read(&b, got, 100) == 100);
    test_expect("read 内容跨多次 peek 后仍正确", check_pattern(got, 100, 7));

    pbuffer_deinit(&b);
    parena_free(pa);
}

void test_buf_extend_limit(void) {
    test_case("buffer extend 增加 cap");
    Parena *pa = parena_new(PA_LIMIT_SIZE);
    if (!pa) return;

    Pbuffer b;
    pbuffer_init(&b, pa, 100);
    test_expect("cap == 100", b.cap == 100);

    u8 src[200]; fill_pattern(src, 200, 11);
    test_expect("写 200B", pbuffer_write(&b, src, 200) == 200);
    test_expect("cap >= 200", b.cap >= 200);

    test_expect("回读 200B", pbuffer_read(&b, g_dst, 200) == 200);
    test_expect("回读内容正确", check_pattern(g_dst, 200, 11));

    pbuffer_deinit(&b);
    parena_free(pa);
}

void test_buf_new_free(void) {
    test_case("buffer new / free 归还");
    Parena *pa = parena_new(PA_LIMIT_SIZE);
    if (!pa) return;

    u64 empty_before = pa->empty_block_count;

    Pbuffer *b = pbuffer_new(pa, 500);
    test_expect("new", b != NULL);
    if (b) {
        u8 src[500]; fill_pattern(src, 500, 11);
        test_expect("写入", pbuffer_write(b, src, 500) == 500);
        pbuffer_free(b);
    }

    test_expect("free 后 block 全归还", pa->empty_block_count == empty_before);
    parena_free(pa);
}

// ==================== 性能 ====================

static void bench_small_chunk_write(void) {
    enum { CAP = 4 * 1024 * 1024, CHUNK = 16, ROUNDS = 20 };
    size_t writes_per_round = CAP / CHUNK;
    size_t total_ops = (size_t)ROUNDS * writes_per_round;
    size_t total_bytes = total_ops * CHUNK;

    Parena *pa = parena_new(PA_LIMIT_SIZE);
    if (!pa) return;

    Pbuffer b;
    pbuffer_init(&b, pa, CAP);
    fill_pattern(g_src, CHUNK, 1);

    // 预热
    for (int r = 0; r < 2; r++) {
        for (size_t i = 0; i < writes_per_round; i++)
            pbuffer_write(&b, g_src, CHUNK);
        pbuffer_reset(&b);
        pbuffer_init(&b, pa, CAP);
    }

    double t0 = now_ns();
    for (int r = 0; r < ROUNDS; r++) {
        for (size_t i = 0; i < writes_per_round; i++)
            pbuffer_write(&b, g_src, CHUNK);
        pbuffer_reset(&b);
        if (r + 1 < ROUNDS) pbuffer_init(&b, pa, CAP);
    }
    double t1 = now_ns();

    char lbl[128];
    snprintf(lbl, sizeof(lbl), "A  small chunk [16B] write");
    print_buf(lbl, total_ops, total_bytes, t1 - t0);

    pbuffer_deinit(&b);
    parena_free(pa);
}

static void bench_large_chunk_write(void) {
    enum { CAP = 4 * 1024 * 1024, CHUNK = 4096, ROUNDS = 100 };
    size_t writes_per_round = CAP / CHUNK;
    size_t total_ops = (size_t)ROUNDS * writes_per_round;
    size_t total_bytes = total_ops * CHUNK;

    Parena *pa = parena_new(PA_LIMIT_SIZE);
    if (!pa) return;

    Pbuffer b;
    pbuffer_init(&b, pa, CAP);
    fill_pattern(g_src, CHUNK, 2);

    for (int r = 0; r < 2; r++) {
        for (size_t i = 0; i < writes_per_round; i++)
            pbuffer_write(&b, g_src, CHUNK);
        pbuffer_reset(&b);
        pbuffer_init(&b, pa, CAP);
    }

    double t0 = now_ns();
    for (int r = 0; r < ROUNDS; r++) {
        for (size_t i = 0; i < writes_per_round; i++)
            pbuffer_write(&b, g_src, CHUNK);
        pbuffer_reset(&b);
        if (r + 1 < ROUNDS) pbuffer_init(&b, pa, CAP);
    }
    double t1 = now_ns();

    char lbl[128];
    snprintf(lbl, sizeof(lbl), "B  large chunk [4KB] write");
    print_buf(lbl, total_ops, total_bytes, t1 - t0);

    pbuffer_deinit(&b);
    parena_free(pa);
}

static void bench_cross_block_write(void) {
    enum { CAP = 4 * 1024 * 1024, ROUNDS = 20 };
    size_t CHUNK = PA_BLOCK_SIZE - sizeof(PchainHead) + 7;
    size_t writes_per_round = CAP / CHUNK;
    size_t total_ops = (size_t)ROUNDS * writes_per_round;
    size_t total_bytes = total_ops * CHUNK;

    Parena *pa = parena_new(PA_LIMIT_SIZE);
    if (!pa) return;

    Pbuffer b;
    pbuffer_init(&b, pa, CAP);
    fill_pattern(g_src, CHUNK < sizeof(g_src) ? CHUNK : sizeof(g_src), 4);

    for (int r = 0; r < 2; r++) {
        for (size_t i = 0; i < writes_per_round; i++)
            pbuffer_write(&b, g_src, CHUNK);
        pbuffer_reset(&b);
        pbuffer_init(&b, pa, CAP);
    }

    double t0 = now_ns();
    for (int r = 0; r < ROUNDS; r++) {
        for (size_t i = 0; i < writes_per_round; i++)
            pbuffer_write(&b, g_src, CHUNK);
        pbuffer_reset(&b);
        if (r + 1 < ROUNDS) pbuffer_init(&b, pa, CAP);
    }
    double t1 = now_ns();

    char lbl[128];
    snprintf(lbl, sizeof(lbl), "C  cross-block write [%zuB]", CHUNK);
    print_buf(lbl, total_ops, total_bytes, t1 - t0);

    pbuffer_deinit(&b);
    parena_free(pa);
}

static void bench_steady_rw(void) {
    enum { CAP = 1024 * 1024, CHUNK = 1024, ITERS = 200 * 1000 };
    enum { WATERMARK = 512 * 1024 };

    Parena *pa = parena_new(PA_LIMIT_SIZE);
    if (!pa) return;

    Pbuffer b;
    pbuffer_init(&b, pa, CAP);
    fill_pattern(g_src, CHUNK, 3);

    for (size_t w = 0; w < WATERMARK; w += CHUNK)
        pbuffer_write(&b, g_src, CHUNK);

    double t0 = now_ns();
    for (size_t i = 0; i < ITERS; i++) {
        pbuffer_write(&b, g_src, CHUNK);
        pbuffer_read(&b, g_dst, CHUNK);
    }
    double t1 = now_ns();
    g_sink = g_dst[0];

    print_buf("D  steady write+read [1KB]",
              (size_t)ITERS * 2, (size_t)ITERS * CHUNK * 2, t1 - t0);

    pbuffer_deinit(&b);
    parena_free(pa);
}

static void bench_peek_vs_read(void) {
    enum { CAP = 1024 * 1024, CHUNK = 1024, ITERS = 200 * 1000 };

    Parena *pa = parena_new(PA_LIMIT_SIZE);
    if (!pa) return;

    Pbuffer b;
    pbuffer_init(&b, pa, CAP);
    fill_pattern(g_src, CHUNK, 3);

    for (size_t i = 0; i < CAP; i += CHUNK)
        pbuffer_write(&b, g_src, CHUNK);

    // peek: 不动 read_count
    double t0 = now_ns();
    for (size_t i = 0; i < ITERS; i++)
        pbuffer_peek(&b, g_dst, CHUNK);
    double t1 = now_ns();
    print_buf("E1 peek [1KB, read_count fixed]",
              ITERS, (size_t)ITERS * CHUNK, t1 - t0);

    // read: 每次读满就 rewind 到起点
    t0 = now_ns();
    for (size_t i = 0; i < ITERS; i++) {
        pbuffer_read(&b, g_dst, CHUNK);
        if (b.read_count >= CAP) {
            b.read_count = 0;
            b.read_off = 0;
            b.read_ptr = list_get_front(&b.chain.list);
        }
    }
    t1 = now_ns();
    print_buf("E2 read [1KB, rewind on full]",
              ITERS, (size_t)ITERS * CHUNK, t1 - t0);

    pbuffer_deinit(&b);
    parena_free(pa);
}

static void bench_extend_grow(void) {
    enum { CAP_FINAL = 1024 * 1024, INIT = 4096, ROUNDS = 2000 };

    Parena *pa = parena_new(PA_LIMIT_SIZE);
    if (!pa) return;

    Pbuffer b;
    fill_pattern(g_src, INIT, 6);

    size_t total_ops = 0, total_bytes = 0;

    for (int r = 0; r < 5; r++) {
        pbuffer_init(&b, pa, INIT);
        for (size_t w = INIT; w < CAP_FINAL; w *= 2)
            pbuffer_write(&b, g_src, INIT);
        pbuffer_deinit(&b);
    }

    double t0 = now_ns();
    for (int r = 0; r < ROUNDS; r++) {
        pbuffer_init(&b, pa, INIT);
        for (size_t w = INIT; w < CAP_FINAL; w *= 2) {
            pbuffer_write(&b, g_src, INIT);
            total_ops++;
            total_bytes += INIT;
        }
        pbuffer_deinit(&b);
    }
    double t1 = now_ns();

    char lbl[128];
    snprintf(lbl, sizeof(lbl), "F  extend grow [%dB -> %dB x %d]", INIT, CAP_FINAL, ROUNDS);
    print_buf(lbl, total_ops, total_bytes, t1 - t0);

    parena_free(pa);
}

void test_buf_backup(void) {
    pr_cont_debug("=== pbuffer benchmark ===\n\n");
    bench_small_chunk_write();
    bench_large_chunk_write();
    bench_cross_block_write();
    bench_steady_rw();
    bench_peek_vs_read();
    bench_extend_grow();
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);

    // 正确性
    test_buf_basic();
    test_buf_cross_block();
    test_buf_peek_no_advance();
    test_buf_extend_limit();
    test_buf_new_free();

    // 性能
    test_buf_backup();

    test_summary();
    return 0;
}
