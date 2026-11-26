#pragma once
#include "parena.h"


enum PbufferKind{
    PbufferSlot = 0,     // slot buffer
    PbufferChain,        // Pchain
    PbufferMalloc,       // malloc
};


typedef struct {
    union {
        Pchain chain;       // PbufferChain
        struct {
            void * ptr;
            Parena * pa;
        }slot;              // PbufferSlot
        u8* byte;           // PbufferMalloc
        int fd;             // PbufferFile
    }data;

    u64 cap;                // 最大容量
    u64 count;              // 写入的字节数量
    u64 offset;             // 读指针的偏移量

    u8 kind : 1;            // enum PbufferKind
    u8 last_buf : 1;        // 协议控制：整条响应的最后一帧
    u8 flush : 1;           // 协议控制：立刻发
    u8 __reserved : 5;      // 空
}Pbuffer;

typedef struct{
    Parena* pa;
    int fd;
    size_t size;
    u8 kind;
}PbufferInitOpt;

Pbuffer* pbuffer_new_imple(Parena* pa, PbufferInitOpt opt);
bool pbuffer_init_imple(Pbuffer *buf, PbufferInitOpt opt);

#define pbuffer_new(pa, ...)        pbuffer_new_imple(pa, (PbufferInitOpt){__VA_ARGS__})
#define pbuffer_init(buf, ...)      pbuffer_init_imple(buf, (PbufferInitOpt){__VA_ARGS__})






