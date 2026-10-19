#include "mod.h"

// 这个文件通过make build生成, 是proczero对应的ELF文件
#include "../../user/initcode.h"
#define initcode target_user_initcode
#define initcode_len target_user_initcode_len

// in trampoline.S
extern char trampoline[];

// in swtch.S
extern void swtch(context_t *old, context_t *new);

// in trap/trap_user.c
extern void trap_user_return();

// 第一个用户进程
static proc_t proczero;

// 获得一个初始化过的用户页表
// 完成trapframe和trampoline的映射
pgtbl_t proc_pgtbl_init(uint64 trapframe)
{
    // 页表页由内核管理, 从内核物理内存区分配
    // pmem_alloc 已经将返回的页面清零
    pgtbl_t pgtbl = (pgtbl_t)pmem_alloc(true);
    assert(pgtbl != 0, "proc_pgtbl_init: no memory for page table");

    // 跳板页只由S-mode执行, 不能设置PTE_U, 否则S-mode反而无法访问
    vm_mappages(pgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

    // trapframe由跳板在S-mode下读写, 同样不能设置PTE_U
    vm_mappages(pgtbl, TRAPFRAME, trapframe, PGSIZE, PTE_R | PTE_W);

    return pgtbl;
}

/*
    第一个用户态进程的创建
    它的代码和数据位于initcode.h的initcode数组

    第一个进程的用户地址空间布局:
    trapoline   (1 page)
    trapframe   (1 page)
    ustack      (1 page)
    .......
                        <--heap_top
    code + data (1 page, 从0地址开始)

	注意: 用用户空间的地址映射需要标记 PTE_U
*/
void proc_make_first()
{
    proc_t *p = &proczero;
    p->pid = 0;

    // trapframe: 内核通过物理地址初始化, 同时会映射到用户页表的TRAPFRAME
    p->tf = (trapframe_t *)pmem_alloc(true);
    assert(p->tf != 0, "proc_make_first: no memory for trapframe");

    // 用户页表: 完成trampoline和trapframe的映射
    p->pgtbl = proc_pgtbl_init((uint64)p->tf);
    assert(p->pgtbl != 0, "proc_make_first: no memory for page table");

    // 用户栈: 一页, 紧邻trapframe下方
    // 注意用户栈需要 PTE_U, 且栈顶位于该页的高地址端(TRAPFRAME)
    p->ustack_npage = 1;
    void *ustack = pmem_alloc(false);
    assert(ustack != 0, "proc_make_first: no memory for user stack");
    vm_mappages(p->pgtbl, TRAPFRAME - PGSIZE, (uint64)ustack,
                PGSIZE, PTE_R | PTE_W | PTE_U);

    // 用户代码段: initcode从0地址开始执行
    assert(initcode_len <= PGSIZE, "proc_make_first: initcode too large");
    void *ucode = pmem_alloc(false);
    assert(ucode != 0, "proc_make_first: no memory for user code");
    memmove(ucode, initcode, initcode_len);
    vm_mappages(p->pgtbl, 0, (uint64)ucode,
                PGSIZE, PTE_R | PTE_W | PTE_X | PTE_U);

    // 堆从代码段之后的地址开始向上增长
    p->heap_top = PGSIZE;

    // 用户态初始现场
    p->tf->user_to_kern_epc = 0;     // 用户程序从0地址开始执行
    p->tf->sp = TRAPFRAME;           // 用户栈顶

    // 内核栈与首次进入trap_user_return的内核上下文
    p->kstack = KSTACK(0);           // 内核栈页的基址
    p->tf->user_to_kern_sp = p->kstack + PGSIZE; // 内核栈顶
    p->ctx.ra = (uint64)trap_user_return;
    p->ctx.sp = p->kstack + PGSIZE;

    // 将当前CPU绑定到proczero, 并切换到它的内核执行流
    mycpu()->proc = p;
    swtch(&mycpu()->ctx, &p->ctx);
}