#pragma once
#include "parena.h"

/*
 * Pbuffer 有两种底层：
 *   PbufferSlot  -- 从 Parena 申请一个固定 slot，容量不可变
 *   PbufferChain -- 按 Block 串成 Pchain，容量可在上限内增长
 *
 * 写入永远是追加（写到 count），读取从 offset 开始消费。
 */
typedef enum PbufferKind {
    PbufferSlot = 0,        // 单块固定容量
    PbufferChain,           // 块链，可增长
} PbufferKind;

/*
 * grow      是否允许写满后动态扩容
 * max_size  扩容上限；grow=1 且 max_size=0 表示不设上限（受 Parena 剩余块限制）
 * 默认（零初始化）：grow=0，容量就是申请时给的 size。
 */
typedef struct {
    size_t max_size;
    bool   grow;
} PbufferOpt;

typedef struct Pbuffer_s {
    union {
        void   *slot_ptr;   // PbufferSlot
        Pchain  chain;      // PbufferChain
    } u;

    Parena *pa;             // 所属 Parena（也是结构体本身的来源）
    u64 cap;                // 已提交容量（字节）
    u64 count;              // 已写入字节数
    u64 offset;             // 已消费字节数
    u64 max_size;           // 不限制，0表示不限制

    u8 kind : 4;
    u8 grow : 4;
} Pbuffer;

Pbuffer *pbuffer_new_imple(Parena *pa, PbufferKind kind, size_t size, PbufferOpt opt);
bool pbuffer_init_imple(Pbuffer *b, Parena *pa, PbufferKind kind, size_t size, PbufferOpt opt);

#define pbuffer_new(pa, kind, size, ...) \
    pbuffer_new_imple((pa), (kind), (size), (PbufferOpt){ __VA_OPT__(__VA_ARGS__) })

#define pbuffer_init(b, pa, kind, size, ...) \
    pbuffer_init_imple((b), (pa), (kind), (size), (PbufferOpt){ __VA_OPT__(__VA_ARGS__) })

/* 可读 / 可写字节数 */
static inline u64 pbuffer_len(Pbuffer *b){
    return b->count - b->offset;
}

static inline u64 pbuffer_space(Pbuffer *b){
    return b->cap - b->count;
}

/*
 * 追加写：返回实际写入的字节数。
 *   - slot  : 写满就截断
 *   - chain : grow=1 时先扩容到 max_size，仍不够则截断
 */
size_t pbuffer_write(Pbuffer *b, const void *src, size_t n);

/* 消费读：推进 offset，返回实际读出字节数 */
size_t pbuffer_read(Pbuffer *b, void *dst, size_t n);

/* 只读不推进 offset */
size_t pbuffer_peek(Pbuffer *b, void *dst, size_t n);

/*
 * 保证还能再写 extra 字节（即 cap >= count + extra）。
 * 仅 chain 且 grow=1 时可能真正扩容；返回最终是否满足。
 */
bool pbuffer_extend(Pbuffer *b, size_t extra);

/* 清空内容：chain 会把块还给 Parena，slot 只重置读写位置 */
void pbuffer_reset(Pbuffer *b);

/* 只释放底层内存，保留 Pbuffer 结构体本身（可用于栈上结构体） */
void pbuffer_deinit(Pbuffer *b);

/* deinit + 把结构体还给 Parena；只用于 pbuffer_new 创建的 Pbuffer */
void pbuffer_free(Pbuffer *b);
