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

static const uint16_t slot_level[] = {
    16, 32, 64, 128, 292, 511, 584, 1022
};

#define slot_level_count ( sizeof(slot_level)/sizeof(slot_level[0]))

// size == 40B 对齐 8B
typedef struct RefBlockHeader_S {
    ListNode    node;           // 挂到 slot_list[slot_level] 或 full_list[slot_level]
    List        free_list;      // 块内空闲槽链表
    u16         free_count;     // 当前空闲槽数
    u16         total_count;    // 该块总槽数
    u16         slot_level;     // 属于哪个id
    u16         keep_alive;     // 保持不死的Refhead
}RefBlockHeader; 

/*
 *  若作为普通Block，看作一整块内存
 *
 *  若作为 RefBlock，内部按照 Slot Size 来分配内存
 *  头部存放一个 (RefBlockHeader*) 指向 RefBlockHeader
 */
typedef struct Block_t{
    u8 __data[PA_BLOCK_SIZE];
}Block;

/* Ping's Arena manager = Parena */
typedef struct Parena_s{
    List empty_list;                        // 空闲块链表
    List empty_rf_list;                     // 空闲 RefBlockHeader 链表
    List slot_list[slot_level_count];       // slot 链表
    void *base;                             // 内存起始地址，用于合法内存范围判断
    u64 block_count;                        // 内存块数量
    u64 empty_block_count;                  // 内存空闲块数量
} Parena;

Parena *parena_new(size_t size);
void parena_free(Parena *pa);
void *parena_alloc_slot(Parena *a,size_t size);
void parena_recycle_slot(Parena* a, void * address);

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

u64 parena_get_slot_level(u64 need_size);
