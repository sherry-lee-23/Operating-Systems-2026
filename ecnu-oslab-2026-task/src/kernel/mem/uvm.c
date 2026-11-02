#include "mod.h"

/*--------------------part-1: 关于内核空间<->用户空间的数据传递--------------------*/

// 用户地址不能当普通指针解引用: satp此时指向内核页表, 直接访问会查到错误映射
// 必须走用户页表翻译出PA, 返回指向该字节的内核可直接访问地址
static uint64 uvm_user_pa(pgtbl_t pgtbl, uint64 va)
{
    pte_t *pte = vm_getpte(pgtbl, va, false);
    if (pte == 0 || !(*pte & PTE_V) || !(*pte & PTE_U))
        panic("uvm_copy: invalid user address");
    return PTE_TO_PA(*pte) + (va & (PGSIZE - 1));
}

// 用户态地址空间[src, src+len) 拷贝至 内核态地址空间[dst, dst+len)
// 注意: src dst 不一定是 page-aligned
void uvm_copyin(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 len)
{
    char *kdst = (char *)dst;

    // 逐页翻译: 相邻VA的PA不一定相邻, 且首/尾页存在页内偏移
    for (uint32 done = 0; done < len;)
    {
        uint64 va = src + done;
        uint32 n = MIN(len - done, (uint32)(PGSIZE - (va & (PGSIZE - 1))));

        memmove(kdst + done, (const void *)uvm_user_pa(pgtbl, va), n);
        done += n;
    }
}

// 内核态地址空间[src, src+len） 拷贝至 用户态地址空间[dst, dst+len)
// 注意: src dst 不一定是 page-aligned
void uvm_copyout(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 len)
{
    const char *ksrc = (const char *)src;

    for (uint32 done = 0; done < len;)
    {
        uint64 va = dst + done;
        uint32 n = MIN(len - done, (uint32)(PGSIZE - (va & (PGSIZE - 1))));

        memmove((void *)uvm_user_pa(pgtbl, va), ksrc + done, n);
        done += n;
    }
}

// 用户态字符串拷贝到内核态
// 最多拷贝maxlen字节, 中途遇到'\0'则终止
// 注意: src dst 不一定是 page-aligned
void uvm_copyin_str(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 maxlen)
{
    char *kdst = (char *)dst;

    // maxlen同时限制读和写: 最多写dst[0, maxlen), 不补写dst[maxlen]
    for (uint32 done = 0; done < maxlen;)
    {
        uint64 va = src + done;
        uint32 n = MIN(maxlen - done, (uint32)(PGSIZE - (va & (PGSIZE - 1))));
        const char *usrc = (const char *)uvm_user_pa(pgtbl, va);

        for (uint32 i = 0; i < n; i++)
        {
            kdst[done + i] = usrc[i];
            if (usrc[i] == '\0')
                return;
        }
        done += n;
    }
}

/*--------------------part-2: mmap_region相关--------------------*/

// 打印以mmap为首的mmap链
// for debug
void uvm_show_mmaplist(mmap_region_t *mmap)
{
    mmap_region_t *tmp = mmap;
    printf("\nalloced mmap_space:\n");
    if (tmp == NULL)
        printf("empty\n");
    while (tmp != NULL)
    {
        printf("alloced mmap_region: %p ~ %p\n", tmp->begin, tmp->begin + tmp->npages * PGSIZE);
        tmp = tmp->next;
    }
}

// 两个 mmap_region 区域合并
// 注意: 保留一个 释放一个 不操作 next 指针
// 由uvm_mmap调用
static __attribute__((unused)) void mmap_merge(mmap_region_t *mmap_1, mmap_region_t *mmap_2, bool keep_mmap_1)
{
    // 确保有效和紧临
    assert(mmap_1 != NULL && mmap_2 != NULL, "mmap_merge: NULL");
    assert(mmap_1->begin + mmap_1->npages * PGSIZE == mmap_2->begin, "mmap_merge: check fail");

    // merge
    if (keep_mmap_1) {
        mmap_1->npages += mmap_2->npages;
        mmap_region_free(mmap_2);
    } else {
        mmap_2->begin -= mmap_1->npages * PGSIZE;
        mmap_2->npages += mmap_1->npages;
        mmap_region_free(mmap_1);
    }
}

// 寻找一块足够大的区域(len), 作为 mmap_region
// 由uvm_mmap调用(处理begin==0的情况)
// 成功返回begin, 失败返回0
static __attribute__((unused)) uint64 uvm_mmap_find(mmap_region_t *head_mmap, uint64 len, mmap_region_t **p_last_mmap, mmap_region_t **p_tmp_mmap)
{
    return 0;
}

// 在用户页表和进程mmap链里新增mmap区域 [begin, begin + npages * PGSIZE)
// 调用者保证begin是page-aligned的, 页面权限为perm
// 注意: 如果start==0, 意味着需要内核自主找一块足够大的空间
// 失败则panic卡死
void uvm_mmap(uint64 begin, uint32 npages, int perm)
{

}


// 在用户页表和进程mmap链里释放mmap区域 [begin, begin + npages * PGSIZE)
// 失败则panic卡死
void uvm_munmap(uint64 begin, uint32 npages)
{

}

/*------------------part-3: 用户空间heap和stack管理相关------------------*/

// 用户堆空间增加, 返回新的堆顶地址 (注意栈顶最大值限制)
uint64 uvm_heap_grow(pgtbl_t pgtbl, uint64 cur_heap_top, uint32 len) 
{
    uint64 new_heap_top = cur_heap_top + len;

    // 只映射新跨入的整页, 已映射的页保持不动
    uint64 begin = ALIGN_UP(cur_heap_top, PGSIZE);
    uint64 end = ALIGN_UP(new_heap_top, PGSIZE);

    for (uint64 va = begin; va < end; va += PGSIZE)
    {
        void *page = pmem_alloc(false);
        vm_mappages(pgtbl, va, (uint64)page, PGSIZE, PTE_R | PTE_W | PTE_U);
    }

    return new_heap_top;
}

// 用户堆空间减少, 返回新的堆顶地址
uint64 uvm_heap_ungrow(pgtbl_t pgtbl, uint64 cur_heap_top, uint32 len)
{
    uint64 new_heap_top = cur_heap_top - len;

    // new_heap_top 所在的那一页仍被使用, 从它的下一页开始释放
    uint64 begin = ALIGN_UP(new_heap_top, PGSIZE);
    uint64 end = ALIGN_UP(cur_heap_top, PGSIZE);

    for (uint64 va = begin; va < end; va += PGSIZE)
        vm_unmappages(pgtbl, va, PGSIZE, true);

    return new_heap_top;
}

// 处理函数栈增长导致的page fault事件
// 成功返回new_ustack_npage，失败返回-1
uint64 uvm_ustack_grow(pgtbl_t pgtbl, uint64 old_ustack_npage, uint64 fault_addr)
{
    uint64 fault_page = ALIGN_DOWN(fault_addr, PGSIZE);
    uint64 stack_bottom = TRAPFRAME - old_ustack_npage * PGSIZE;

    // 只接受向下增长, 且不能越过mmap区上界
    if (fault_page < MMAP_END || fault_page >= stack_bottom)
        return (uint64)-1;

    // 一次缺页可能跨多页: 补齐[当前栈底 - PGSIZE, fault_page]之间的全部页
    uint64 npage = old_ustack_npage;
    for (uint64 va = stack_bottom - PGSIZE;; va -= PGSIZE)
    {
        void *page = pmem_alloc(false);
        vm_mappages(pgtbl, va, (uint64)page, PGSIZE, PTE_R | PTE_W | PTE_U);
        npage++;

        if (va <= fault_page)
            break;
    }

    return npage;
}

/*----------------------part-4: 用户页表管理相关----------------------*/

// 递归释放 页表占用的物理页 和 页表管理的物理页
// ps: 顶级页表level = 3
static void destroy_pgtbl(pgtbl_t pgtbl, uint32 level)
{

}

// 页表销毁
void uvm_destroy_pgtbl(pgtbl_t pgtbl)
{
    vm_unmappages(pgtbl, TRAPFRAME, PGSIZE, true);   // 可以释放，因为trapframe是每个进程独有的
    vm_unmappages(pgtbl, TRAMPOLINE, PGSIZE, false); // 不能释放，因为所有进程共用区域
    destroy_pgtbl(pgtbl, 3);
}

// 连续虚拟空间的复制
// 在uvm_copy_pgtbl中使用
static __attribute__((unused)) void copy_range(pgtbl_t old, pgtbl_t new, uint64 begin, uint64 end)
{
    uint64 va, pa, page;
    int flags;
    pte_t *pte;

    for (va = begin; va < end; va += PGSIZE)
    {
        pte = vm_getpte(old, va, false);
        assert(pte != NULL, "uvm_copy_pgtbl: pte == NULL");
        assert((*pte) & PTE_V, "uvm_copy_pgtbl: pte not valid");

        pa = (uint64)PTE_TO_PA(*pte);
        flags = (int)PTE_FLAGS(*pte);

        page = (uint64)pmem_alloc(false);
        memmove((char *)page, (const char *)pa, PGSIZE);
        vm_mappages(new, va, page, PGSIZE, flags);
    }
}

// 拷贝页表 (拷贝并不包括 trapframe 和 trampoline)
// 拷贝的页表管理的物理页是原来页表的复制品
void uvm_copy_pgtbl(pgtbl_t old, pgtbl_t new, uint64 heap_top, uint64 ustack_npage, mmap_region_t *mmap)
{

}
