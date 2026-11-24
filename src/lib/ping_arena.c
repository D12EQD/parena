#include "lib/ping_arena.h"
#include "lib/logger.h"
#include <sys/mman.h>
#include <string.h>

#define ceil_calc_block_count(size)\
    ((size + PA_BLOCK_SIZE - 1) / PA_BLOCK_SIZE)

#define get_refhead_from_address(address)\
    ( (void *)(( (u64)( address )) & (~ (PA_BLOCK_SIZE - 1))) )

// ============ basic opeation ============
static void ref_head_init(RefBlockHeader *head, u16 level, bool keep_alive);
static bool is_safe_address(Parena *a, void *address, size_t size);
static void block_recycle(Parena *a, Block* block);
static Block* block_new(Parena* pa);

static bool split_block_to_refheader(Parena* a){
    void * new_block = block_new(a);
    if (!new_block) return false;

    for (u8 * i = new_block; 
        i + sizeof(RefBlockHeader) <= ((u8 *)new_block) + PA_BLOCK_SIZE; 
        i += sizeof(RefBlockHeader)
    ){
        list_insert_front(&a->empty_rf_list, i);
    }

    return true;
}

static bool split_block_to_slot(Parena* a, RefBlockHeader* head, u16 lv){
    void * new_block = block_new(a);
    if (!new_block){
        return false;
    }

    *(RefBlockHeader **)new_block = head;

    List *slot_list = &head->free_list;
    for (
        u8 *i = ((u8 *)new_block) + sizeof(RefBlockHeader *); 
        i + slot_level[lv] <= (u8 *)new_block + PA_BLOCK_SIZE; 
        i += slot_level[lv]
    ){
        list_insert_front(slot_list, i);
        head->free_count++;
        head->total_count++;
    }

    return true;
}

static void ref_head_init(RefBlockHeader *head, u16 level, bool keep_alive){
    head->slot_level = level;
    head->free_count = 0;
    head->total_count = 0;
    head->keep_alive = keep_alive;
    list_init(&(head->free_list));
}

/* 获取一个block 从 list 中 并且删除 */
static Block* block_new(Parena* pa){
    List *list = &pa->empty_list;

    if (list_is_empty(list)) return NULL;

    ASSERT(pa->empty_block_count);

    pa->empty_block_count --;

    Block *block = (Block *)list->begin; 

    list_delete_front(list);


    return block;
}

/* 将一个块规划给 arena */
static void block_recycle(Parena *a, Block* block){
    ASSERT(is_safe_address(a, block, sizeof(Block)));
    a->empty_block_count ++;
    ASSERT(a->empty_block_count <= a->block_count);

    list_insert_front(&a->empty_list, block);
}

static bool is_safe_address(Parena *a, void *address, size_t size){
    bool res =  (
        ( (u8*)address >= (u8*)a->base ) && 
        ( ( (u8*)address + size ) <= ((u8*)a->base) + a->block_count * PA_BLOCK_SIZE )
    );

    // if (!res){
    //     pr_debug("This address not safe ptr %lx", (u64)address);
    // }
    return res;
}

// /*
//  *  查看链表是否正常，并且打印输出
//  */
// static bool check_slot_list_health(Parena* a, List *list, size_t size){
//     for (ListNode * node = list->begin; node; node = node->next){
//         ASSERT(is_safe_address(a, node, size));
//     }
//     return false;
// }

/* 计算对应的 Slot 等级 如果返回 slot_level_count 说明查找错误 */
static u64 get_slot_level(u64 need_size){
    int l = -1, r = slot_level_count;
    while (l + 1 < r){
        int mid = ( l + r ) >> 1;
        if (need_size <= slot_level[mid]){
            r = mid;
        }else{
            l = mid;
        }
    }
    return r;
}

/* 创建新的 Parena 如果 size == 0 默认使用最大容量，如果失败，返回 NULL */
Parena* parena_new(size_t size){
    if (size > PA_LIMIT_SIZE) return NULL;
    if (size == 0) size = PA_LIMIT_SIZE;
    u64 block_count = ceil_calc_block_count(size);

    Parena* pa = (Parena *)malloc(sizeof(Parena));
    if (!pa) return NULL;

    pa->block_count = block_count;
    pa->empty_block_count = block_count;

    list_init(&pa->empty_list);
    list_init(&pa->empty_rf_list);
    for (u64 i = 0; i < slot_level_count; i ++){
        list_init(&pa->slot_list[i]);
    }

    pa->base = mmap(
        NULL, 
        block_count * PA_BLOCK_SIZE, 
        PROT_WRITE | PROT_READ, 
        MAP_PRIVATE | MAP_ANONYMOUS, 
        -1, 0
    );

    if (pa->base == (void *)(-1)){
        free(pa);
        return NULL;
    }

    // 插入链表中
    Block * start = pa->base;

    for (u64 i = 0; i < block_count; i ++){
        list_insert_front(&pa->empty_list, start);
        start ++;
    }

    // ref head 的数量和 block_count 保持 相同大小
    u64 ref_block_count = ceil_calc_block_count(block_count * sizeof(RefBlockHeader));
    for (u64 i = 0; i < ref_block_count; i ++){
        split_block_to_refheader(pa);
    }

    return pa;
}

void parena_free(Parena* pa){
    munmap(pa->base, pa->block_count * PA_BLOCK_SIZE);
    free(pa);
}

// ========== ref head function ==========

/* 字面意思 */
static void ref_head_recycle(Parena* a, RefBlockHeader* head){
    list_delete(&a->slot_list[head->slot_level], head);
    list_insert_front(&a->empty_rf_list, head);
}

/* 
 *  从 ref_list 获取一个 ref_head，没有时会尝试从空闲内存块中获取一个新的
 *  该 ref_head 会初始化，并且绑定一个对应等级的 Block
 */
RefBlockHeader* ref_head_new(Parena*a, u16 level){
    List *ref_list = &a->empty_rf_list;
    if (list_is_empty(ref_list)){
        return NULL;
    }

    RefBlockHeader *head = list_get_front(ref_list);
    list_delete_front(ref_list);

    ref_head_init(head, level, list_is_empty(&a->slot_list[level]));

    if (!split_block_to_slot(a, head, level)){
        ref_head_recycle(a, head);
        return NULL;
    }

    list_insert_front(&a->slot_list[level], head);

    return head;
}


// ================ slot function ================

/* 获取一个 slot address 从对应等级的 slot_list 中 */
void* slot_new(Parena *a, u16 level){
    List* slot_list = &a->slot_list[level];
    RefBlockHeader* head;

    if (list_is_empty(slot_list)){
        // 如果该等级的slot_level 没有 head
        // 获取一个 RefHead
        head = ref_head_new(a, level);
        if (!head) return NULL;
        ASSERT(is_safe_address(a, head, sizeof(RefBlockHeader)));
    }else{
        head = list_get_front(slot_list);
    }

    ASSERT(slot_list->begin);

    // maybe while ?
    if (head->free_count == 0) {
        if (head->node.next){
            // 如果至少有两个节点，则可以支持循环
            auto flag = head;
            auto bef = head;
            int max_count = 5; // 经验次数，从ngnix偷来的
            do {
                bef = head;
                head = (RefBlockHeader *)(head->node.next);

                list_delete_front(slot_list);
                list_insert_back(slot_list, bef);

                if (head == flag) break;
                if (( max_count -- ) == 0) break;
            }while (head->free_count == 0);
        }

        // 如果寻找了半天还是不够，换一个 new head
        if (head->free_count == 0){
            RefBlockHeader* new_head = ref_head_new(a, level);
            if (!new_head) return NULL;
            head = new_head;
        }
    }

    ASSERT(head->free_count);
    ASSERT(head->free_list.begin);

    head->free_count --;
    void * ret_address = list_get_front(&head->free_list);
    list_delete_front(&head->free_list);
    ASSERT(is_safe_address(a, ret_address, slot_level[level]));

    return ret_address;
}

/* 分配小对象 */
void* parena_alloc_slot(Parena* a, size_t size){
    if (size == 0) return NULL;

    u16 lv = get_slot_level(size);
    if (lv == slot_level_count || slot_level[lv] < size) return NULL;

    void * ret = slot_new(a, lv);
    return ret;
}

void parena_recycle_slot(Parena* a, void * address){
    if (!is_safe_address(a, address, sizeof(void *))) return;
    RefBlockHeader** head_ptr = get_refhead_from_address(address);
    RefBlockHeader* head = *head_ptr;
    list_insert_front(&head->free_list, address);

    if (( ++ head->free_count ) == head->total_count){ 
        if (!head->keep_alive){
            ref_head_recycle(a, head);
            block_recycle(a, get_refhead_from_address(address)); // 删除该内存地址
        }
    }
}
