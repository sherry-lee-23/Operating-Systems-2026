#include "mod.h"

/* 测试用: sys_copyin 允许一次接收的最大元素数 */
#define SYS_COPYIN_MAX 64

/*
    测试: 从用户空间传入一个int类型的数组
    uint64 addr 数组起始地址
    uint32 len  元素数量
    成功返回0
*/
uint64 sys_copyin()
{
    uint64 addr;
    uint32 len;
    arg_uint64(0, &addr);
    arg_uint32(1, &len);

    if (len > SYS_COPYIN_MAX) {
        printf("sys_copyin: too many elements (%d)\n", (int)len);
        return (uint64)-1;
    }

    int kbuf[SYS_COPYIN_MAX];
    proc_t *p = myproc();
    uvm_copyin(p->pgtbl, (uint64)kbuf, addr, len * sizeof(int));

    printf("sys_copyin: ");
    for (uint32 i = 0; i < len; i++)
        printf("%d ", kbuf[i]);
    printf("\n");

    return 0;
}

/*
    测试: 向用户空间传出一个int类型的数组
    uint64 addr 数组起始地址
    成功返回拷贝的元素数量
*/
uint64 sys_copyout()
{
    uint64 addr;
    arg_uint64(0, &addr);

    int kbuf[5] = {1, 2, 3, 4, 5};
    proc_t *p = myproc();
    uvm_copyout(p->pgtbl, addr, (uint64)kbuf, sizeof(kbuf));

    return 5;
}

/*
    测试: 从用户空间传入一个字符串
    uint64 addr 字符串起始地址
    成功返回0
*/
uint64 sys_copyinstr()
{
    uint64 addr;
    arg_uint64(0, &addr);

    // uvm_copyin_str 最多写 maxlen 字节且不补写 buf[maxlen],
    // 因此留一个字节由调用方补终止符
    char buf[STR_MAXLEN + 1];
    proc_t *p = myproc();
    uvm_copyin_str(p->pgtbl, (uint64)buf, addr, STR_MAXLEN);
    buf[STR_MAXLEN] = 0;

    printf("sys_copyinstr: %s\n", buf);

    return 0;
}

/*
    用户堆空间伸缩
    uint64 new_heap_top (如果是0, 代表查询当前堆顶位置)
    成功返回new_heap_top, 失败返回-1
*/
uint64 sys_brk()
{
    uint64 new_heap_top;
    arg_uint64(0, &new_heap_top);

    proc_t *p = myproc();

    // 查询当前堆顶
    if (new_heap_top == 0)
    {
        printf("sys_brk: query heap_top = %x, heap_base = %x\n", p->heap_top, p->heap_base);
        return p->heap_top;
    }

    // 不能低于初始镜像保留边界, 也不能压到mmap区
    if (new_heap_top < p->heap_base || new_heap_top > MMAP_BEGIN)
    {
        printf("sys_brk: invalid heap_top = %x (heap_base = %x, MMAP_BEGIN = %x)\n",
               new_heap_top, p->heap_base, MMAP_BEGIN);
        return (uint64)-1;
    }

    if (new_heap_top > p->heap_top)
    {
        p->heap_top = uvm_heap_grow(p->pgtbl, p->heap_top,
                                    (uint32)(new_heap_top - p->heap_top));
        printf("sys_brk: grow to %x (heap_base = %x)\n", p->heap_top, p->heap_base);
    }
    else if (new_heap_top < p->heap_top)
    {
        p->heap_top = uvm_heap_ungrow(p->pgtbl, p->heap_top,
                                      (uint32)(p->heap_top - new_heap_top));
        printf("sys_brk: shrink to %x (heap_base = %x)\n", p->heap_top, p->heap_base);
    }
    else
    {
        printf("sys_brk: unchanged heap_top = %x\n", p->heap_top);
    }

    return p->heap_top;
}

/*
    增加一段内存映射
    uint64 start 起始地址
    uint32 len   范围 (字节,需检查是否是page-aligned)
    成功返回映射空间的起始地址, 失败返回-1
*/
uint64 sys_mmap()
{
    uint64 begin, len;
    arg_uint64(0, &begin);
    arg_uint64(1, &len);

    if (len == 0 || len % PGSIZE != 0)
    {
        printf("sys_mmap: invalid len = %x\n", len);
        return (uint64)-1;
    }

    proc_t *p = myproc();

    if (begin != 0)
    {
        uint64 end = begin + len;
        if (begin % PGSIZE != 0 || begin < MMAP_BEGIN || end > MMAP_END)
        {
            printf("sys_mmap: invalid range, begin = %x, len = %x\n", begin, len);
            return (uint64)-1;
        }
        // 与已分配区域重叠则拒绝, 避免重复建PTE与后续double free
        for (mmap_region_t *m = p->mmap; m != NULL; m = m->next)
        {
            uint64 m_begin = m->begin;
            uint64 m_end = m_begin + (uint64)m->npages * PGSIZE;
            if (begin < m_end && m_begin < end)
            {
                printf("sys_mmap: overlap with [%x, %x)\n", m_begin, m_end);
                return (uint64)-1;
            }
        }
    }

    uint32 npages = (uint32)(len / PGSIZE);
    uint64 start = uvm_mmap(begin, npages, PTE_R | PTE_W);

    printf("\nsys_mmap: begin = %x, npages = %d, ret = %x\n", begin, (int)npages, start);
    if (start == (uint64)-1)
    {
        printf("sys_mmap: failed\n");
        return (uint64)-1;
    }

    uvm_show_mmaplist(p->mmap);
    vm_print(p->pgtbl);
    printf("\n");

    return start;
}

/*
    解除一段内存映射
    uint64 start 起始地址
    uint32 len   范围 (字节, 需检查是否是page-aligned)
    成功返回0 失败返回-1
*/
uint64 sys_munmap()
{
    uint64 begin, len;
    arg_uint64(0, &begin);
    arg_uint64(1, &len);

    if (len == 0 || len % PGSIZE != 0 || begin % PGSIZE != 0 ||
        begin < MMAP_BEGIN || begin + len > MMAP_END)
    {
        printf("sys_munmap: invalid range, begin = %x, len = %x\n", begin, len);
        return (uint64)-1;
    }

    uint32 npages = (uint32)(len / PGSIZE);
    printf("\nsys_munmap: begin = %x, npages = %d\n", begin, (int)npages);
    uvm_munmap(begin, npages);

    proc_t *p = myproc();
    uvm_show_mmaplist(p->mmap);
    vm_print(p->pgtbl);
    printf("\n");

    return 0;
}