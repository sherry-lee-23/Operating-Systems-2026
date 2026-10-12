#include "arch/mod.h"
#include "lib/mod.h"
#include "lock/mod.h"
#include "mem/mod.h"
#include "trap/mod.h"

void main()
{
    uart_init();
    printf("cpu %d is booting!\n", mycpuid());

    // lab2 内存初始化（按实际情况补）
    // pmem_init();
    // kvm_init();
    // kvm_inithart();

    if (mycpuid() == 0)
        trap_kernel_init();     // 全局：PLIC + timer_create

    trap_kernel_inithart();     // 每核：PLIC 使能 + stvec + 开中断

    printf("cpu %d: trap enabled\n", mycpuid());

    while (1)
        asm volatile("wfi");
}