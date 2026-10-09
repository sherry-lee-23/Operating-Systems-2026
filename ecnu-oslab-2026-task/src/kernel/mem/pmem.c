#include "mod.h"

// 内核空间和用户空间的可分配物理页分开描述
static alloc_region_t kern_region, user_region;

// 物理内存的初始化
// 本质上就是填写kern_region和user_region, 包括基本数值和空闲链表
void pmem_init(void)
{
    uint64 begin = (uint64)ALLOC_BEGIN;
    uint64 split = begin + (uint64)KERN_PAGES * PGSIZE;
    uint64 end = (uint64)ALLOC_END;

    kern_region.begin = begin;
    kern_region.end = end;
    kern_region.allocable = 0;
    kern_region.list_head.next = 0;
    spinlock_init(&kern_region.lk, "kern_region");

    user_region.begin = split;
    user_region.end = end;
    user_region.allocable = 0;
    user_region.list_head.next = 0;
    spinlock_init(&user_region.lk, "user_region");

    for (uint64 pa = begin; pa < end; pa += PGSIZE) {
        alloc_region_t *region = (pa < split) ? &kern_region : &user_region;
        page_node_t *node = (page_node_t *)pa;

        node->next = region->list_head.next;
        region->list_head.next = node;
        region->allocable++;
    }
}

// 尝试返回一个可分配的清零后的物理页
// 失败则panic锁死
void* pmem_alloc(bool in_kernel)
{
    alloc_region_t *region = in_kernel ? &kern_region : &user_region;
    page_node_t *page;

    spinlock_acquire(&region->lk);

    page = region->list_head.next;
    if (page == 0) {
        spinlock_release(&region->lk);
        panic("pmem_alloc: out of memory");
    }

    region->list_head.next = page->next;
    region->allocable--;

    spinlock_release(&region->lk);

    memset(page, 0, PGSIZE);

    return page;
}

// 释放一个物理页
// 失败则panic锁死
void pmem_free(uint64 page, bool in_kernel)
{
    alloc_region_t *region = in_kernel ? &kern_region : &user_region;

    // 非法页直接 panic(静态检查不需要锁)
    if (page % PGSIZE != 0 ||
        page < region->begin ||
        page >= region->end) {
        panic("pmem_free: invalid page");
    }

    page_node_t *node = (page_node_t *)page;

    spinlock_acquire(&region->lk);
    node->next = region->list_head.next;
    region->list_head.next = node;
    region->allocable++;
    spinlock_release(&region->lk);

}
