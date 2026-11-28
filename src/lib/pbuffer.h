#pragma once
#include "parena.h"

// 默认写入超过限制可以扩容
typedef struct Pbuffer_s {
    Pchain chain;
    u64    cap; // 总容量

    PchainHead *write_ptr; // 写指针，指向可写地址的链表节点
    PchainHead *read_ptr;  // 读指针，指向可读地址的链表节点

    u64 write_count; // 已写入字节数
    u64 read_count;  // 已消费字节数

    u64 write_off; // 读偏移，指明当前在该PchainBlock总读出了多少字节，通常指向下一个需要读写的内部偏移地址
    u64 read_off; // 写偏移，同上类似
} Pbuffer;

#define pbuffer_new(pa, size) pbuffer_new_imple((pa), (size))
#define pbuffer_init(b, pa, size) pbuffer_init_imple((b), (pa), (size))

static inline u8 *chain_block_data(void *node) { return (u8 *)node + sizeof(PchainHead); }

