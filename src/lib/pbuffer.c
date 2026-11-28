#include "pbuffer.h"
#include "lib/logger.h"
#include <string.h>


/* 从 src 读入数据，从 start 开始的 chain 逻辑区间 [off , off + n) */

static inline void chain_copy_out(PchainHead **start, u64 *off, void *dst, size_t n) {
    ASSERT(start && *start);
    while (n) {
        if (*off == PCHAIN_BLOCK_SIZE) {
            *start = (void *)((*start)->node.next);
            *off   = 0;
        }

        ASSERT(start && *start);

        u64 space = PCHAIN_BLOCK_SIZE - *off;
        u64 cost  = min(n, space);

        memcpy(dst, chain_block_data(*start) + *off, cost);
        n -= cost;
        dst += cost;
        *off += cost;
    }
}

/* 把 src 写入从 start 开始的 chain 的逻辑区间 [off, off+n) */
static inline void chain_copy_in(PchainHead **start, u64 *off, const void *src, size_t n) {
    ASSERT(start && *start);
    while (n) {
        if (*off == PCHAIN_BLOCK_SIZE) {
            *start = (void *)(*start)->node.next;
            *off   = 0;
        }

        ASSERT(start && *start); // out of range

        u64 space = PCHAIN_BLOCK_SIZE - *off;
        u64 cost  = min(n, space);

        memcpy(chain_block_data(*start) + *off, src, cost);

        src += cost;
        *off += cost;
        n -= cost;
    }
}

/* ==================== 构造 / 销毁 ==================== */
bool pbuffer_init_imple(Pbuffer *b, Parena *pa, size_t size) {
    if (unlikely(!b || !pa))
        return false;

    if (!size)
        return false;

    memset(b, 0, sizeof(*b));
    pchain_init(pa, &b->chain);

    if (!pchain_append(&b->chain, size)) {
        return false;
    }

    b->cap       = size;
    b->write_ptr = list_get_front(&b->chain.list);
    b->read_ptr  = b->write_ptr;

    ASSERT(b->write_ptr && b->read_ptr);
    return true;
}

Pbuffer *pbuffer_new_imple(Parena *pa, size_t size) {
    if (!pa)
        return NULL;

    Pbuffer *b = parena_alloc_slot(pa, sizeof(Pbuffer));
    if (!b)
        return NULL;

    if (!pbuffer_init_imple(b, pa, size)) {
        parena_recycle_slot(b);
        return NULL;
    }

    return b;
}

/* ==================== 扩容 ==================== */

/* 需要 extra 字节数量的内存 */
bool pbuffer_extend(Pbuffer *b, size_t extra) {
    if (unlikely(!b))
        return false;
    if (b->cap >= b->write_count + extra)
        return true;

    u64 need = b->write_count + extra;
    u64 add  = need - b->cap;
    if (!pchain_append(&b->chain, (size_t)add))
        return false;

    b->cap += add;
    return true;
}

/* ==================== 写 / 读 ==================== */

/* 类似 memcpy，Pbuffer 会自动扩容 */
size_t pbuffer_write(Pbuffer *b, const void *src, size_t n) {
    if (unlikely(!b || !src || !n))
        return 0;

    if (b->write_count + n > b->cap) {
        pbuffer_extend(b, n);
        if (b->write_count + n > b->cap) {
            if (b->write_count >= b->cap)
                return 0;
            n = (size_t)(b->cap - b->write_count); /* 截断 */
        }
    }

    chain_copy_in(&b->write_ptr, &b->write_off, src, n);
    b->write_count += n;
    return n;
}

/* 类似 memcpy */
size_t pbuffer_read(Pbuffer *b, void *dst, size_t n) {
    if (unlikely(!b || !dst || !n))
        return 0;

    u64 avail = b->write_count - b->read_count;
    if (n > avail)
        n = (size_t)avail;
    if (!n)
        return 0;

    chain_copy_out(&b->read_ptr, &b->read_off, dst, n);
    b->read_count += n;
    return n;
}

/* 查看字节并且拷贝，不移动读指针 */
size_t pbuffer_peek(Pbuffer *b, void *dst, size_t n) {
    if (unlikely(!b || !dst || !n))
        return 0;

    u64 avail = b->write_count - b->read_count;
    if (n > avail)
        n = (size_t)avail;
    if (!n)
        return 0;

    // 用局部副本调 chain_copy_out
    PchainHead *ptr = b->read_ptr;
    u64         off = b->read_off;
    if (!ptr) {
        ptr = list_get_front(&b->chain.list);
        off = 0;
        if (!ptr)
            return 0;
    }

    chain_copy_out(&ptr, &off, dst, n);
    return n;
}

/* ==================== 复位 / 释放 ==================== */

/* buffer 重置，清空所有内存 */
void pbuffer_reset(Pbuffer *b) {
    if (!b)
        return;

    Parena *pa = b->chain.parena;
    pchain_recycle(&b->chain);
    pchain_init(pa, &b->chain);
    pchain_append(&b->chain, PCHAIN_BLOCK_SIZE); // 默认分配一个 BLOCK

    b->cap         = 0;
    b->write_count = 0;
    b->read_count  = 0;
    b->write_off   = 0;
    b->read_off    = 0;
    b->write_ptr   = list_get_front(&b->chain.list);
    b->read_ptr    = b->write_ptr;
}

/* 
 * 回收Buffer，该Buffer应该为用户自己分配的结构体，
 * 其读写内存还是从Parena得到的
 */
void pbuffer_deinit(Pbuffer *b) {
    if (!b)
        return;

    if (b->chain.parena)
        pchain_recycle(&b->chain);

    b->write_ptr = NULL;
    b->read_ptr  = NULL;
}

/* 回收buffer，该 buffer 结构体应该从 Parena 中分配得到的内存 */
void pbuffer_free(Pbuffer *b) {
    if (!b)
        return;

    Parena *pa = b->chain.parena;
    pbuffer_deinit(b);
    if (pa) parena_recycle_slot(b);
}
