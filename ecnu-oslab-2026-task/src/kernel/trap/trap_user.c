#include "mod.h"
#include "../../user/syscall_num.h"

// in trampoline.S
extern char trampoline[];  // 内核和用户切换的代码
extern char user_vector[]; // 用户触发陷阱进入内核
extern char user_return[]; // 内核处理完毕返回用户

// in trap.S
extern char kernel_vector[]; // 内核态trap处理流程, 进入内核后应当切换中断处理入口

// in trap_kernel.c
extern char *interrupt_info[16]; // 中断错误信息
extern char *exception_info[16]; // 异常错误信息

// 在user_vector()里面调用
// 用户态trap处理的核心逻辑
void trap_user_handler()
{
    // 处理期间若再次发生trap, 交给内核态的处理入口
    w_stvec((uint64)kernel_vector);

    proc_t *p = myproc();
    trapframe_t *tf = p->tf;
    uint64 scause = r_scause();

    // 记录被打断的用户PC, 返回用户态时从这里继续
    tf->user_to_kern_epc = r_sepc();

    if (scause & 0x8000000000000000ul) {
        // 中断
        uint64 id = scause & 0xf;
        if (id == 1) {
            timer_interrupt_handler(); // M-mode时钟中断转发来的S-mode软件中断
        } else if (id == 9) {
            external_interrupt_handler(); // UART等外设中断
        } else {
            printf("\nunexpected interrupt: %s\n", interrupt_info[id]);
            printf("scause = %p, sepc = %p\n", scause, r_sepc());
            panic("trap_user_handler");
        }
    } else if (scause == 8) {
        // 系统调用: Environment call from U-mode
        uint64 num = tf->a7;

        if (num == SYS_helloworld) {
            printf("proczero: hello world\n");
            tf->a0 = 0;
        } else {
            printf("unknown syscall: %d\n", (int)num);
            tf->a0 = -1;
        }

        // 跳过引发异常的ecall指令
        tf->user_to_kern_epc += 4;
    } else {
        printf("\nunexpected exception: %s\n", exception_info[scause & 0xf]);
        printf("scause = %p, sepc = %p, stval = %p\n", scause, r_sepc(), r_stval());
        panic("trap_user_handler");
    }

    // trap处理完毕, 返回用户态
    trap_user_return();
}

// 调用user_return()
// 内核态返回用户态
void trap_user_return()
{
    proc_t *p = myproc();
    trapframe_t *tf = p->tf;

    // 下次进入内核时需要的运行信息
    tf->user_to_kern_satp = r_satp();
    tf->user_to_kern_sp = p->kstack + PGSIZE;
    tf->user_to_kern_trapvector = (uint64)trap_user_handler;
    tf->user_to_kern_hartid = mycpuid();

    // 用户态发生trap后从高地址的跳板入口进入
    w_stvec(TRAMPOLINE + ((uint64)user_vector - (uint64)trampoline));

    // 返回到用户态时继续执行的PC
    w_sepc(tf->user_to_kern_epc);

    // 回到U-mode, 并允许用户态响应中断
    uint64 sstatus = r_sstatus();
    sstatus &= ~SSTATUS_SPP;
    sstatus |= SSTATUS_SPIE;
    w_sstatus(sstatus);

    // 下次进入user_vector时用来交换得到trapframe
    w_sscratch(TRAPFRAME);

    // 必须通过TRAMPOLINE处的虚拟地址调用user_return
    // 直接调用user_return符号会跳转到内核直接映射地址, 切换页表后将无法取指
    uint64 ur = TRAMPOLINE + ((uint64)user_return - (uint64)trampoline);
    ((void (*)(uint64, uint64))ur)(TRAPFRAME, MAKE_SATP(p->pgtbl));

    panic("trap_user_return: user_return returned");
}