#include "arch/mod.h"
#include "lib/mod.h"
#include "lock/mod.h"
#include "mem/mod.h"
#include "trap/mod.h"
#include "proc/mod.h"

// CPU0完成全局初始化后置1, 其他CPU在此等待
static volatile int boot_ready = 0;

void main()
{
    uart_init();
    printf("cpu %d is booting!\n", mycpuid());

    if (mycpuid() == 0) {
        // 全局资源只初始化一次
        pmem_init();
        kvm_init();
        kvm_inithart();     // 当前CPU切换到内核页表

        trap_kernel_init(); // 全局：PLIC + timer_create

        __sync_synchronize();
        boot_ready = 1;
    } else {
        // 等待全局初始化完成, 否则可能使用尚未建立的页表
        while (!boot_ready)
            ;
        __sync_synchronize();
        kvm_inithart();
    }

    // 每核：PLIC 使能 + stvec + 开中断
    trap_kernel_inithart();
    printf("cpu %d: trap enabled\n", mycpuid());

    // 创建并启动第一个用户进程(只需在CPU0上做)
    if (mycpuid() == 0)
        proc_make_first();

    while (1)
        asm volatile("wfi");
}