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

/* 该地址是否对该Parena有效 */
static inline bool is_safe_ptr(Parena *a, void *ptr, size_t size){
    bool res =  (
        ( (u8*)ptr >= (u8*)a->base ) && 
        ( ( (u8*)ptr + size ) <= ((u8*)a->base) + a->block_count * PA_BLOCK_SIZE )
    );
    return res;
}

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

static inline Parena* get_global_parena_from_ptr(void *ptr){
    if likely(is_safe_ptr(gpa.hot_pa, ptr, 1)){
        return gpa.hot_pa;
    }

    for (int i = 0; i < GPA_MAX_COUNT; i ++){
        if (is_safe_ptr(gpa.cache[i], ptr, 1)){
            gpa.hot_pa = gpa.cache[i];
            return gpa.hot_pa;
        }
    }

    PANIC("cache error or the ptr is BAD");
}

static inline RefBlockHeader* get_head_from_pa_and_ptr(Parena* pa, void *ptr){
    u64 idx = ( (u8*)ptr - (u8*)pa->base ) >> PA_BLOCK_LOG_SIZE;
    return ( pa->rbh_base + idx * sizeof(RefBlockHeader) );
}

// ============ basic opeation ============

/* 
 *  拆分一个连续地址，按照某个大小，一块一块拆分进入 RefHead
 *  returns:
 *      u64 加入了多少新数据
 */
static inline u64 split_mem_to_list(List *head, void * start, size_t sz, size_t slot_sz){
    ASSERT(slot_sz >= sizeof(ListNode));
    u64 res = 0;
    for (void *i = start, *end = start + sz; i + slot_sz <= end; i += slot_sz){
        list_insert_front(head, i);
        res ++;
    }

    return res;
}

static inline void ref_head_init(RefBlockHeader *head, u64 level, bool keep_alive){
    head->slot_level = level;
    head->free_count = 0;
    head->total_count = 0;
    head->keep_alive = keep_alive;
    list_init(&(head->free_list));
}

static inline Block* block_new(Parena *pa){
    if (list_is_empty(&pa->empty_list)){
        return NULL;
    }

    Block* res = (Block *)list_get_front(&pa->empty_list);
    list_delete_front(&pa->empty_list);

    ASSERT(pa->empty_block_count);
    pa->empty_block_count --;
    return res;
}

/* 
 *  获取一个block 从 pa->empty_list 中 并且删除 
 *  同时获取对应 block 对应的 RefHead，并且调用 split_block_to_slot 将其中的数据加入
 *  保证获取到的 Head 已经可以是可以从其中提取 slot 的对象，并且保证该 Head 加入
 *   NOTE: keep_alive 在这设计，若需要实验则从这里开始
 *
 *  args:
 *      pa parena
 *      u64 level 槽位等级
 *  returns:
 *      out_head_ptr return地址
 */
static inline RefBlockHeader* ref_block_new(Parena* pa, u64 level){
    Block * block = block_new(pa);
    if (!block) return NULL;

    u64 block_id = ( (u64)( ((void *)block) - pa->base ) ) / PA_BLOCK_SIZE;

    // 该block_id 对应的 RefHead
    RefBlockHeader *new_head = &( (RefBlockHeader *)(pa->rbh_base) )[block_id];
    ref_head_init(new_head, level, false);

    u64 slot_count = split_mem_to_list(
        &new_head->free_list, block, 
        PA_BLOCK_SIZE, pa->slot_level[level]
    );

    new_head->free_count = slot_count;
    new_head->total_count = slot_count;

    list_insert_front(&pa->slot_list[level], new_head);

    return new_head;
}

/* 将一个块归还给 arena */
static inline void block_recycle(Block* block, Parena* pa){
    pa->empty_block_count ++;
    ASSERT(pa->empty_block_count <= pa->block_count);
    list_insert_front(&pa->empty_list, block);
}

static inline void ref_block_recycle(Parena *pa, RefBlockHeader* head, void* block_ptr){
    u64 a = ((u64)block_ptr) & ( ~( PA_BLOCK_SIZE - 1 ) );

    // 1. 从 slot_list 中删除 head
    list_delete(&pa->slot_list[head->slot_level], head);
    // 2. 回收该 head 的 block
    block_recycle((void *)a, pa);

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

    if (l[0] < sizeof(ListNode)) return false;
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

    if (opt.slot_level) {
        pa->slot_level       = opt.slot_level;
        pa->slot_level_count = opt.slot_level_count;
    } else {
        pa->slot_level       = (u64 *)SLOT_LEVEL;
        pa->slot_level_count = SLOT_LEVEL_COUNT;
    }

    // 3. slot_list 数组
    pa->slot_list = (List *)malloc(sizeof(List) * pa->slot_level_count);
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
void* slot_new(Parena *a, u64 level){
    List* slot_list = &a->slot_list[level];
    RefBlockHeader* head;

    if (list_is_empty(slot_list)){
        head = ref_block_new(a, level);
        if (!head) return NULL;
    }else{
        head = list_get_front(slot_list);
    }

    // 1. 保证 head 不为 NULL
    ASSERT(!list_is_empty(slot_list));


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
            head = ref_block_new(a, level);
            if (!head) return NULL; // TODO : Add some statisical infomation to this case in future.
                                    // It is mean that there is no useful memory anymore.
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
    Parena *pa = get_global_parena_from_ptr(ptr);
    RefBlockHeader* head = get_head_from_pa_and_ptr(pa, ptr);


    if (( ++ (head->free_count) ) == head->total_count) { 
        if (!head->keep_alive){
            ref_block_recycle(pa, head, ptr); // 删除该内存地址
        }
    }
}
