#include "lib/parena.h"
#include "lib/logger.h"
#include <string.h>
#include <sys/mman.h>

#define ceil_calc_block_count(size) ((size + PA_BLOCK_SIZE - 1) / PA_BLOCK_SIZE)

#define GPA_MAX_COUNT 10

typedef struct {
    Parena *cache[GPA_MAX_COUNT];
    Parena *hot_pa;
} global_parena;

static global_parena gpa;

/* ==================== 指针 / parray 辅助 ==================== */

static inline u64 slot_level_at(Parena *pa, u64 i) {
    return *(u64 *)parray_get(&pa->slot_level, i);
}

static inline void *block_at(Parena *pa, u64 id) { return parray_get(&pa->block, id); }

static inline RefBlockHeader *head_at(Parena *pa, u64 id) {
    return (RefBlockHeader *)parray_get(&pa->rbh, id);
}

static inline u64 block_id_of(Parena *pa, void *ptr) {
    return ((u8 *)ptr - (u8 *)pa->block.data) / PA_BLOCK_SIZE;
}

static inline bool parray_contains(Parray *a, void *ptr, size_t size) {
    if (!a->data || !ptr)
        return false;
    return (u8 *)ptr >= (u8 *)a->data && (u8 *)ptr + size <= (u8 *)a->data + a->cap * a->type_size;
}

static inline bool is_safe_ptr(Parena *a, void *ptr, size_t size) {
    return a && parray_contains(&a->block, ptr, size);
}

/* ==================== 全局 parena 注册 ==================== */

static inline bool register_global_parena(Parena *pa) {
    if likely (gpa.hot_pa == pa)
        return true;

    for (int i = 0; i < GPA_MAX_COUNT; i++) {
        if (pa == gpa.cache[i]) {
            gpa.hot_pa = pa;
            return true;
        }
    }
    for (int i = 0; i < GPA_MAX_COUNT; i++) {
        if (!gpa.cache[i]) {
            gpa.cache[i] = pa;
            gpa.hot_pa   = pa;
            return true;
        }
    }
    return false;
}

static inline void unregister_global_parena(Parena *pa) {
    if (gpa.hot_pa == pa)
        gpa.hot_pa = NULL;
    for (int i = 0; i < GPA_MAX_COUNT; i++) {
        if (gpa.cache[i] == pa) {
            gpa.cache[i] = NULL;
            return;
        }
    }
    PANIC("a parena don't exist");
}

static inline Parena *get_global_parena_from_ptr(void *ptr) {
    if (!ptr)
        return NULL;

    if (gpa.hot_pa && is_safe_ptr(gpa.hot_pa, ptr, 1))
        return gpa.hot_pa;

    for (int i = 0; i < GPA_MAX_COUNT; i++) {
        if (gpa.cache[i] && is_safe_ptr(gpa.cache[i], ptr, 1)) {
            gpa.hot_pa = gpa.cache[i];
            return gpa.hot_pa;
        }
    }
    return NULL;
}

static inline RefBlockHeader *get_head_from_pa_and_ptr(Parena *pa, void *ptr) {
    u64 idx = ((u8 *)ptr - (u8 *)pa->block.data) >> PA_BLOCK_LOG_SIZE;
    return head_at(pa, idx);
}

/* ==================== block 层 ==================== */

static inline Block *block_new(Parena *pa) {
    if (list_is_empty(&pa->empty_list))
        return NULL;

    Block *res = (Block *)list_get_front(&pa->empty_list);
    list_delete_front(&pa->empty_list);

    ASSERT(pa->empty_block_count);
    pa->empty_block_count--;
    return res;
}

static inline void block_recycle(Block *block, Parena *pa) {
    pa->empty_block_count++;
    ASSERT(pa->empty_block_count <= pa->block.cap);
    list_insert_front(&pa->empty_list, block);
}

/* 把连续 [start, start+sz) 按 slot_sz 切分后挂到 head 上 */
static inline u64 split_mem_to_list(List *head, void *start, size_t sz, size_t slot_sz) {
    ASSERT(slot_sz >= sizeof(ListNode));
    u64 res = 0;
    u8 *p   = (u8 *)start;
    u8 *end = p + sz;
    for (; p + slot_sz <= end; p += slot_sz) {
        list_insert_front(head, p);
        res++;
    }
    return res;
}

static inline void ref_head_init(RefBlockHeader *head, u64 level, bool keep_alive) {
    head->slot_level  = (u16)level;
    head->free_count  = 0;
    head->total_count = 0;
    head->keep_alive  = keep_alive;
    list_init(&head->free_list);
}

/*
 *  创建新的 RefBlockHeader 并且加入对应等级的链表中
 */
static inline RefBlockHeader *ref_block_new(Parena *pa, u64 level) {
    Block *block = block_new(pa);
    if (!block)
        return NULL;

    u64             block_id = block_id_of(pa, block);
    RefBlockHeader *head     = head_at(pa, block_id);
    ref_head_init(head, level, false);

    u64 slot_count =
        split_mem_to_list(&head->free_list, block, PA_BLOCK_SIZE, slot_level_at(pa, level));
    head->free_count  = (u16)slot_count;
    head->total_count = (u16)slot_count;

    list_insert_front(&pa->slot_list[level], head);
    return head;
}

static inline void ref_block_recycle(Parena *pa, RefBlockHeader *head, void *block_ptr) {
    u64    block_id = block_id_of(pa, block_ptr);
    Block *block    = (Block *)block_at(pa, block_id);

    list_delete(&pa->slot_list[head->slot_level], head);
    block_recycle(block, pa);
}

/* ==================== slot level / 校验 ==================== */

u64 parena_get_slot_level(Parena *pa, u64 need_size) {
    int l = -1, r = (int)pa->slot_level.cap - 1;
    while (l + 1 < r) {
        int mid = (l + r) >> 1;
        if (need_size <= slot_level_at(pa, (u64)mid))
            r = mid;
        else
            l = mid;
    }
    return r;
}

static inline bool check_level_valid(u64 *l, u64 count) {
    if (!l)
        return true;
    if (!count)
        return false;
    if (count > PA_BLOCK_SIZE)
        return false;
    if (l[0] < sizeof(ListNode))
        return false;
    for (u64 i = 1; i < count; i++) {
        if (l[i - 1] >= l[i])
            return false;
    }
    if (l[count - 1] > PA_BLOCK_SIZE)
        return false;
    return true;
}

/* ==================== 构造 / 销毁 ==================== */

Parena *parena_new_imple(size_t size, ParenaOpt opt) {
    Parena         *pa        = NULL;
    RefBlockHeader *rbh_base  = NULL;
    void           *mmap_base = MAP_FAILED;
    size_t          mmap_size;
    u64             block_count;
    u64             slot_count;
    u64             i;

    if (size > PA_LIMIT_SIZE)
        return NULL;
    if (size == 0)
        size = PA_LIMIT_SIZE;
    if (!check_level_valid(opt.slot_level, opt.slot_level_count))
        return NULL;

    block_count = ceil_calc_block_count(size);
    mmap_size   = block_count * PA_BLOCK_SIZE;

    /* 1. 主体 */
    pa = calloc(1, sizeof(*pa));
    if (!pa)
        return NULL;

    list_init(&pa->empty_list);

    /* 2. slot_level */
    if (opt.slot_level) {
        parray_init(&pa->slot_level, opt.slot_level, opt.slot_level_count, sizeof(u64));
        slot_count = opt.slot_level_count;
    } else {
        parray_init(&pa->slot_level, (void *)SLOT_LEVEL, SLOT_LEVEL_COUNT, sizeof(u64));
        slot_count = SLOT_LEVEL_COUNT;
    }
    pa->slot_level.cap = slot_count;

    /* 3. slot_list / slot_full_list */
    pa->slot_list = malloc(sizeof(List) * slot_count);
    if (!pa->slot_list)
        goto err_pa;

    pa->slot_full_list = malloc(sizeof(List) * slot_count);
    if (!pa->slot_full_list)
        goto err_slot_list;

    for (i = 0; i < slot_count; i++) {
        list_init(&pa->slot_list[i]);
        list_init(&pa->slot_full_list[i]);
    }

    /* 4. 主内存区 */
    mmap_base = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mmap_base == MAP_FAILED)
        goto err_slot_full_list;
    parray_init(&pa->block, mmap_base, block_count, PA_BLOCK_SIZE);

    /* 5. RefBlockHeader 数组 */
    rbh_base = malloc(sizeof(RefBlockHeader) * block_count);
    if (!rbh_base)
        goto err_mmap;
    parray_init(&pa->rbh, rbh_base, block_count, sizeof(RefBlockHeader));

    /* 6. 计数器 */
    pa->block.cap         = block_count;
    pa->empty_block_count = block_count;

    /* 7. 注册（提交点） */
    if (!register_global_parena(pa))
        goto err_rbh;

    /* 8. empty_list */
    for (i = 0; i < block_count; i++) {
        list_insert_front(&pa->empty_list, block_at(pa, i));
    }

    return pa;

err_rbh:
    free(pa->rbh.data);
    pa->rbh.data = NULL;
err_mmap:
    munmap(pa->block.data, mmap_size);
    pa->block.data = NULL;
err_slot_full_list:
    free(pa->slot_full_list);
    pa->slot_full_list = NULL;
err_slot_list:
    free(pa->slot_list);
    pa->slot_list = NULL;
err_pa:
    free(pa);
    return NULL;
}

void parena_free(Parena *pa) {
    if (!pa)
        return;
    unregister_global_parena(pa);
    free(pa->rbh.data);
    munmap(pa->block.data, pa->block.cap * pa->block.type_size);
    free(pa->slot_list);
    free(pa->slot_full_list);
    free(pa);
}

/* ==================== slot 分配 ==================== */

/*
 *  return: 是否能通过调整链表找到一个 free_count > 0 的 head
 */

void *slot_new(Parena *pa, u64 level) {
    List *slot_list = &pa->slot_list[level];
    List *full_list = &pa->slot_full_list[level];

    RefBlockHeader *head;
    if (list_is_empty(slot_list)) {
        head = ref_block_new(pa, level);
        if (!head)
            return NULL;
    } else {
        head = list_get_front(slot_list);
    }

    void *ptr = list_get_front(&head->free_list);
    list_delete_front(&head->free_list);

    if (--head->free_count == 0) {
        list_delete_front(slot_list);
        list_insert_front(full_list, head);
    }
    return ptr;
}

void *parena_alloc_slot(Parena *pa, size_t size) {
    if unlikely (size == 0)
        return NULL;

    u64 lv = parena_get_slot_level(pa, size);
    if unlikely (lv == pa->slot_level.cap)
        return NULL;
    if unlikely (slot_level_at(pa, lv) < size)
        return NULL;

    return slot_new(pa, lv);
}

void parena_recycle_slot(void *ptr) {
    if unlikely (!ptr)
        PANIC("parena_recycle_slot Ptr is NULL?");

    Parena *pa = get_global_parena_from_ptr(ptr);
    if unlikely (!pa)
        PANIC("get_global_parena_from_ptr return NULL?");

    RefBlockHeader *head = get_head_from_pa_and_ptr(pa, ptr);

    list_insert_front(&head->free_list, ptr);
    ASSERT(((u8 *)ptr - (u8 *)pa->block.data) % slot_level_at(pa, head->slot_level) == 0);

    if (head->free_count == 0) {
        list_delete(&pa->slot_full_list[head->slot_level], head);
        list_insert_front(&pa->slot_list[head->slot_level], head);
    }

    if (++(head->free_count) == head->total_count) {
        if (!head->keep_alive) {
            ref_block_recycle(pa, head, ptr);
        }
    }
}

void pchain_init(Parena *pa, Pchain *pc) {
    memset(pc, 0, sizeof(Pchain));
    pc->parena = pa;
}

void pchain_head_init(PchainHead *head) { memset(head, 0, sizeof(PchainHead)); }

Pmark pchain_get_mark(Pchain *pc) {
    return (Pmark){.mark = (void *)pc->list.end, .belong = pc, .tail_remain = pc->tail_remain};
}

i64 pchain_rewind(Pchain *pc, Pmark mark) {
    if (!(pc) || (pc != mark.belong) || !(pc->parena)) {
        pr_warn("mark not belong to pchain");
        return -1;
    }

    size_t collected = 0;

    while (pc->list.end != (ListNode *)mark.mark) {
        if (!pc->list.end)
            break;
        Block *b = (Block *)pc->list.end;
        list_delete_back(&pc->list);
        block_recycle(b, pc->parena);
        collected++;
    }

    i64 res = (i64)collected * PCHAIN_BLOCK_SIZE - ((i64)pc->tail_remain - (i64)mark.tail_remain);
    pc->tail_remain = mark.tail_remain;
    return res;
}

bool pchain_append(Pchain *pc, size_t size) {
    Parena *pa = pc->parena;

    if (size <= pc->tail_remain) {
        pc->tail_remain -= size;
    } else {
        size_t need_block = (size - pc->tail_remain + PCHAIN_BLOCK_SIZE - 1) / PCHAIN_BLOCK_SIZE;
        size_t remain     = need_block * PCHAIN_BLOCK_SIZE - (size - pc->tail_remain);

        u64   i;
        Pmark mark = pchain_get_mark(pc);
        for (i = 0; i < need_block; i++) {
            Block *new_block = block_new(pa);

            if (!new_block) {
                pchain_rewind(pc, mark);
                return false;
            }

            list_insert_back(&pc->list, new_block);
        }

        pc->tail_remain = remain;
    }
    return true;
}

void pchain_recycle(Pchain *pc) {
    while (!list_is_empty(&pc->list)) {
        Block *b = (Block *)pc->list.end;
        list_delete_back(&pc->list);
        block_recycle(b, pc->parena);
    }

    pc->parena      = NULL;
    pc->tail_remain = 0;
}
