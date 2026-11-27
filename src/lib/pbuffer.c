#include "pbuffer.h"
#include "lib/logger.h"
#include <string.h>

/* ==================== chain 底层块访问 ==================== */

/* 块首部是 PchainHead（链表节点），数据区紧跟其后 */
static inline u8 *chain_block_data(void *node){
    return (u8 *)node + sizeof(PchainHead);
}

/*
 *  把 chain 逻辑区间 [off, off+n) 拷出到 dst。
 *  要求 off + n <= 已提交容量，调用方保证。
 */
static inline void chain_copy_out(Pchain *pc, u64 off, void *dst, size_t n){
    ListNode *blk = pc->list.begin;
    u8 *out = (u8 *)dst;

    while (blk && off >= PCHAIN_BLOCK_SIZE){
        blk = blk->next;
        off -= PCHAIN_BLOCK_SIZE;
    }

    while (n){
        if (!blk){
            PANIC("pbuffer chain_copy_out 越界");
            return;
        }
        size_t chunk = (size_t)min((u64)n, (u64)PCHAIN_BLOCK_SIZE - off);
        memcpy(out, chain_block_data(blk) + off, chunk);
        out += chunk;
        n   -= chunk;
        off  = 0;
        blk  = blk->next;
    }
}

/* 把 src 写入 chain 的逻辑区间 [off, off+n) */
static inline void chain_copy_in(Pchain *pc, u64 off, const void *src, size_t n){
    ListNode *blk = pc->list.begin;
    const u8 *in = (const u8 *)src;

    while (blk && off >= PCHAIN_BLOCK_SIZE){
        blk = blk->next;
        off -= PCHAIN_BLOCK_SIZE;
    }

    while (n){
        if (!blk){
            PANIC("pbuffer chain_copy_in 越界");
            return;
        }
        size_t chunk = (size_t)min((u64)n, (u64)PCHAIN_BLOCK_SIZE - off);
        memcpy(chain_block_data(blk) + off, in, chunk);
        in  += chunk;
        n   -= chunk;
        off  = 0;
        blk  = blk->next;
    }
}

static inline u64 slot_size_at(Parena *pa, u64 lv){
    return *(u64 *)parray_get(&pa->slot_level, lv);
}

/* ==================== 构造 ==================== */

bool pbuffer_init_imple(Pbuffer *b, Parena *pa, PbufferKind kind, size_t size, PbufferOpt opt){
    if (unlikely(!b || !pa)) return false;

    memset(b, 0, sizeof(*b));
    b->pa   = pa;
    b->kind = (u8)kind;
    b->grow = opt.grow ? 1 : 0;

    /* 上限不能小于初始容量；grow=0 时上限就是 size */
    b->max_size = opt.max_size;
    if (b->max_size && b->max_size < size) b->max_size = size;
    if (!b->grow) b->max_size = size;

    switch (kind) {
        case PbufferSlot: {
            if (!size) return false;

            u64 lv = parena_get_slot_level(pa, size);
            if (lv >= pa->slot_level.cap) return false;

            void *ptr = parena_alloc_slot(pa, size);
            if (!ptr) return false;

            b->u.slot_ptr = ptr;
            /* slot 实际可用容量是所在等级的大小，而不是请求的 size */
            b->cap = slot_size_at(pa, lv);
            break;
        }
        case PbufferChain:
            pchain_init(pa, &b->u.chain);
            b->cap = 0;
            if (size){
                if (!pchain_append(&b->u.chain, size)){
                    pchain_recycle(&b->u.chain);
                    return false;
                }
                b->cap = size;
            }
            break;
        default:
            return false;
    }

    return true;
}

Pbuffer *pbuffer_new_imple(Parena *pa, PbufferKind kind, size_t size, PbufferOpt opt){
    if (!pa) return NULL;

    Pbuffer *b = parena_alloc_slot(pa, sizeof(Pbuffer));
    if (!b) return NULL;

    if (!pbuffer_init_imple(b, pa, kind, size, opt)){
        parena_recycle_slot(b);
        return NULL;
    }
    return b;
}

/* ==================== 写 / 读 ==================== */

bool pbuffer_extend(Pbuffer *b, size_t extra){
    if (unlikely(!b) || b->kind != PbufferChain) return false;
    if (b->cap >= b->count + extra) return true;
    if (!b->grow) return false;

    u64 want = b->count + extra;
    if (b->max_size && want > b->max_size) want = b->max_size;

    if (want > b->cap){
        u64 add = want - b->cap;
        if (!pchain_append(&b->u.chain, (size_t)add)) return false;
        b->cap += add;
    }

    return b->cap >= b->count + extra;
}

size_t pbuffer_write(Pbuffer *b, const void *src, size_t n){
    if (unlikely(!b || !src || !n)) return 0;

    if (b->count + n > b->cap){
        /* chain + grow 先尝试扩容；slot 或无增长权限则截断 */
        if (b->kind == PbufferChain && b->grow)
            pbuffer_extend(b, n);

        if (b->count + n > b->cap){
            if (b->count >= b->cap) return 0;
            n = (size_t)(b->cap - b->count);
        }
    }

    if (b->kind == PbufferSlot){
        memcpy((u8 *)b->u.slot_ptr + b->count, src, n);
    }else{
        chain_copy_in(&b->u.chain, b->count, src, n);
    }

    b->count += n;
    return n;
}

size_t pbuffer_read(Pbuffer *b, void *dst, size_t n){
    if (unlikely(!b || !dst || !n)) return 0;

    u64 avail = b->count - b->offset;
    if (n > avail) n = (size_t)avail;
    if (!n) return 0;

    if (b->kind == PbufferSlot){
        memcpy(dst, (u8 *)b->u.slot_ptr + b->offset, n);
    }else{
        chain_copy_out(&b->u.chain, b->offset, dst, n);
    }

    b->offset += n;
    return n;
}

size_t pbuffer_peek(Pbuffer *b, void *dst, size_t n){
    if (unlikely(!b || !dst || !n)) return 0;

    u64 avail = b->count - b->offset;
    if (n > avail) n = (size_t)avail;
    if (!n) return 0;

    if (b->kind == PbufferSlot)
        memcpy(dst, (u8 *)b->u.slot_ptr + b->offset, n);
    else
        chain_copy_out(&b->u.chain, b->offset, dst, n);

    return n;
}

/* ==================== 复位 / 释放 ==================== */

void pbuffer_reset(Pbuffer *b){
    if (!b) return;

    if (b->kind == PbufferChain){
        pchain_recycle(&b->u.chain);
        pchain_init(b->pa, &b->u.chain);
        b->cap = 0;
    }

    b->count  = 0;
    b->offset = 0;
}

void pbuffer_deinit(Pbuffer *b){
    if (!b) return;

    if (b->kind == PbufferSlot){
        if (b->u.slot_ptr) parena_recycle_slot(b->u.slot_ptr);
    } else {
        if (b->u.chain.parena) pchain_recycle(&b->u.chain);
    }

    b->u.slot_ptr = NULL;
    b->pa     = NULL;
    b->cap    = 0;
    b->count  = 0;
    b->offset = 0;
}

void pbuffer_free(Pbuffer *b){
    if (!b) return;

    Parena *pa = b->pa;
    pbuffer_deinit(b);
    if (pa) parena_recycle_slot(b);
}
