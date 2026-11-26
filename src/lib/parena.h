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
    8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096
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

/* Ping's Arena manager = Parena */
typedef struct Parena_s{
    List empty_list;                        // 空闲块链表
    List rbh_list;                          // rhb list
    List* slot_list;                        // slot 链表
    void *base;                             // 内存起始地址，用于合法内存范围判断
    void *rbh_base;                         // rbh base 地址
    u64 block_count;                        // 内存块数量
    u64 empty_block_count;                  // 内存空闲块数量
    u64* slot_level;                        // 内置 slot_level 等级，支持用户输入自己想要的等级分配
    u64 slot_level_count;                   // slot_level 等级数量
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

u64 parena_get_slot_level(Parena *pa, u64 need_size);
