#include "pbuffer.h"

/* 
 *  传入选项，组织参数
 *  returns : 
 *      bool success or failed
 */
bool pbuffer_init_imple(Pbuffer *buf, PbufferInitOpt opt){
    switch (opt.kind) {
        case PbufferSlot:
            auto lv = parena_get_slot_level(opt.size);
            if (lv == slot_level_count) return false;
            if (!opt.pa) return false;

            void * ptr = parena_alloc_slot(opt.pa, opt.size);
            if (!ptr) return false;

            buf->data.slot.ptr = ptr;
            buf->data.slot.pa = opt.pa;

            // slot 难以调整大小，直接设置为 Parena 真实容量即可
            buf->cap = slot_level[lv];

            break;
        case PbufferChain:
            if (!opt.pa) return false;

            pchain_init(opt.pa, &buf->data.chain);
            if (!pchain_append(&buf->data.chain, opt.size)) return false;
            buf->cap = opt.size;

            break;
    }

    buf->count = 0;
    buf->offset = 0;

    // TODO : flush last_buf 不知道有什么用

    return true;
}

/* 
 *  从parena中分配一个Pbuffer空闲地址 并且调用 buffer_init 初始化
 */
Pbuffer* pbuffer_new_imple(Parena* pa, PbufferInitOpt opt){
    if (opt.pa != pa || pa == NULL) return NULL;
    Pbuffer * buf = parena_alloc_slot(pa, sizeof(Pbuffer));
    if (!buf) return NULL;
    if (!pbuffer_init_imple(buf, opt)) return NULL;
    return buf;
}

/*
 *  copy to user
 *  return : 读出的字节数量
 */
size_t pbuffer_read(){

}

/*
 *  copy_from_user
 *  return : 写入的字节数量
 */
size_t pbuffer_write(){

}

/*
 *  扩容
 */
bool pbuffer_extent(){

}

bool pbuffer_clean(){

}

bool pbuffer_free(){

}

