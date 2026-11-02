/*
 * Lab5 边界自测：D2 契约第 1 问「new_heap_top < heap_base 必须拒绝」
 *
 * 对应提交清单: tests/self-test.md + tests/self-test.png
 *
 * 用法(截图时由使用者自己拷入, 本文件不自动替换 initcode.c):
 *   cp tests/self-test-initcode.c src/user/initcode.c
 *   make clean && make build && make run
 *
 * 注意: 不写死 heap_base 的数值, 一律先 brk(0) 查询再据此构造非法请求,
 *       这样即使 heap_base 的计算方式变化, 本测试仍然成立。
 */

#include "sys.h"

#define PGSIZE 4096

int main()
{
    /* 1. 查询当前堆顶: 进程刚创建、尚未 brk 过, heap_top 应等于 heap_base */
    long long base = syscall(SYS_brk, 0);

    /* 2. 请求把堆顶降到 heap_base 以下(低一页), 预期被拒绝并返回 -1 */
    long long r = syscall(SYS_brk, base - PGSIZE);
    if (r != -1)
    {
        /* 预期走不到这里: 说明内核没有用 heap_base 做下界检查 */
        syscall(SYS_copyinstr, "FAIL: brk accepted below heap_base");
    }

    /* 3. 被拒绝后堆顶必须保持不变(非法请求不能产生任何副作用) */
    long long after = syscall(SYS_brk, 0);
    if (after != base)
    {
        syscall(SYS_copyinstr, "FAIL: heap_top changed by rejected brk");
    }

    /* 4. 证明拒绝后代码/数据页仍被映射:
     *    若 brk 误把代码页解除, 下面任何一次 ecall 都会 page fault / panic */
    int L[5];
    syscall(SYS_copyout, L);               /* 内核写 [1,2,3,4,5] 到用户数组 L */
    syscall(SYS_copyin, L, 5);             /* 用户回传, 内核打印 */
    syscall(SYS_copyinstr, "still alive"); /* 证明代码段/字符串仍可读 */

    while (1)
        ;
    return 0;
}
