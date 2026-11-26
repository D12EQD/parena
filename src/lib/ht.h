#pragma once
#include "def.h"
#include <stdint.h>

typedef u32 PhashKey;

enum PNodeState{
    PNodeEmpty   = 0,
    PNodeExist   = 1,
    PNodeTomb    = 2,
};

typedef struct {
    u32 state;
    void *data;
}PhashData;

typedef struct {
    void (*free_node)(void *);              // delete call back
    int (*cmp)(void *, void *, u32);        // return -1 0 -1
}PhashOp;

typedef struct{
    uint32_t count;     // 存活的节点数量
    uint32_t cap;       // 容积
    uint32_t data_size; // 哈希表数据大小，需要固定
    uint32_t tomb;      // 假清理的节点数量
    PhashData* items;
    PhashOp op;
}PhashTable;

PhashTable *ht_new(u64 count,PhashOp op,u32 data_size);

void ht_free(PhashTable *ht);
bool ht_realloc(PhashTable *ht,u64 new_cap);
void ht_check_health(PhashTable *ht);
bool lookup(PhashTable *ht,void *data,u32 *ret_idx);

bool ht_add_data(PhashTable *ht,void *data);
bool ht_del_data(PhashTable *ht, void * data);
bool ht_ask_data_exist(PhashTable *ht, void * data);

PhashKey get_key_from_ht(PhashTable* ht, void *data);

void ht_display(PhashTable* ht);

#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif
