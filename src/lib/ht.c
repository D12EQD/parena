#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include "ht.h"

static inline PhashKey get_key(void *data, u32 data_size, u32 cap){
    u64 data_bits = 0;
    memcpy(&data_bits, data, min(data_size, sizeof(u64)));

    u64 h = ((u64)( data_bits )) * 0x9E3779B97F4A7C15UL;
    return(PhashKey)(h >> 32) & (( (u64)( cap ) ) - 1);
}

PhashKey get_key_from_ht(PhashTable* ht, void *data){
    return get_key(data, ht->data_size, ht->cap);
}

// 获取比 num 更大的 2^x
u32 get_greater_power_2(u32 num){
    u32 cap = 1;
    while (cap < num) cap <<= 1;
    return cap;
}

/*
 *  创建哈希表，默认创建两倍大小容积
 *  args:
 *      count 可能的最多元素数量
 *  return:
 *      PhashTable
 */
PhashTable * ht_new(u64 count, PhashOp op, u32 data_size){
    if (count > INT32_MAX) return NULL;
    u64 cap = get_greater_power_2(count * 2);

    PhashTable* ht = (PhashTable*)calloc(1, sizeof(PhashTable));
    if (!ht) return NULL;

    ht->cap = cap;
    ht->data_size = data_size;
    ht->items = calloc(ht->cap, sizeof(*ht->items));
    ht->op = op;

    if (!ht->items){
        free(ht);
        return NULL;
    }

    return ht;
}

/* 释放哈希表但是不释放数据空间 */
void ht_free_without_data(PhashTable* ht){
    free(ht->items);
    free(ht);
}

void ht_free(PhashTable* ht){
    if (ht->op.free_node){
        for (u64 i = 0; i < ht->cap; i ++){
            if (ht->items[i].data && ht->items[i].state == PNodeExist){
                ht->op.free_node(ht->items[i].data);
            }
        }
    }
    free(ht->items);
    free(ht);
}

bool ht_realloc(PhashTable* ht, u64 new_cap){
    // cap 是 u32，且哈希取模依赖 2 的幂
    if (new_cap > UINT32_MAX) return false;
    if (!(new_cap && !(new_cap & (new_cap - 1)))){
        new_cap = get_greater_power_2((u32)new_cap);
    }
    // 至少能装下现有元素，否则 rehash 会无处可插
    if (new_cap <= ht->count) return false;

    PhashTable new_ht = (PhashTable){
        .items = (PhashData *)calloc(new_cap, sizeof(PhashData)),
        .op = ht->op,
        .data_size = ht->data_size,
        .cap = new_cap,
        .tomb = 0,
        .count = 0,
    };

    if (!new_ht.items) goto failed;

    for (u32 i = 0; i < ht->cap; i ++){
        auto item = ht->items[i];
        if (item.state == PNodeExist){
            if (!ht_add_data(&new_ht, item.data)){
                goto ht_add_error;
            }
        }
    }

    free(ht->items);
    *ht = new_ht;

    return true;
ht_add_error:
    free(new_ht.items);
failed:
    return false;
}

__attribute__((noinline))
void ht_gcc_cleanup_func(PhashTable **ht_ptr){
    ht_check_health(*ht_ptr);
}

#define ht_auto_check_health(ht) __attribute__((cleanup(ht_gcc_cleanup_func))) PhashTable* __ht_ptr_auto_check = (ht);

/*
 *  自动检测哈希表的健康程度，可能扩容
 *  负载因子大于 0.75 为扩容条件
 */
void ht_check_health(PhashTable* ht){
    if (ht->count * 4  > 3 * ht->cap){
        ht_realloc(ht, ht->cap * 2);
    }else if (ht->cap - (ht->count + ht->tomb) <= ht->cap / 8){
        ht_realloc(ht, ht->cap);
    }
}

/* 比较两个节点的实际数据是否相等，而不是只比较哈希值 */
static inline bool node_data_equal(PhashTable* ht, const void *a, const void *b){
    if (ht->op.cmp){
        return ht->op.cmp((void *)a, (void *)b, ht->data_size) == 0;
    }
    return memcmp(a, b, ht->data_size) == 0;
}

/*
 *  args:
 *      ht PhashTable
 *      data 待查找的数据，key 由数据现场计算
 *      ret_idx 返回下标，不应该为NULL或者错误地址
 *  returns:
 *      true  找到了 key 且数据相等，ret_idx 是它的槽
 *      false 没找到，ret_idx 是应该插进去的槽
 */
bool lookup(PhashTable* ht, void *data, u32 *ret_idx){
    PhashKey key = get_key_from_ht(ht, data);
    u32 mask = ht->cap - 1;
    i32 first_tomb = -1;

    for (u32 i = key, add = 0; /* inf */; add = (add + 1) & mask, i = (i + add) & mask){
        u32 state = ht->items[i].state;

        if (state == PNodeExist){
            if (get_key(ht->items[i].data, ht->data_size, ht->cap) == key &&
                node_data_equal(ht, ht->items[i].data, data)){
                *ret_idx = i;
                goto success;
            }
        }

        if (first_tomb == -1 && state == PNodeTomb){
            first_tomb = i;
        }

        if (state == PNodeEmpty){
            if (first_tomb == -1){
                *ret_idx = i;
            }else{
                *ret_idx = first_tomb;
            }
            goto failed;
        }
    }

    // 全是墓碑和存在的节点，不可能

failed:
    return false;
success:
    return true;
}

/* 
 *  插入节点
 *  已经存在的 key 无法再次插入
 *  如果发生了重复的key，但是实际节点不同，会尝试扩容后再次插入
 */
bool ht_add_data(PhashTable *ht, void *data){
    ht_auto_check_health(ht);

    u32 ret_idx;
    bool lookup_res = lookup(ht, data, &ret_idx);

    // 如果找到了，不允许key相同的存在
    if (lookup_res){
        return false;
    }

    switch (ht->items[ret_idx].state) {
        case PNodeTomb:  // 坟墓可以直接插入
            ht->count ++;
            ht->tomb --;
            break;
        case PNodeEmpty: // 空槽可以直接插入
            ht->count ++;
            break;
        case PNodeExist: // 已经存在应该返回 false
        default:
    }
    ht->items[ret_idx].data = data;
    ht->items[ret_idx].state = PNodeExist;
    return true;
}

/* 
 *  根据已知数据查找是否存在查找数据 
 */
bool ht_ask_data_exist(PhashTable *ht, void * data){
    ht_auto_check_health(ht);
    u32 ret_idx = 0;
    bool res = lookup(ht, data, &ret_idx);
    return res;
}

bool ht_del_data(PhashTable *ht, void * data){
    ht_auto_check_health(ht);
    u32 ret_idx;
    bool res = lookup(ht, data, &ret_idx);

    if (!res) return false; // 没找到返回false

    u32 state = ht->items[ret_idx].state;
    switch (state){
        case PNodeExist:
            if (ht->op.free_node){
                ht->op.free_node(ht->items[ret_idx].data);
            }
            ht->count --;
            ht->tomb ++;
            ht->items[ret_idx].state = PNodeTomb;
            ht->items[ret_idx].data = NULL;
            break;
        case PNodeTomb:
        case PNodeEmpty:
        default:
    }

    return true;
}

