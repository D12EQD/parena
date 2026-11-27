#pragma once
#include <stdlib.h>
#include <stdint.h>
#include "def.h"
#include "linklist.h"

#define PA_LIMIT_BLOCK 2048 // 2048 * 4096
#define PA_BLOCK_LOG_SIZE 12
#define PA_BLOCK_SIZE (1 << PA_BLOCK_LOG_SIZE) /* default page size */
#define PA_LIMIT_SIZE PA_LIMIT_BLOCK * PA_BLOCK_SIZE

struct Parena_s;

static const u64 SLOT_LEVEL[] = {
    16, 32, 64, 128, 256, 512, 1024, 2048, 4096
};

#define SLOT_LEVEL_COUNT ( sizeof(SLOT_LEVEL)/sizeof(SLOT_LEVEL[0]))

// size == 40B 对齐 8B
typedef struct RefBlockHeader_S {
    ListNode    node;           // 挂到 slot_list[slot_level] 或 full_list[slot_level]
    List        free_list;      // 块内空闲槽链表
    u16         free_count;     // 当前空闲槽数
    u16         total_count;    // 该块总槽数
    u16         slot_level;     // 属于哪个id
    u16         keep_alive;     // 保持不死的Refhead
}RefBlockHeader; 

typedef struct Block_t{
    u8 __data[PA_BLOCK_SIZE];
}Block;

typedef struct{
    void *data;         // 数据地址
    size_t type_size;   // 每个物品的大小
    size_t cap;         // 当前容积
}Parray;

/* Ping's Arena manager = Parena */
typedef struct Parena_s{
    List empty_list;                        // 空闲块链表
    List* slot_list;                        // slot 可用链表 存放不同等级的 rbh
    List* slot_full_list;                   // slot 满链表 存放不同等级的 rbh
    Parray rbh;
    Parray block;
    Parray slot_level;
    u64 empty_block_count;                  // 当前空闲 Block 数量
} Parena;

typedef struct {
    u64* slot_level;
    u64 slot_level_count;
}ParenaOpt;

Parena* parena_new_imple(size_t size, ParenaOpt opt);
#define parena_new(size, ...) parena_new_imple((size), (ParenaOpt){ __VA_OPT__(__VA_ARGS__) })

void parena_free(Parena *pa);
void *parena_alloc_slot(Parena *a,size_t size);
void parena_recycle_slot(void * ptr);

/* 返回能满足 need_size 的 slot_level 下标；找不到时返回 slot_level.cap */
u64 parena_get_slot_level(Parena *pa, u64 need_size);

// ================== parray ==================
static inline void* parray_get(Parray* a, size_t idx){
    return a->data + idx * a->type_size;
}

static inline void parray_init(Parray* a, void *data, size_t cap, size_t type_size){
    a->data = data;
    a->cap = cap;
    a->type_size = type_size;
}

typedef struct PchainBlock_S {
    ListNode node;
} PchainHead;

#define PCHAIN_BLOCK_SIZE ( PA_BLOCK_SIZE - sizeof(PchainHead) )

typedef struct Pchain_S {
    List list;                  // Pc (PchainBlock) 链表
    Parena* parena;             // 所属 Parena
    size_t tail_remain;         // tail 剩余字节数量
} Pchain;

typedef struct Pmark_S{
    Pchain* belong;
    PchainHead* mark;
    size_t tail_remain;
}Pmark;

void pchain_init(Parena *pa,Pchain *pc);
bool pchain_append(Pchain *pc,size_t size);
Pmark pchain_get_mark(Pchain *pc);
i64 pchain_rewind(Pchain *pc,Pmark mark);
void pchain_recycle(Pchain *pc);
