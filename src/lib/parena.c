#include "lib/parena.h"
#include "lib/logger.h"
#include <sys/mman.h>
#include <string.h>

// ============ inline 结构体和函数 ============
#define ceil_calc_block_count(size)\
    ((size + PA_BLOCK_SIZE - 1) / PA_BLOCK_SIZE)

#define GPA_MAX_COUNT 10 // 最多 10 个全局的 parena，太多不适合

typedef struct{
    Parena* cache[GPA_MAX_COUNT];   // 全局 parena 数组
    Parena* hot_pa;
}global_parena;

global_parena gpa;

static inline Parena* get_global_parena_from_ptr(void *ptr, size_t size);
static inline bool register_global_parena(Parena* pa);
static inline void ref_head_init(RefBlockHeader *head, u16 level, bool keep_alive);
static inline bool is_safe_ptr(Parena *a, void *ptr, size_t size);
static inline void block_recycle(Block* block);
static inline Block* block_new(Parena* pa, RefBlockHeader ** out);

// ================== parena manager ================
/*  
 *  该操作应在 parena_new 以及其他通用操作中调用
 *  returns :
 *      返回 flase 表示全局 cache 已经满了，该行为应该只在 parena_new 中出现并且调用
 */
static inline bool register_global_parena(Parena* pa){
    if likely(gpa.hot_pa == pa) return true;

    for (int i = 0; i < GPA_MAX_COUNT; i ++){
        if (pa == gpa.cache[i]){
            gpa.hot_pa = pa;
            return true;
        }
    }

    for (int i = 0; i < GPA_MAX_COUNT; i ++){
        if (!gpa.cache[i]){
            gpa.cache[i] = pa;
            gpa.hot_pa = pa;
            return true;
        }
    }

    return false;
}

static inline void unregister_global_parena(Parena *pa){
    if (gpa.hot_pa == pa){
        gpa.hot_pa = NULL;
    }

    for (int i = 0; i < GPA_MAX_COUNT; i ++){
        if (gpa.cache[i] == pa){
            gpa.cache[i] = NULL;
            return;
        }
    }

    PANIC("a parena don't exist?");
}

static inline Parena* get_global_parena_from_ptr(void *ptr, size_t size){
    if likely(is_safe_ptr(gpa.hot_pa, ptr, size)){
        return gpa.hot_pa;
    }

    for (int i = 0; i < GPA_MAX_COUNT; i ++){
        if (is_safe_ptr(gpa.cache[i], ptr, size)){
            gpa.hot_pa = gpa.cache[i];
            return gpa.hot_pa;
        }
    }

    PANIC("cache error or the ptr is BAD");
}

static inline RefBlockHeader* get_head_from_pa_and_ptr(Parena* pa, void *ptr){
    ASSERT(is_safe_ptr(pa, ptr, PA_BLOCK_SIZE));

    u64 idx = ( (u8*)ptr - (u8*)pa->rbh_base ) / sizeof(RefBlockHeader);
    return &( (RefBlockHeader *)pa->rbh_base )[idx];
}

// ============ basic opeation ============

/* 
 *  拆分一个块给 head，作为新 slot 使用
 *  该 Block 全部地址都视为可以使用的
 *  调用者需要保证 lv 是合法的，这里不检查
 */
static inline bool split_block_to_slot(Parena* a, RefBlockHeader* head, u16 lv){
    void * new_block = block_new(a, &head);
    if unlikely(!new_block){
        return false;
    }

    List *slot_list = &head->free_list;
    for (
        void *i = new_block;
        i + a->slot_level[lv] <= new_block + PA_BLOCK_SIZE; 
        i += a->slot_level[lv]
    ){
        list_insert_front(slot_list, i);
        head->free_count++;
        head->total_count++;
    }

    return true;
}

static inline void ref_head_init(RefBlockHeader *head, u16 level, bool keep_alive){
    head->slot_level = level;
    head->free_count = 0;
    head->total_count = 0;
    head->keep_alive = keep_alive;
    list_init(&(head->free_list));
}

/* 
 *  获取一个block 从 pa->empty_list 中 并且删除 
 *  同时获取对应block 对应的 RefHead
 */
static inline Block* block_new(Parena* pa, RefBlockHeader** out_head_ptr){
    List *list = &pa->empty_list;

    if unlikely(list_is_empty(list)) return NULL;

    ASSERT(pa->empty_block_count);
    pa->empty_block_count --;

    Block *block = (Block *)list->begin; 
    list_delete_front(list);

    u64 block_id = ( (u64)( ((void *)block) - pa->base ) ) / PA_BLOCK_SIZE;

    // 该block_id 对应的 RefHead 一定为空
    RefBlockHeader *new_head = &( (RefBlockHeader *)(pa->rbh_base) )[block_id];
    *out_head_ptr = new_head;
    return block;
}

/* 将一个块规划给 arena */
static inline void block_recycle_imple(Block* block, Parena* pa, RefBlockHeader* head){
    pa->empty_block_count ++;
    ASSERT(pa->empty_block_count <= pa->block_count);
    list_insert_front(&pa->empty_list, block);
    list_delete(&pa->rbh_list, head);
}

static inline void block_recycle(Block * block){
    Parena *pa = get_global_parena_from_ptr(block, PA_BLOCK_SIZE);
    RefBlockHeader* head = get_head_from_pa_and_ptr(pa, block);
    block_recycle_imple(block, pa, head);
}


/* 该地址是否对该Parena有效 */
static inline bool is_safe_ptr(Parena *a, void *ptr, size_t size){
    bool res =  (
        ( (u8*)ptr >= (u8*)a->base ) && 
        ( ( (u8*)ptr + size ) <= ((u8*)a->base) + a->block_count * PA_BLOCK_SIZE )
    );
    return res;
}

/* 计算对应的 Slot 等级 如果返回 slot_level_count 说明查找错误 */
u64 parena_get_slot_level(Parena *pa, u64 need_size){
    int l = -1, r = pa->slot_level_count;
    while (l + 1 < r){
        int mid = ( l + r ) >> 1;
        if (need_size <= pa->slot_level[mid]){
            r = mid;
        }else{
            l = mid;
        }
    }
    return r;
}

/*
 *  检测是否是合法的slot level
 *  如果为 l 也是合法的
 */
static inline bool check_level_valid(u64* l, u64 count){
    if (!l) return true;
    if (!count) return false;
    if (count > PA_BLOCK_SIZE) return false;

    if (l[0] == 0) return false;
    for (u64 i = 1; i < count; i ++){
        if (l[i - 1] >= l[i]) return false;
    }

    if (l[count - 1] > PA_BLOCK_SIZE) return false;
    return true;
}

/* 创建新的 Parena 如果 size == 0 默认使用最大容量，如果失败，返回 NULL */
Parena *parena_new_imple(size_t size, ParenaOpt opt){
    Parena *pa = NULL;
    RefBlockHeader *rbh_base = NULL;
    size_t mmap_size;
    u64 block_count;
    u64 i;

    // 前置检查：无副作用，可以直接 return
    if (size > PA_LIMIT_SIZE)
        return NULL;
    if (size == 0)
        size = PA_LIMIT_SIZE;
    if (!check_level_valid(opt.slot_level, opt.slot_level_count))
        return NULL;

    block_count = ceil_calc_block_count(size);
    mmap_size = block_count * PA_BLOCK_SIZE;

    // 1. Parena
    pa = malloc(sizeof(*pa));
    if (!pa)
        return NULL;

    pa->base              = MAP_FAILED;
    pa->slot_list         = NULL;
    pa->rbh_base          = NULL;
    pa->block_count       = block_count;
    pa->empty_block_count = block_count;

    list_init(&pa->empty_list);
    list_init(&pa->rbh_list);

    if (opt.slot_level) {
        pa->slot_level       = opt.slot_level;
        pa->slot_level_count = opt.slot_level_count;
    } else {
        pa->slot_level       = (u64 *)SLOT_LEVEL;
        pa->slot_level_count = SLOT_LEVEL_COUNT;
    }

    // 3. slot_list 数组
    pa->slot_list = malloc(sizeof(List) * pa->slot_level_count);
    if (!pa->slot_list) goto err_pa;

    for (i = 0; i < pa->slot_level_count; i++){
        list_init(&pa->slot_list[i]);
    }

    /* 4. 主内存区 */
    pa->base = mmap(
        NULL, mmap_size,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0
    );

    if (pa->base == MAP_FAILED) goto err_slot_list;

    /* 5. RefBlockHeader 数组 */
    rbh_base = (RefBlockHeader*) malloc(sizeof(RefBlockHeader) * block_count);
    if (!rbh_base) goto err_mmap;

    for (i = 0; i < block_count; i++)
        list_insert_front(&pa->rbh_list, &rbh_base[i]);
    pa->rbh_base = rbh_base;

    /* 6. 全局注册（提交点，失败即回滚） */
    if (!register_global_parena(pa))
        goto err_rbh;

    // 基本操作
    Block *start = pa->base;
    for (i = 0; i < block_count; i++)
        list_insert_front(&pa->empty_list, start++);

    return pa;
err_rbh:
    free(rbh_base);
    pa->rbh_base = NULL;
err_mmap:
    munmap(pa->base, mmap_size);
    pa->base = MAP_FAILED;
err_slot_list:
    free(pa->slot_list);
    pa->slot_list = NULL;
err_pa:
    free(pa);
    return NULL;
}

void parena_free(Parena* pa){
    unregister_global_parena(pa);
    free(pa->rbh_base);
    munmap(pa->base, pa->block_count * PA_BLOCK_SIZE);
    free(pa->slot_list);
    free(pa);
}

// ================ slot function ================

/* 获取一个 slot ptr 从对应等级的 slot_list 中 */
void* slot_new(Parena *a, u16 level){
    List* slot_list = &a->slot_list[level];
    RefBlockHeader* head = (RefBlockHeader*)slot_list[level].begin;

    // maybe while ?
    if (head->free_count == 0) {
        if (head->node.next){
            // 如果至少有两个节点，则可以支持循环
            auto flag = head;
            auto bef = head;
            int max_count = 4; // 经验次数，从ngnix偷来的

            do {
                bef = head;
                head = (RefBlockHeader *)(head->node.next);

                list_delete_front(slot_list);
                list_insert_back(slot_list, bef);

                if (head == flag) break;
                if (( max_count -- ) == 0) break;
            }while (head->free_count == 0);
        }

        if (head->free_count == 0){
            Block * new_block = block_new(a, &head);
            if (!new_block) return NULL; // TODO : Add some statisical infomation to this case in future.
                                         // It is mean that there is no useful memory anymore.
            list_insert_front(&a->slot_list[level], head);
        }
    }

    ASSERT(head->free_count);
    ASSERT(head->free_list.begin);

    head->free_count --;

    void * ret_ptr = list_get_front(&head->free_list);
    list_delete_front(&head->free_list);
    ASSERT(is_safe_ptr(a, ret_ptr, a->slot_level[level]));

    return ret_ptr;
}

/* 分配小对象 */
void* parena_alloc_slot(Parena* pa, size_t size){
    if unlikely(size == 0) return NULL;

    u16 lv = parena_get_slot_level(pa, size);
    if unlikely(lv == pa->slot_level_count || pa->slot_level[lv] < size) return NULL;

    void * ret = slot_new(pa, lv);
    return ret;
}

void parena_recycle_slot(void * ptr){
    Parena *pa = get_global_parena_from_ptr(ptr, PA_BLOCK_SIZE);
    RefBlockHeader* head = get_head_from_pa_and_ptr(pa, ptr);
    list_insert_front(&head->free_list, ptr);

    if (( ++ head->free_count ) == head->total_count){ 
        if (!head->keep_alive){
            block_recycle(ptr); // 删除该内存地址
        }
    }
}

void pchain_init(Parena *pa, Pchain* pc){
    memset(pc, 0, sizeof(Pchain));
    pc->parena = pa;
}

void pchain_head_init(PchainHead* head){
    memset(head, 0, sizeof(PchainHead));
}

Pmark pchain_get_mark(Pchain* pc){
    return (Pmark){
        .mark = (void *)pc->list.end,
        .belong = pc,
        .tail_remain = pc->tail_remain
    };
}

i64 pchain_rewind(Pchain *pc, Pmark mark){
    if ( !(pc) || ( pc != mark.belong ) || !(pc->parena)){
        pr_warn("mark not belong to pchain");
        return -1;
    }

    size_t collected = 0;

    while (pc->list.end != (ListNode *)mark.mark){
        if (!pc->list.end) break;
        Block *b = (Block *)pc->list.end;
        list_delete_back(&pc->list);
        block_recycle(b);
        collected ++;
    }

    i64 res = (i64)collected * PCHAIN_BLOCK_SIZE - ((i64)pc->tail_remain - (i64)mark.tail_remain);
    pc->tail_remain = mark.tail_remain;
    return res;
}

bool pchain_append(Pchain* pc, size_t size){
    Parena* pa = pc->parena;
    if (size <= pc->tail_remain){
        pc->tail_remain -= size;
    }else{
        size_t need_block = ceil_calc_block_count( size - pc->tail_remain );
        size_t remain = need_block * PCHAIN_BLOCK_SIZE - ( size - pc->tail_remain );

        u64 i;
        Pmark mark = pchain_get_mark(pc);
        for (i = 0; i < need_block ; i ++){
            RefBlockHeader *head;
            Block* new_block = block_new(pa, &head);

            if (!new_block){
                pchain_rewind(pc, mark);
                return false;
            }

            list_insert_front(&pc->list, head);
        }

        pc->tail_remain = remain;
    }
    return true;
}

void pchain_recycle(Pchain *pc){
    while (!list_is_empty(&pc->list)){
        Block *b = (Block *)pc->list.end;
        list_delete_back(&pc->list);
        block_recycle(b);
    }
    pc->parena = NULL;
    pc->tail_remain = 0;
}
