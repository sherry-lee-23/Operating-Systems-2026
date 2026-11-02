/*
 * Lab5 阶段5「页表复制与销毁」内核自测（official-test-5 的可复现证据）
 *
 * 说明:
 *  - 这是临时的内核自测代码, 不参与常规构建: tests/ 不在 Makefile 的源码通配
 *    范围(src/kernel 下一级与二级目录中的 .c 文件)内, 直接放在 tests/ 下不会被编译。
 *  - 使用时需要拷贝到 src/kernel/mem/uvm_selftest.c, 并在 main.c 中临时加钩子;
 *    接入、构建、运行、截图后清理的完整步骤见 tests/stage5-selftest.md。
 *  - 文件内的相对 include 路径("mod.h" 与 "../proc/mod.h")是按拷贝后位于
 *    src/kernel/mem/ 下的位置书写的, 因此不要直接在 tests/ 下编译。
 *  - 自测只调用公开接口(proc_pgtbl_init / uvm_copy_pgtbl / uvm_destroy_pgtbl /
 *    vm_getpte / vm_mappages / pmem_alloc / uvm_copyin), 不修改任何内核逻辑。
 */

#include "mod.h"
#include "../proc/mod.h"

extern alloc_region_t kern_region, user_region;
extern char trampoline[];

#define T_CODE_VA  0UL
#define T_HEAP_VA  ((uint64)PGSIZE)
#define T_MMAP_VA  ((uint64)MMAP_BEGIN)
#define T_STACK_VA ((uint64)(TRAPFRAME - PGSIZE))

static uint64 pa_of(pgtbl_t pg, uint64 va)
{
    pte_t *pte = vm_getpte(pg, va, false);
    assert(pte != 0, "selftest: pte not found");
    assert((*pte) & PTE_V, "selftest: pte not valid");
    return PTE_TO_PA(*pte);
}

static bool all_eq(const char *p, char c, uint32 n)
{
    for (uint32 i = 0; i < n; i++)
        if (p[i] != c)
            return false;
    return true;
}

void uvm_selftest(void)
{
    bool ok = true;

    printf("\n===== stage5 selftest begin =====\n");

    /* ---- 构造父进程页表(与 proczero 布局一致) ---- */
    uint64 ptf = (uint64)pmem_alloc(true);
    pgtbl_t parent = proc_pgtbl_init(ptf);

    void *code = pmem_alloc(false);
    void *heap = pmem_alloc(false);
    void *mmap = pmem_alloc(false);
    void *stk = pmem_alloc(false);
    memset(code, 'C', PGSIZE);
    memset(heap, 'H', PGSIZE);
    memset(mmap, 'M', PGSIZE);
    memset(stk, 'S', PGSIZE);
    vm_mappages(parent, T_CODE_VA, (uint64)code, PGSIZE, PTE_R | PTE_W | PTE_X | PTE_U);
    vm_mappages(parent, T_HEAP_VA, (uint64)heap, PGSIZE, PTE_R | PTE_W | PTE_U);
    vm_mappages(parent, T_MMAP_VA, (uint64)mmap, PGSIZE, PTE_R | PTE_W | PTE_U);
    vm_mappages(parent, T_STACK_VA, (uint64)stk, PGSIZE, PTE_R | PTE_W | PTE_U);

    /* ---- 子进程: 独立根页表 + 独立 trapframe, 再复制 ---- */
    uint64 ctf = (uint64)pmem_alloc(true);
    pgtbl_t child = proc_pgtbl_init(ctf);
    uvm_copy_pgtbl(parent, child, T_HEAP_VA + PGSIZE, 2, NULL);

    uint64 vas[4] = {T_CODE_VA, T_HEAP_VA, T_MMAP_VA, T_STACK_VA};
    char tag[4] = {'C', 'H', 'M', 'S'};
    for (int i = 0; i < 4; i++)
    {
        uint64 p_pa = pa_of(parent, vas[i]);
        uint64 c_pa = pa_of(child, vas[i]);
        bool diff = (p_pa != c_pa);
        bool same = all_eq((char *)c_pa, tag[i], PGSIZE);
        printf("copy page[%d] va=%x parent_pa=%x child_pa=%x 独立=%d 内容一致=%d\n",
               i, vas[i], p_pa, c_pa, (int)diff, (int)same);
        ok = ok && diff && same;
    }

    /* ---- trapframe 私有 / trampoline 共享 ---- */
    uint64 p_tf = pa_of(parent, TRAPFRAME);
    uint64 c_tf = pa_of(child, TRAPFRAME);
    uint64 p_tr = pa_of(parent, TRAMPOLINE);
    uint64 c_tr = pa_of(child, TRAMPOLINE);
    bool tf_ok = (p_tf != c_tf) && (c_tf == ctf);
    bool tr_ok = (p_tr == c_tr) && (p_tr == (uint64)trampoline);
    printf("trapframe  parent_pa=%x child_pa=%x 私有=%d\n", p_tf, c_tf, (int)tf_ok);
    printf("trampoline parent_pa=%x child_pa=%x 共享=%d\n", p_tr, c_tr, (int)tr_ok);
    ok = ok && tf_ok && tr_ok;

    /* ---- 双向写隔离 ---- */
    memset((void *)pa_of(parent, T_HEAP_VA), 'X', 16);
    bool w1 = all_eq((char *)pa_of(child, T_HEAP_VA), 'H', 16);
    memset((void *)pa_of(child, T_MMAP_VA), 'Y', 16);
    bool w2 = all_eq((char *)pa_of(parent, T_MMAP_VA), 'M', 16);
    printf("write isolation: 父写不影响子=%d 子写不影响父=%d\n", (int)w1, (int)w2);
    ok = ok && w1 && w2;

    /* ---- 销毁子进程 ---- */
    uint32 u_before = user_region.allocable;
    uint32 k_before = kern_region.allocable;
    uvm_destroy_pgtbl(child);
    uint32 u_freed = user_region.allocable - u_before;
    uint32 k_freed = kern_region.allocable - k_before;
    printf("destroy child: 释放用户页=%d(期望4) 释放内核页=%d(根+trapframe+页表页, >=2)\n",
           (int)u_freed, (int)k_freed);
    ok = ok && (u_freed == 4) && (k_freed >= 2);

    /* ---- 子进程销毁后父进程仍可用 ---- */
    char buf[8];
    uvm_copyin(parent, (uint64)buf, T_CODE_VA, 4);
    bool alive = all_eq(buf, 'C', 4);
    printf("destroy child 后父页表仍可读写: %d\n", (int)alive);
    ok = ok && alive;

    /* ---- 销毁父进程: trampoline 若被误释放会 panic ---- */
    u_before = user_region.allocable;
    k_before = kern_region.allocable;
    uvm_destroy_pgtbl(parent);
    u_freed = user_region.allocable - u_before;
    k_freed = kern_region.allocable - k_before;
    printf("destroy parent: 释放用户页=%d(期望4) 释放内核页=%d, 未 panic => trampoline 未被释放\n",
           (int)u_freed, (int)k_freed);
    ok = ok && (u_freed == 4) && (k_freed >= 2);

    printf(ok ? "===== stage5 selftest PASS =====\n" : "===== stage5 selftest FAIL =====\n");
}

/*
 * 使用方式(详见 tests/stage5-selftest.md):
 *
 *   1) cp tests/stage5-selftest.c src/kernel/mem/uvm_selftest.c
 *   2) 在 src/kernel/main.c 的 mmap_init(); 之后临时加入:
 *          extern void uvm_selftest(void);
 *          uvm_selftest();
 *   3) make clean && make build && make run
 *   4) 截图 official-test-5.png 后清理:
 *          rm src/kernel/mem/uvm_selftest.c
 *          删除 main.c 中的 extern 与调用
 *          make clean && make build          # 确认去掉钩子后仍可构建
 *          git status                         # 内核侧应无残留, tests/ 下两个文件保留
 *
 * 预期输出:
 *   copy page[0..3] ... 独立=1 内容一致=1
 *   trapframe  ... 私有=1
 *   trampoline ... 共享=1
 *   write isolation: 父写不影响子=1 子写不影响父=1
 *   destroy child: 释放用户页=4 ...
 *   destroy child 后父页表仍可读写: 1
 *   destroy parent: ... 未 panic => trampoline 未被释放
 *   ===== stage5 selftest PASS =====
 */
