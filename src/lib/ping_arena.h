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

/* 
 *  内存小于等于 slot_level 会划分为引用计数分配接口 
 *  最小值不应小于 sizeof(List)
 *  最大值不应大于 PA_BLOCK_SIZE
 *  ref block 需要空出一个 ListNode，所以数字很奇怪
 *  该数字从 PA_BLOCK_SIZE - sizeof(List) 的因数选取
 */
static const uint16_t slot_level[] = {
    28, 56, 73, 146, 292, 511, 584, 1022
};

#define slot_level_count ( sizeof(slot_level)/sizeof(slot_level[0]))


// size == 28B
typedef struct RefBlockHeader {
    ListNode    node;           // 挂到 slot_list[slot_level] 或 full_list[slot_level]
    List        free_list;      // 块内空闲槽链表
    u16         free_count;     // 当前空闲槽数
    u16         total_count;    // 该块总槽数
    u16         slot_level;     // 属于哪个id
    u16         keep_alive;     // 保持不死的Refhead
    // 保持28B
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
