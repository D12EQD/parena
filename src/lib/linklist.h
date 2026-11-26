#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <assert.h>

#define list_ptr(val) ((List *)(val))
#define list_is_empty(val) ((!((list_ptr(val)))->begin))

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

