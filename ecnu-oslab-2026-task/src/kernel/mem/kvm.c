#include "mod.h"

// 内核页表
static pgtbl_t kernel_pgtbl;

// 根据pagetable,找到va对应的pte
// 若设置alloc=true 则在PTE无效时尝试申请一个物理页
// 成功返回PTE, 失败返回NULL
// 提示：使用 VA_TO_VPN + PTE_TO_PA + PA_TO_PTE
pte_t *vm_getpte(pgtbl_t pgtbl, uint64 va, bool alloc)
{
    if (va >= VA_MAX) return 0;

    pte_t *pgt = (pte_t *)pgtbl;
    for(int level = 2; level > 0; level--){
        pte_t *pte = &pgt[VA_TO_VPN(va, level)];
        if (*pte & PTE_V){
            pgt = (pte_t *)PTE_TO_PA(*pte);
        }else{
            if(!alloc) return 0;
            pgt = (pte_t *)pmem_alloc(true);
            *pte = PA_TO_PTE((uint64)pgt) | PTE_V;
        }
    }
    return &pgt[VA_TO_VPN(va,0)];
}

// 在pgtbl中建立 [va, va + len) -> [pa, pa + len) 的映射
// 本质是找到va在页表对应位置的pte并修改它
// 检查: va pa 应当是 page-aligned, len(字节数) > 0, va + len <= VA_MAX
// 注意: perm 应该如何使用
void vm_mappages(pgtbl_t pgtbl, uint64 va, uint64 pa, uint64 len, int perm)
{
    if (len == 0) return;
    if (va % PGSIZE != 0) panic("vm_mappages: va not aligned");
    if (pa % PGSIZE != 0) panic("vm_mappages: pa not aligned");
    if (va + len > VA_MAX) panic("vm_mappages: va overflow");

    uint64 pages = (len + PGSIZE - 1) / PGSIZE;

    for (uint64 i = 0; i < pages; i++) {
        uint64 cur_va = va + i * PGSIZE;
        uint64 cur_pa = pa + i * PGSIZE;

        pte_t *pte = vm_getpte(pgtbl, cur_va, true);
        if (pte == 0)          panic("vm_mappages: getpte failed");
        if (*pte & PTE_V)      panic("vm_mappages: remap");

        *pte = PA_TO_PTE(cur_pa) | (uint64)perm | PTE_V;
    }
}

// 解除pgtbl中[va, va+len)区域的映射
// 如果freeit == true则释放对应物理页, 默认是用户的物理页
void vm_unmappages(pgtbl_t pgtbl, uint64 va, uint64 len, bool freeit)
{
    if (len == 0) return;
    if (va % PGSIZE != 0)  panic("vm_unmappages: va not aligned");
    if (va + len > VA_MAX) panic("vm_unmappages: va overflow");

    uint64 pages = (len + PGSIZE - 1) / PGSIZE;

    for (uint64 i = 0; i < pages; i++) {
        uint64 cur_va = va + i * PGSIZE;

        pte_t *pte = vm_getpte(pgtbl, cur_va, false);
        if (pte == 0)              panic("vm_unmappages: pte not found");
        if (!(*pte & PTE_V))       panic("vm_unmappages: already unmapped");

        if (freeit) {
            pmem_free(PTE_TO_PA(*pte), false);
        }
        *pte = 0;
    }
}

// 完成UART、CLINT、PLIC、内核代码区、内核数据区、可分配区域的页表映射
// 相当于部分填充kernel_pgtbl
void kvm_init()
{
    kernel_pgtbl = (pgtbl_t)pmem_alloc(true);

    // 硬件寄存器区(直接映射)
    vm_mappages(kernel_pgtbl, UART_BASE,  UART_BASE,  0x1000UL,   PTE_R | PTE_W); // UART
    vm_mappages(kernel_pgtbl, CLINT_BASE, CLINT_BASE, 0x10000UL,  PTE_R | PTE_W); // CLINT
    vm_mappages(kernel_pgtbl, PLIC_BASE,  PLIC_BASE,  0x400000UL, PTE_R | PTE_W); // PLIC
    
    vm_mappages(kernel_pgtbl,
                KERNEL_BASE, KERNEL_BASE,
                128UL * 1024 * 1024,
                PTE_R | PTE_W | PTE_X);

}

// 每个CPU都需要调用, 从不使用页表切换到使用内核页表
// 切换后需要刷新TLB里面的缓存
void kvm_inithart()
{
    w_satp(MAKE_SATP(kernel_pgtbl));
    sfence_vma();
}

// 输出页表内容(for debug)
void vm_print(pgtbl_t pgtbl)
{
    // 顶级页表，次级页表，低级页表
    pgtbl_t pgtbl_2 = pgtbl, pgtbl_1 = NULL, pgtbl_0 = NULL;
    pte_t pte;

    printf("level-2 pgtbl: pa = %p\n", pgtbl_2);
    for (int i = 0; i < PGSIZE / sizeof(pte_t); i++)
    {
        pte = pgtbl_2[i];
        if (!((pte)&PTE_V))
            continue;
        assert(PTE_CHECK(pte), "vm_print: pte check fail (1)");
        pgtbl_1 = (pgtbl_t)PTE_TO_PA(pte);
        printf(".. level-1 pgtbl %d: pa = %p\n", i, pgtbl_1);

        for (int j = 0; j < PGSIZE / sizeof(pte_t); j++)
        {
            pte = pgtbl_1[j];
            if (!((pte)&PTE_V))
                continue;
            assert(PTE_CHECK(pte), "vm_print: pte check fail (2)");
            pgtbl_0 = (pgtbl_t)PTE_TO_PA(pte);
            printf(".. .. level-0 pgtbl %d: pa = %p\n", j, pgtbl_0);

            for (int k = 0; k < PGSIZE / sizeof(pte_t); k++)
            {
                pte = pgtbl_0[k];
                if (!((pte)&PTE_V))
                    continue;
                assert(!PTE_CHECK(pte), "vm_print: pte check fail (3)");
                printf(".. .. .. physical page %d: pa = %p flags = %d\n", k, (uint64)PTE_TO_PA(pte), (int)PTE_FLAGS(pte));
            }
        }
    }
}
