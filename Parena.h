#pragma once
#include <stdlib.h>
#include <assert.h>
#include <sys/mman.h>
#include <string.h>

#ifndef likely
#define likely(x)	(__builtin_expect(!!(x), 1))
#endif

#ifndef unlikely
#define unlikely(x)	(__builtin_expect(!!(x), 0))
#endif

#ifndef _ASM
#define _ASM __asm__ __volatile__
#endif

#ifndef max
#define max(a, b) (( a ) > ( b ) ? ( a ) : ( b ))
#endif

#ifndef min
#define min(a, b) (( a ) < ( b ) ? ( a ) : ( b ))
#endif

#ifndef get_bit
#define get_bit(b, l, r) (((b) >> (l)) & ((1ULL << ((r) - (l) + 1)) - 1))
#endif

#ifndef set_bit
#define set_bit(b, l, r) ((b) |= (((1ULL << ((r) - (l) + 1)) - 1) << (l)))
#endif

#ifndef lowbit
#define lowbit(x) ((x) & (-x))
#endif

#ifndef ANSI_RESET
#define ANSI_RESET          "\033[0m"
#endif

#ifndef ANSI_RED
#define ANSI_RED            "\033[31m"
#endif

#ifndef ANSI_GREEN
#define ANSI_GREEN          "\033[1;32m"
#endif

#ifndef ANSI_YELLOW
#define ANSI_YELLOW         "\033[33m"
#endif

#ifndef ANSI_BLUE
#define ANSI_BLUE           "\033[34m"
#endif

#ifndef ANSI_MAGENTA
#define ANSI_MAGENTA        "\033[35m"
#endif

#ifndef ANSI_CYAN
#define ANSI_CYAN           "\033[36m"
#endif

#ifndef ANSI_BRIGHT_RED
#define ANSI_BRIGHT_RED     "\033[1;31m"
#endif

#ifndef ANSI_BRIGHT_YELLOW
#define ANSI_BRIGHT_YELLOW  "\033[1;33m"
#endif

#ifndef ANSI_BRIGHT_GREEN
#define ANSI_BRIGHT_GREEN   "\033[1;32m"
#endif

typedef signed char         i8;
typedef unsigned char       u8;

typedef signed short        i16;
typedef unsigned short      u16;

typedef signed int          i32;
typedef unsigned int        u32;

typedef signed long         i64;
typedef unsigned long       u64;

struct ListNode_s{
    struct ListNode_s* prev;
    struct ListNode_s* next;
};

struct List_s{
    struct ListNode_s * begin; // 指向开始的位置
    struct ListNode_s * end; // 指向最后一个位置
};


typedef struct ListNode_s ListNode;
typedef struct List_s List;

struct Parena_s;

#ifndef PA_LIMIT_BLOCK
#define PA_LIMIT_BLOCK 2048 // 2048 * 4096
#endif

#ifndef PA_BLOCK_LOG_SIZE

#define PA_BLOCK_LOG_SIZE 12
#define PA_BLOCK_SIZE (1 << PA_BLOCK_LOG_SIZE) /* default page size */
#define PA_LIMIT_SIZE PA_LIMIT_BLOCK * PA_BLOCK_SIZE

#else

#define PA_BLOCK_SIZE (1 << PA_BLOCK_LOG_SIZE)
#define PA_LIMIT_SIZE PA_LIMIT_BLOCK * PA_BLOCK_SIZE

#endif


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

/*
 * Pbuffer 有两种底层：
 *   PbufferSlot  -- 从 Parena 申请一个固定 slot，容量不可变
 *   PbufferChain -- 按 Block 串成 Pchain，容量可在上限内增长
 *
 * 写入永远是追加（写到 count），读取从 offset 开始消费。
 */
typedef enum PbufferKind {
    PbufferSlot = 0,        // 单块固定容量
    PbufferChain,           // 块链，可增长
} PbufferKind;

/*
 * grow      是否允许写满后动态扩容
 * max_size  扩容上限；grow=1 且 max_size=0 表示不设上限（受 Parena 剩余块限制）
 * 默认（零初始化）：grow=0，容量就是申请时给的 size。
 */
typedef struct {
    size_t max_size;
    bool   grow;
} PbufferOpt;

typedef struct Pbuffer_s {
    union {
        void   *slot_ptr;   // PbufferSlot
        Pchain  chain;      // PbufferChain
    } u;

    Parena *pa;             // 所属 Parena（也是结构体本身的来源）
    u64 cap;                // 已提交容量（字节）
    u64 count;              // 已写入字节数
    u64 offset;             // 已消费字节数
    u64 max_size;           // 不限制，0表示不限制

    u8 kind : 4;
    u8 grow : 4;
} Pbuffer;

Pbuffer *pbuffer_new_imple(Parena *pa, PbufferKind kind, size_t size, PbufferOpt opt);
bool pbuffer_init_imple(Pbuffer *b, Parena *pa, PbufferKind kind, size_t size, PbufferOpt opt);

#define pbuffer_new(pa, kind, size, ...) \
    pbuffer_new_imple((pa), (kind), (size), (PbufferOpt){ __VA_OPT__(__VA_ARGS__) })

#define pbuffer_init(b, pa, kind, size, ...) \
    pbuffer_init_imple((b), (pa), (kind), (size), (PbufferOpt){ __VA_OPT__(__VA_ARGS__) })

/* 可读 / 可写字节数 */
static inline u64 pbuffer_len(Pbuffer *b){
    return b->count - b->offset;
}

static inline u64 pbuffer_space(Pbuffer *b){
    return b->cap - b->count;
}

/*
 * 追加写：返回实际写入的字节数。
 *   - slot  : 写满就截断
 *   - chain : grow=1 时先扩容到 max_size，仍不够则截断
 */
size_t pbuffer_write(Pbuffer *b, const void *src, size_t n);

/* 消费读：推进 offset，返回实际读出字节数 */
size_t pbuffer_read(Pbuffer *b, void *dst, size_t n);

/* 只读不推进 offset */
size_t pbuffer_peek(Pbuffer *b, void *dst, size_t n);

/*
 * 保证还能再写 extra 字节（即 cap >= count + extra）。
 * 仅 chain 且 grow=1 时可能真正扩容；返回最终是否满足。
 */
bool pbuffer_extend(Pbuffer *b, size_t extra);

/* 清空内容：chain 会把块还给 Parena，slot 只重置读写位置 */
void pbuffer_reset(Pbuffer *b);

/* 只释放底层内存，保留 Pbuffer 结构体本身（可用于栈上结构体） */
void pbuffer_deinit(Pbuffer *b);

/* deinit + 把结构体还给 Parena；只用于 pbuffer_new 创建的 Pbuffer */
void pbuffer_free(Pbuffer *b);

#ifdef PARENA_IMPLEMENT

#define list_ptr(val) ((List *)(val))
#define list_is_empty(val) ((!((list_ptr(val)))->begin))


/* Interface */
#define list_insert_front(l, n) list_insert_front_imple((List* )l, (ListNode *)n)
#define list_insert_back(l, n) list_insert_back_imple((List* )l, (ListNode *)n)

// 遍历整个链表找到node 在node节点后插入n这个节点
#define list_insert_after(l, n, node) list_insert_after_imple((List* )l, (ListNode *)n, (ListNode *) node) 

// 遍历整个链表找到node 在node节点前插入n这个节点
#define list_insert_before(l, n, node) list_insert_before_imple((List* )l, (ListNode *)n, (ListNode *) node)
#define list_delete(l, node) list_delete_imple((List* )l, (ListNode *) node) 
#define link_node_delete(l, n) link_node_delete_imple((List *)l, (ListNode *) n)
#define list_init(l) link_list_init_imple((List *)l)
#define list_free(l) link_list_free_imple((List *)l)

/* 从链表头部获取一个数据 不保证链表安全 */
#define list_get_front(l) (void *)( ((List*)l)->begin )
#define list_get_back(l) (void *)( ((List*)l)->end )


/*
* 注意该函数只是初始化不创建
*/
static inline void link_list_init_imple(List *l){
    if (!l){
        return;
    }

    l->begin = NULL;
    l->end = NULL;
    return;
}   

/*
* 插入链表首部
* begin -> begin_next
* n -> begin -> begin_next
*/
static inline  bool list_insert_front_imple(List *l, ListNode *n){
    if (l->begin == NULL && l->end == NULL){
        // 空链表
        l->begin = n;
        l->end = n;

        n->prev = NULL;
        n->next = NULL;
        return true;
    }
    
    ListNode * b = l->begin;
    n->next = b;    
    b->prev = n;    
    l->begin = n;   
    n->prev = NULL; 

    return true;
}

/*
* 插入数据到尾部
* end -> NULL 
* end -> n -> NULL 
*/
static inline bool list_insert_back_imple(List *l, ListNode * n){
    if (!l) return false;
    if (l->begin == NULL && l->end == NULL){
        // 空链表
        l->begin = n;
        l->end = n;

        n->prev = NULL;
        n->next = NULL;

        return true;
    }

    ListNode * e = l->end;
    l -> end = n;

    // e -> prev = e -> prev; 
    e -> next = n;

    n -> prev = e;
    n -> next = NULL;

    return true;
}

/*
* 遍历链表中的节点，比较地址相同的node地址并且在next插入该节点
*/
static inline bool list_insert_after_imple(List *l, ListNode * n, ListNode * node){
    if (list_is_empty(l)){
        return false;
    }

    for (ListNode * i = l->begin; i != NULL; i = i->next){
        if (i == node){
            if (i == l->end) return list_insert_back(l, n);
            
            /*
                i - i_next
                i - n - i_next
            */
            
            n->next = i->next;
            n->prev = i;

            if (i->next) i->next->prev = n;
            i->next = n;

            
            return true;
        }
    }

    return false;
}


/*
* 遍历链表中的节点，比较地址相同的node地址并且在prev插入该节点
*/
static inline bool list_insert_before_imple(List *l, ListNode * n, ListNode * node){
    if (list_is_empty(l)) return false;

    for (ListNode * i = l->begin; i != NULL; i = i->next){
        if (i == node){
            if (i == l->begin) return list_insert_front(l, n);
            
            /*
                i_prve - i 
                i_prve - n - i 
            */

            n->next = i;
            n->prev = i->prev;

            i->prev->next = n;
            i->prev = n;
            // i->next = i->next;
            
            return true;
        }
    }

    return false;
}

static inline bool list_delete_front(List * l){
    if (list_is_empty(l)) return false;

    if (l->begin == l->end){
        l->begin->next = NULL;
        l->begin->prev = NULL;

        l->begin = NULL;
        l->end = NULL;
        return true;
    }

    ListNode *b = l->begin;

    l->begin = b->next;
    b->next->prev = NULL;

    b->next = NULL;
    b->prev = NULL;
    return true;
}

static inline bool list_delete_back(List * l){
    if (list_is_empty(l)) return false;

    if (l->begin == l->end){
        l->begin->next = NULL;
        l->begin->prev = NULL;

        l->begin = NULL;
        l->end = NULL;
        return true;
    }

    ListNode * e = l->end;
    e->prev->next = NULL;
    l->end = e->prev;

    e->next = NULL;
    e->prev = NULL;
    return true;
}

static inline void list_delete_imple(List * l, ListNode * node){
    if (list_is_empty(l)) return;
    if (node == l->begin){
        list_delete_front(l);
        return;
    }
    if (node == l->end){
        list_delete_back(l);
        return;
    }

    node->prev->next = node->next;  
    node->next->prev = node->prev;  
    node->prev = NULL;
    node->next = NULL;

    return;
}

/*
* 释放整个链表节点，并且释放链表本身
*/
static inline void link_list_free_imple(List *l){
    ListNode *cur = l->begin;
    while (cur) {
        ListNode *next = cur->next;
        free(cur);
        cur = next;
    }
    free(l);
}

/*
* 指定node，从当前链表中直接删除
*/
static inline void link_node_delete_imple(List *l, ListNode *node) {
    if (!l || !node) return;
    
    if (l->begin == node) {
        l->begin = node->next;
    }
    
    if (l->end == node) {
        l->end = node->prev;
    }
    
    if (node->next) node->next->prev = node->prev;
    if (node->prev) node->prev->next = node->next;
    
    node->next = NULL;
    node->prev = NULL;
}

// ================== implement ==================
static inline void* parray_get(Parray* a, size_t idx){
    return a->data + idx * a->type_size;
}

static inline void parray_init(Parray* a, void *data, size_t cap, size_t type_size){
    a->data = data;
    a->cap = cap;
    a->type_size = type_size;
}

static const u64 SLOT_LEVEL[] = {
    16, 32, 64, 128, 256, 512, 1024, 2048, 4096
};
#define SLOT_LEVEL_COUNT ( sizeof(SLOT_LEVEL)/sizeof(SLOT_LEVEL[0]))

#define ceil_calc_block_count(size) \
    ((size + PA_BLOCK_SIZE - 1) / PA_BLOCK_SIZE)

#define GPA_MAX_COUNT 10

typedef struct {
    Parena *cache[GPA_MAX_COUNT];
    Parena *hot_pa;
} global_parena;

static global_parena gpa;

/* ==================== 指针 / parray 辅助 ==================== */

static inline u64 slot_level_at(Parena *pa, u64 i) {
    return *(u64 *)parray_get(&pa->slot_level, i);
}

static inline void *block_at(Parena *pa, u64 id) {
    return parray_get(&pa->block, id);
}

static inline RefBlockHeader *head_at(Parena *pa, u64 id) {
    return (RefBlockHeader *)parray_get(&pa->rbh, id);
}

static inline u64 block_id_of(Parena *pa, void *ptr) {
    return ((u8 *)ptr - (u8 *)pa->block.data) / PA_BLOCK_SIZE;
}

static inline bool parray_contains(Parray *a, void *ptr, size_t size) {
    if (!a->data || !ptr) return false;
    return (u8 *)ptr >= (u8 *)a->data && (u8 *)ptr + size <= (u8 *)a->data + a->cap * a->type_size;
}

static inline bool is_safe_ptr(Parena *a, void *ptr, size_t size) {
    return a && parray_contains(&a->block, ptr, size);
}

/* ==================== 全局 parena 注册 ==================== */

static inline bool register_global_parena(Parena *pa) {
    if likely(gpa.hot_pa == pa) return true;

    for (int i = 0; i < GPA_MAX_COUNT; i++) {
        if (pa == gpa.cache[i]) { gpa.hot_pa = pa; return true; }
    }
    for (int i = 0; i < GPA_MAX_COUNT; i++) {
        if (!gpa.cache[i]) { 
            gpa.cache[i] = pa; gpa.hot_pa = pa; 
            return true; 
        }
    }
    return false;
}

static inline void unregister_global_parena(Parena *pa) {
    if (gpa.hot_pa == pa) gpa.hot_pa = NULL;
    for (int i = 0; i < GPA_MAX_COUNT; i++) {
        if (gpa.cache[i] == pa) { gpa.cache[i] = NULL; return; }
    }
}

static inline Parena *get_global_parena_from_ptr(void *ptr) {
    if (!ptr) return NULL;

    if (gpa.hot_pa && is_safe_ptr(gpa.hot_pa, ptr, 1))
        return gpa.hot_pa;

    for (int i = 0; i < GPA_MAX_COUNT; i++) {
        if (gpa.cache[i] && is_safe_ptr(gpa.cache[i], ptr, 1)) {
            gpa.hot_pa = gpa.cache[i];
            return gpa.hot_pa;
        }
    }
    return NULL;
}

static inline RefBlockHeader *get_head_from_pa_and_ptr(Parena *pa, void *ptr) {
    u64 idx = ((u8 *)ptr - (u8 *)pa->block.data) >> PA_BLOCK_LOG_SIZE;
    return head_at(pa, idx);
}

/* ==================== block 层 ==================== */

static inline Block *block_new(Parena *pa) {
    if (list_is_empty(&pa->empty_list)) return NULL;

    Block *res = (Block *)list_get_front(&pa->empty_list);
    list_delete_front(&pa->empty_list);

    pa->empty_block_count--;
    return res;
}

static inline void block_recycle(Block *block, Parena *pa) {
    pa->empty_block_count++;
    list_insert_front(&pa->empty_list, block);
}

/* 把连续 [start, start+sz) 按 slot_sz 切分后挂到 head 上 */
static inline u64 split_mem_to_list(List *head, void *start, size_t sz, size_t slot_sz) {
    u64 res = 0;
    u8 *p = (u8 *)start;
    u8 *end = p + sz;
    for (; p + slot_sz <= end; p += slot_sz) {
        list_insert_front(head, p);
        res++;
    }
    return res;
}

static inline void ref_head_init(RefBlockHeader *head, u64 level, bool keep_alive) {
    head->slot_level = (u16)level;
    head->free_count = 0;
    head->total_count = 0;
    head->keep_alive = keep_alive;
    list_init(&head->free_list);
}

/*
 *  创建新的 RefBlockHeader 并且加入对应等级的链表中
 */
static inline RefBlockHeader *ref_block_new(Parena *pa, u64 level) {
    Block *block = block_new(pa);
    if (!block) return NULL;

    u64 block_id = block_id_of(pa, block);
    RefBlockHeader *head = head_at(pa, block_id);
    ref_head_init(head, level, false);

    u64 slot_count = split_mem_to_list(
        &head->free_list, block, PA_BLOCK_SIZE, slot_level_at(pa, level)
    );
    head->free_count  = (u16)slot_count;
    head->total_count = (u16)slot_count;

    list_insert_front(&pa->slot_list[level], head);
    return head;
}

static inline void ref_block_recycle(Parena *pa, RefBlockHeader *head, void *block_ptr) {
    u64 block_id = block_id_of(pa, block_ptr);
    Block *block = (Block *)block_at(pa, block_id);

    list_delete(&pa->slot_list[head->slot_level], head);
    block_recycle(block, pa);
}

/* ==================== slot level / 校验 ==================== */

u64 parena_get_slot_level(Parena *pa, u64 need_size) {
    int l = -1, r = (int)pa->slot_level.cap - 1;
    while (l + 1 < r) {
        int mid = (l + r) >> 1;
        if (need_size <= slot_level_at(pa, (u64)mid)) r = mid;
        else l = mid;
    }
    return r;
}

static inline bool check_level_valid(u64 *l, u64 count) {
    if (!l) return true;
    if (!count) return false;
    if (count > PA_BLOCK_SIZE) return false;
    if (l[0] < sizeof(ListNode)) return false;
    for (u64 i = 1; i < count; i++) {
        if (l[i - 1] >= l[i]) return false;
    }
    if (l[count - 1] > PA_BLOCK_SIZE) return false;
    return true;
}

/* ==================== 构造 / 销毁 ==================== */

Parena *parena_new_imple(size_t size, ParenaOpt opt) {
    Parena *pa = NULL;
    RefBlockHeader *rbh_base = NULL;
    void *mmap_base = MAP_FAILED;
    size_t mmap_size;
    u64 block_count;
    u64 slot_count;
    u64 i;

    if (size > PA_LIMIT_SIZE) return NULL;
    if (size == 0) size = PA_LIMIT_SIZE;
    if (!check_level_valid(opt.slot_level, opt.slot_level_count)) return NULL;

    block_count = ceil_calc_block_count(size);
    mmap_size   = block_count * PA_BLOCK_SIZE;

    /* 1. 主体 */
    pa = calloc(1, sizeof(*pa));
    if (!pa) return NULL;

    list_init(&pa->empty_list);

    /* 2. slot_level */
    if (opt.slot_level) {
        parray_init(&pa->slot_level, opt.slot_level, opt.slot_level_count, sizeof(u64));
        slot_count = opt.slot_level_count;
    } else {
        parray_init(&pa->slot_level, (void *)SLOT_LEVEL, SLOT_LEVEL_COUNT, sizeof(u64));
        slot_count = SLOT_LEVEL_COUNT;
    }
    pa->slot_level.cap = slot_count;

    /* 3. slot_list / slot_full_list */
    pa->slot_list = malloc(sizeof(List) * slot_count);
    if (!pa->slot_list) goto err_pa;

    pa->slot_full_list = malloc(sizeof(List) * slot_count);
    if (!pa->slot_full_list) goto err_slot_list;

    for (i = 0; i < slot_count; i++) {
        list_init(&pa->slot_list[i]);
        list_init(&pa->slot_full_list[i]);
    }

    /* 4. 主内存区 */
    mmap_base = mmap(
        NULL, mmap_size,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0
    );
    if (mmap_base == MAP_FAILED) goto err_slot_full_list;
    parray_init(&pa->block, mmap_base, block_count, PA_BLOCK_SIZE);

    /* 5. RefBlockHeader 数组 */
    rbh_base = malloc(sizeof(RefBlockHeader) * block_count);
    if (!rbh_base) goto err_mmap;
    parray_init(&pa->rbh, rbh_base, block_count, sizeof(RefBlockHeader));

    /* 6. 计数器 */
    pa->block.cap = block_count;
    pa->empty_block_count = block_count;

    /* 7. 注册（提交点） */
    if (!register_global_parena(pa)) goto err_rbh;

    /* 8. empty_list */
    for (i = 0; i < block_count; i++) {
        list_insert_front(&pa->empty_list, block_at(pa, i));
    }

    return pa;

err_rbh:
    free(pa->rbh.data);
    pa->rbh.data = NULL;
err_mmap:
    munmap(pa->block.data, mmap_size);
    pa->block.data = NULL;
err_slot_full_list:
    free(pa->slot_full_list);
    pa->slot_full_list = NULL;
err_slot_list:
    free(pa->slot_list);
    pa->slot_list = NULL;
err_pa:
    free(pa);
    return NULL;
}

void parena_free(Parena *pa) {
    if (!pa) return;
    unregister_global_parena(pa);
    free(pa->rbh.data);
    munmap(pa->block.data, pa->block.cap * pa->block.type_size);
    free(pa->slot_list);
    free(pa->slot_full_list);
    free(pa);
}

/* ==================== slot 分配 ==================== */

/*
 *  return: 是否能通过调整链表找到一个 free_count > 0 的 head
 */

void *slot_new(Parena *pa, u64 level) {
    List *slot_list = &pa->slot_list[level];
    List *full_list = &pa->slot_full_list[level];

    RefBlockHeader *head;
    if (list_is_empty(slot_list)){
        head = ref_block_new(pa, level);
        if (!head) return NULL;
    }else{
        head = list_get_front(slot_list);
    }
 

    void * ptr = list_get_front(&head->free_list);
    list_delete_front(&head->free_list);

    if (-- head->free_count == 0){
        list_delete_front(slot_list);
        list_insert_front(full_list, head);
    }
    return ptr;
}

void *parena_alloc_slot(Parena *pa, size_t size) {
    if unlikely(size == 0) return NULL;

    u64 lv = parena_get_slot_level(pa, size);
    if unlikely(lv == pa->slot_level.cap) return NULL;
    if unlikely(slot_level_at(pa, lv) < size) return NULL;

    return slot_new(pa, lv);
}

void parena_recycle_slot(void *ptr) {
    Parena *pa = get_global_parena_from_ptr(ptr);
    RefBlockHeader *head = get_head_from_pa_and_ptr(pa, ptr);

    list_insert_front(&head->free_list, ptr);

    if (head->free_count == 0){
        list_delete(&pa->slot_full_list[head->slot_level], head);
        list_insert_front(&pa->slot_list[head->slot_level], head);
    }

    if (++(head->free_count) == head->total_count) {
        if (!head->keep_alive) {
            ref_block_recycle(pa, head, ptr);
        }
    }
}


void pchain_init(Parena *pa, Pchain* pc){
    memset(pc, 0, sizeof(Pchain));
    pc->parena = pa;
}

void pchain_head_init(PchainHead* head){
    memset(head, 0, sizeof(PchainHead));
}

Pmark pchain_get_mark(Pchain* pc){
    return (Pmark){
        .mark = (void *)pc->list.end,
        .belong = pc,
        .tail_remain = pc->tail_remain
    };
}

i64 pchain_rewind(Pchain *pc, Pmark mark){
    if ( !(pc) || ( pc != mark.belong ) || !(pc->parena)){
        return -1;
    }

    size_t collected = 0;

    while (pc->list.end != (ListNode *)mark.mark){
        if (!pc->list.end) break;
        Block *b = (Block *)pc->list.end;
        list_delete_back(&pc->list);
        block_recycle(b, pc->parena);
        collected ++;
    }

    i64 res = (i64)collected * PCHAIN_BLOCK_SIZE - ((i64)pc->tail_remain - (i64)mark.tail_remain);
    pc->tail_remain = mark.tail_remain;
    return res;
}

bool pchain_append(Pchain* pc, size_t size){
    Parena* pa = pc->parena;

    if (size <= pc->tail_remain){
        pc->tail_remain -= size;
    }else{
        size_t need_block = (size - pc->tail_remain + PCHAIN_BLOCK_SIZE - 1) / PCHAIN_BLOCK_SIZE;
        size_t remain = need_block * PCHAIN_BLOCK_SIZE - ( size - pc->tail_remain );

        u64 i;
        Pmark mark = pchain_get_mark(pc);
        for (i = 0; i < need_block ; i ++){
            Block* new_block = block_new(pa);

            if (!new_block){
                pchain_rewind(pc, mark);
                return false;
            }

            list_insert_back(&pc->list, new_block);
        }

        pc->tail_remain = remain;
    }
    return true;
}

void pchain_recycle(Pchain *pc){
    while (!list_is_empty(&pc->list)){
        Block *b = (Block *)pc->list.end;
        list_delete_back(&pc->list);
        block_recycle(b, pc->parena);
    }

    pc->parena = NULL;
    pc->tail_remain = 0;
}

/* ==================== chain 底层块访问 ==================== */

/* 块首部是 PchainHead（链表节点），数据区紧跟其后 */
static inline u8 *chain_block_data(void *node){
    return (u8 *)node + sizeof(PchainHead);
}

/*
 *  把 chain 逻辑区间 [off, off+n) 拷出到 dst。
 *  要求 off + n <= 已提交容量，调用方保证。
 */
static inline void chain_copy_out(Pchain *pc, u64 off, void *dst, size_t n){
    ListNode *blk = pc->list.begin;
    u8 *out = (u8 *)dst;

    while (blk && off >= PCHAIN_BLOCK_SIZE){
        blk = blk->next;
        off -= PCHAIN_BLOCK_SIZE;
    }

    while (n){
        if (!blk){
            return;
        }
        size_t chunk = (size_t)min((u64)n, (u64)PCHAIN_BLOCK_SIZE - off);
        memcpy(out, chain_block_data(blk) + off, chunk);
        out += chunk;
        n   -= chunk;
        off  = 0;
        blk  = blk->next;
    }
}

/* 把 src 写入 chain 的逻辑区间 [off, off+n) */
static inline void chain_copy_in(Pchain *pc, u64 off, const void *src, size_t n){
    ListNode *blk = pc->list.begin;
    const u8 *in = (const u8 *)src;

    while (blk && off >= PCHAIN_BLOCK_SIZE){
        blk = blk->next;
        off -= PCHAIN_BLOCK_SIZE;
    }

    while (n){
        if (!blk){
            return;
        }
        size_t chunk = (size_t)min((u64)n, (u64)PCHAIN_BLOCK_SIZE - off);
        memcpy(chain_block_data(blk) + off, in, chunk);
        in  += chunk;
        n   -= chunk;
        off  = 0;
        blk  = blk->next;
    }
}

static inline u64 slot_size_at(Parena *pa, u64 lv){
    return *(u64 *)parray_get(&pa->slot_level, lv);
}

/* ==================== 构造 ==================== */

bool pbuffer_init_imple(Pbuffer *b, Parena *pa, PbufferKind kind, size_t size, PbufferOpt opt){
    if (unlikely(!b || !pa)) return false;

    memset(b, 0, sizeof(*b));
    b->pa   = pa;
    b->kind = (u8)kind;
    b->grow = opt.grow ? 1 : 0;

    /* 上限不能小于初始容量；grow=0 时上限就是 size */
    b->max_size = opt.max_size;
    if (b->max_size && b->max_size < size) b->max_size = size;
    if (!b->grow) b->max_size = size;

    switch (kind) {
        case PbufferSlot: {
            if (!size) return false;

            u64 lv = parena_get_slot_level(pa, size);
            if (lv >= pa->slot_level.cap) return false;

            void *ptr = parena_alloc_slot(pa, size);
            if (!ptr) return false;

            b->u.slot_ptr = ptr;
            /* slot 实际可用容量是所在等级的大小，而不是请求的 size */
            b->cap = slot_size_at(pa, lv);
            break;
        }
        case PbufferChain:
            pchain_init(pa, &b->u.chain);
            b->cap = 0;
            if (size){
                if (!pchain_append(&b->u.chain, size)){
                    pchain_recycle(&b->u.chain);
                    return false;
                }
                b->cap = size;
            }
            break;
        default:
            return false;
    }

    return true;
}

Pbuffer *pbuffer_new_imple(Parena *pa, PbufferKind kind, size_t size, PbufferOpt opt){
    if (!pa) return NULL;

    Pbuffer *b = parena_alloc_slot(pa, sizeof(Pbuffer));
    if (!b) return NULL;

    if (!pbuffer_init_imple(b, pa, kind, size, opt)){
        parena_recycle_slot(b);
        return NULL;
    }
    return b;
}

/* ==================== 写 / 读 ==================== */

bool pbuffer_extend(Pbuffer *b, size_t extra){
    if (unlikely(!b) || b->kind != PbufferChain) return false;
    if (b->cap >= b->count + extra) return true;
    if (!b->grow) return false;

    u64 want = b->count + extra;
    if (b->max_size && want > b->max_size) want = b->max_size;

    if (want > b->cap){
        u64 add = want - b->cap;
        if (!pchain_append(&b->u.chain, (size_t)add)) return false;
        b->cap += add;
    }

    return b->cap >= b->count + extra;
}

size_t pbuffer_write(Pbuffer *b, const void *src, size_t n){
    if (unlikely(!b || !src || !n)) return 0;

    if (b->count + n > b->cap){
        /* chain + grow 先尝试扩容；slot 或无增长权限则截断 */
        if (b->kind == PbufferChain && b->grow)
            pbuffer_extend(b, n);

        if (b->count + n > b->cap){
            if (b->count >= b->cap) return 0;
            n = (size_t)(b->cap - b->count);
        }
    }

    if (b->kind == PbufferSlot){
        memcpy((u8 *)b->u.slot_ptr + b->count, src, n);
    }else{
        chain_copy_in(&b->u.chain, b->count, src, n);
    }

    b->count += n;
    return n;
}

size_t pbuffer_read(Pbuffer *b, void *dst, size_t n){
    if (unlikely(!b || !dst || !n)) return 0;

    u64 avail = b->count - b->offset;
    if (n > avail) n = (size_t)avail;
    if (!n) return 0;

    if (b->kind == PbufferSlot){
        memcpy(dst, (u8 *)b->u.slot_ptr + b->offset, n);
    }else{
        chain_copy_out(&b->u.chain, b->offset, dst, n);
    }

    b->offset += n;
    return n;
}

size_t pbuffer_peek(Pbuffer *b, void *dst, size_t n){
    if (unlikely(!b || !dst || !n)) return 0;

    u64 avail = b->count - b->offset;
    if (n > avail) n = (size_t)avail;
    if (!n) return 0;

    if (b->kind == PbufferSlot)
        memcpy(dst, (u8 *)b->u.slot_ptr + b->offset, n);
    else
        chain_copy_out(&b->u.chain, b->offset, dst, n);

    return n;
}

/* ==================== 复位 / 释放 ==================== */

void pbuffer_reset(Pbuffer *b){
    if (!b) return;

    if (b->kind == PbufferChain){
        pchain_recycle(&b->u.chain);
        pchain_init(b->pa, &b->u.chain);
        b->cap = 0;
    }

    b->count  = 0;
    b->offset = 0;
}

void pbuffer_deinit(Pbuffer *b){
    if (!b) return;

    if (b->kind == PbufferSlot){
        if (b->u.slot_ptr) parena_recycle_slot(b->u.slot_ptr);
    } else {
        if (b->u.chain.parena) pchain_recycle(&b->u.chain);
    }

    b->u.slot_ptr = NULL;
    b->pa     = NULL;
    b->cap    = 0;
    b->count  = 0;
    b->offset = 0;
}

void pbuffer_free(Pbuffer *b){
    if (!b) return;

    Parena *pa = b->pa;
    pbuffer_deinit(b);
    if (pa) parena_recycle_slot(b);
}

#endif
