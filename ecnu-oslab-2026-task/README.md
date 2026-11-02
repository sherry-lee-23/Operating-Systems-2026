# LAB-5: 系统调用流程建立 + 用户态虚拟内存管理

**前言**

在lab-4中, 我们初步实现了第一个用户进程`proczero`

它通过`sys_helloworld`系统调用, 利用内核的系统服务发出了"第一声啼哭"

本次实验的核心目的是完善和发展`proczero`, 具体包括两个方面:

- 赋予`proczero`更强的内存掌控能力, 包括堆、栈、离散映射三个部分

- 赋予`proczero`完善的请求服务能力, 建立真正的系统调用流程

## 代码组织结构

```
ECNU-OSLAB-2025-TASK
├── LICENSE        开源协议
├── .vscode        配置了可视化调试环境
├── registers.xml  配置了可视化调试环境
├── .gdbinit.tmp-riscv xv6自带的调试配置
├── common.mk      Makefile中一些工具链的定义
├── Makefile       编译运行整个项目 (CHANGE, 新增目录syscall)
├── kernel.ld      定义了内核程序在链接时的布局
├── pictures       README使用的图片目录 (CHANGE, 日常更新)
├── README.md      实验指导书 (CHANGE, 日常更新)
└── src            源码
    ├── kernel     内核源码
    │   ├── arch   RISC-V相关
    │   │   ├── method.h
    │   │   ├── mod.h
    │   │   └── type.h
    │   ├── boot   机器启动
    │   │   ├── entry.S
    │   │   └── start.c
    │   ├── lock   锁机制
    │   │   ├── spinlock.c
    │   │   ├── method.h
    │   │   ├── mod.h
    │   │   └── type.h
    │   ├── lib    常用库
    │   │   ├── cpu.c
    │   │   ├── print.c
    │   │   ├── uart.c
    │   │   ├── utils.c
    │   │   ├── method.h
    │   │   ├── mod.h
    │   │   └── type.h
    │   ├── mem    内存模块
    │   │   ├── pmem.c
    │   │   ├── kvm.c
    │   │   ├── uvm.c (TODO, 用户态虚拟内存管理主体)
    │   │   ├── mmap.c (TODO, mmap节点资源仓库)
    │   │   ├── method.h (CHANGE, 日常更新)
    │   │   ├── mod.h
    │   │   └── type.h (CHANGE, 日常更新)
    │   ├── trap   陷阱模块
    │   │   ├── plic.c
    │   │   ├── timer.c
    │   │   ├── trap_kernel.c
    │   │   ├── trap_user.c (TODO, 系统调用处理 + pagefault处理)
    │   │   ├── trap.S
    │   │   ├── trampoline.S
    │   │   ├── method.h
    │   │   ├── mod.h (CHANGE, 日常更新)
    │   │   └── type.h
    │   ├── proc   进程模块
    │   │   ├── proc.c (TODO, proczero->mmap初始化)
    │   │   ├── swtch.S
    │   │   ├── method.h
    │   │   ├── mod.h
    │   │   └── type.h (CHANGE, 进程结构体里新增mmap字段)
    │   ├── syscall 系统调用模块
    │   │   ├── syscall.c (NEW, 系统调用通用逻辑)
    │   │   ├── sysfunc.c (TODO, 各个系统调用的处理逻辑) 
    │   │   ├── method.h (NEW)
    │   │   ├── mod.h (NEW)
    │   │   └── type.h (NEW)
    │   └── main.c
    └── user       用户程序
        ├── initcode.c (CHANGE, 按照测试需求来设置)
        ├── sys.h
        ├── syscall_arch.h
        └── syscall_num.h (CHANGE, 日常更新)
```

**标记说明**

**NEW**: 新增源文件, 直接拷贝即可, 无需修改

**CHANGE**: 旧的源文件发生了更新, 直接拷贝即可, 无需修改

**TODO**: 你需要实现新功能 / 你需要完善旧功能

## 任务1：用户态和内核态的数据迁移

回忆一下上个实验的`sys_helloworld`系统调用, 它的作用是让内核输出`"hello world"`

一个明显的问题: 用户态程序无法向内核程序传递参数, 导致系统服务非常僵硬和受限

我们可以从普通函数的参数传递获得启示, 传参方法无非两种:

- 直接传递值: `add(int a, int b)`, 本质是将参数值放到寄存器里

- 基于地址做间接传递: `strcmp(char *s1, char *s2, int len)`, 本质是将地址放到寄存器里

通过阅读`user/syscall_arch.h`, 可以发现系统调用编号默认放在a7寄存器, a0到a5寄存器则是存放参数

```c
static inline long __syscall6(long n, long a, long b, long c, long d, long e, long f)
{
    register long a7 __asm__("a7") = n;
    register long a0 __asm__("a0") = a;
    register long a1 __asm__("a1") = b;
    register long a2 __asm__("a2") = c;
    register long a3 __asm__("a3") = d;
    register long a4 __asm__("a4") = e;
    register long a5 __asm__("a5") = f;
    __asm_syscall("r"(a7), "0"(a0), "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5))
}
```

内核可以通过访问`proc->tf->ax`直接拿到这些参数 (trapframe实在太好用了~)

- 对于值传递, `arg_uint32`和`arg_uint64`可以很好地完成任务

- 对于地址传递, 必须考虑用户地址空间和内核地址空间不匹配的问题:

**用户传入的地址空间是基于用户页表的, 但是进入内核后使用的是内核页表**

解决这个问题需要手动查询用户页表, 找到虚拟地址对应的物理地址, 之后再做数据迁移

请你完成`kernel/mem/uvm.c`的第一部分, 包括`uvm_copyin`、`uvm_copyout`、`uvm_copyin_str`三个部分

`uvm_copyin_str` 返回 `void`，达到长度上限时不保证得到终止字符串。调用方若要以 `%s` 输出容量为 `C` 的缓冲区（`C>=1`），可最多复制 `C-1` 字节并置 `buf[C-1]=0`；不能在仅有 `maxlen` 字节的目标缓冲区外补写 `dst[maxlen]`。

随后, 你需要补全`trap_user_handler`中的系统调用的处理逻辑:

- 调用`syscall`进行分类跳转

- 补全三个具体的处理逻辑 `sys_copyin`、`sys_copyout`、`sys_copyinstr`

- 注意: 这三个系统调用只服务于本次测试, 不是长期保存的系统调用

## 测试1：用户态和内核态的数据迁移

测试逻辑: 

- 用户读取内核中的数组 (1 2 3 4 5)

- 用户将读到的数组传递给内核, 内核收到后打印出来

- 用户将自己的字符串传递给内核, 内核收到后打印出来

```c
// in initcode.c
#include "sys.h"

int main()
{
    int L[5];
    char* s = "hello, world"; 
    syscall(SYS_copyout, L);
    syscall(SYS_copyin, L, 5);
    syscall(SYS_copyinstr, s);
    while(1);
    return 0;
}
```

测试结果见`picture/test-1.png`

## 任务2：堆的手动管理与栈的自动管理

上次实验中, 栈空间被设置为4KB, 堆空间被设置为0KB, 对于非常简单的`initcode.c`是足够的

然而, 现实世界的应用程序需要可以动态增长的栈和堆, 本次实验我们做一个初步的实现

### 堆的管理是手动的

**堆-HEAP**为用户提供了一块连续的大范围内存空间, 它的生长方向的是低地址到高地址

内核给用户程序提供了一个`sys_brk`系统调用, 允许用户改变堆顶的位置

`sys_brk`的效果可以进一步分为:

- 空间增加: old_heap_top < new_heap_top 

- 空间减少: old_heap_top > new_heap_top

- 空间不变: old_heap_top == new_heap_top

- 查询当前堆顶: new_heap_top == 0

涉及内存页面的申请释放、用户页表的修改、`proc->heap_top`的更新

请你完成`sys_brk`、`uvm_heap_grow`、`uvm_heap_ungrow`几个函数

创建进程时，应根据实际装入的代码/数据镜像和保留映射记录不可收缩的初始堆顶；`brk` 不能低于此边界，也不能越过 `MMAP_BEGIN`。允许为 `proc_t` 增加必要的初始堆边界字段，在创建/回收时维护，并在 Lab6 fork 及后续 exec 中继承或重设。不要只检查 `USER_BASE`，也不要用已变化的 `heap_top` 重新推算初始边界。

### 栈的管理是自动的

**栈-STACK**为用户的临时变量和函数执行提供了一块连续的内存空间, 它的生长方向是高地址到低地址

用户程序无需显式地管理栈空间, 由内核根据用户需要进行自动管理 (自动的内存申请和映射)

内核不会在进程初始化时直接分配一个很大的栈空间 (默认分配4KB), 而是根据程序运行的需要逐步分配足够大的空间

当用户读或写一块未分配的地址空间时, 会触发**13号异常(Load Page Fault)** / **15号异常(Store/AMO Page Fault)**

我们在`trap_user_handler`里识别这两种异常, 然后调用`uvm_ustack_grow`来处理缺页异常

`uvm_ustack_grow`首先判断发生page fault的地址 (放在stval寄存器) 是否是合理的栈扩展地址

确认合法性后: 申请物理页面、修改用户页表、更新`proc->ustack_npage`

需要提醒的是: 一次可以扩展多个页面, 扩展后不会发生收缩 (和堆的管理不同)

### 边界检查

需要提醒的是: 我们在栈和堆的中间区域里, 划分了一段地址空间作为离散内存空间的区域 (mmap_region)

这块区域的起点地址被定义为`MMAP_BEGIN`, 终点被定义为`MMAP_END` (in `kernl/mem/type.h`)

因此, 栈的生长不应该越过`MMAP_END`, 堆的生长不应该越过`MMAP_BEGIN`

mmap_region的详细介绍放在任务3和和任务4, 这里只需要注意边界检查即可

## 测试2：堆的手动管理与栈的自动管理

**堆的管理**

```c
// in initcode.c
#include "sys.h"

#define PGSIZE 4096

int main()
{
    long long heap_top = 0;
    
    heap_top = syscall(SYS_brk, 0);
    heap_top = syscall(SYS_brk, heap_top + PGSIZE * 9);
    heap_top = syscall(SYS_brk, heap_top);
    heap_top = syscall(SYS_brk, heap_top - PGSIZE * 5);

    while(1);
    return 0;
}
```

你需要在`sys_brk`中增加一些调试性输出

测试结果见`picture/test-2(1)(2).png`

**栈的管理**

函数内定义非static的长数组就能让栈的大小超过4KB

你也可以通过深度函数递归来实现类似的效果 (比如汉诺塔问题)

```c
// in initcode.c
#include "sys.h"

#define PGSIZE 4096

int main()
{
    char tmp[PGSIZE * 4];

    tmp[PGSIZE * 3] = 'h';
    tmp[PGSIZE * 3 + 1] = 'e';
    tmp[PGSIZE * 3 + 2] = 'l';
    tmp[PGSIZE * 3 + 3] = 'l';
    tmp[PGSIZE * 3 + 4] = 'o';
    tmp[PGSIZE * 3 + 5] = '\0';

    syscall(SYS_copyinstr, tmp + PGSIZE * 3);

    tmp[0] = 'w';
    tmp[1] = 'o';
    tmp[2] = 'r';
    tmp[3] = 'l';
    tmp[4] = 'd';
    tmp[5] = '\0';

    syscall(SYS_copyinstr, tmp);

    while (1);
    return 0;
}
```

你需要在`trap_user_handler`中增加一些调试性输出

测试结果见`picture/test-3.png`

## 任务3: mmap_region_node 仓库管理

应用程序有了堆和栈就足够了吗? 应用程序有时需要临时申请一块内存空间, 过一会就释放掉

- 用栈来申请的话无法手动释放 (释放函数里数组占用的空间?)

- 用堆来申请的话可能面临碎片化风险 (堆更适合管理大片逻辑连续的内存空间)

因此, 我们需要设计一种可以动态申请释放的离散内存资源管理方法

直观的想法就是链表结构: 将多个内存资源节点通过链表链接在一起, 在进程结构体里存储表头!

说明: 在真实的操作系统里, 堆、栈、内存映射区的细节和定位与我们这里说的有所区别

结构体 `mmap_region_t` 用于描述一块连续地址空间, 它起始于`begin`, 包括`npages`个页面

进程会记录地址空间中的第一个`mmap_region_t`, 各个资源节点通过`next`指针串联 (构成单链表)

**特别提醒: mma_region_t 描述的是已分配出去的空间, 和2024版本是反过来的!**

```c
/* mmap_region 描述了一个 mmap区域 */
typedef struct mmap_region
{
    uint64 begin;             // 起始地址
    uint32 npages;            // 管理的页面数量
    struct mmap_region *next; // 链表指针
} mmap_region_t;
```

理解这部分后我们继续考虑另一个问题: `mmap_region_t`结构体本身也是一种资源

我们规定OS内核可以提供`N_MMAP`个这样的结构体, 各个进程需要有序获取该资源

为了保证各个进程可以高效和有序地共享这种资源, 我们在`kernel/mem/mmap.c`里维护了一个资源仓库

```c
/* mmap_region_node 是 mmap_region 在仓库里的包装 */
typedef struct mmap_region_node
{
    mmap_region_t mmap;
    struct mmap_region_node *next;
} mmap_region_node_t;


// mmap_region_node_t 仓库(单向链表) + 链表头节点(不可分配) + 保护仓库的自旋锁
static mmap_region_node_t node_list[N_MMAP];
static mmap_region_node_t list_head;
static spinlock_t list_lk;
```

具体来说:

- 首先将 `mmap_region_t` 包装为 `mmap_region_node_t`, 以维护资源仓库的单链表结构

- 然后通过全局的自旋锁 `list_lk` 确保任何时候只有一个进程在获取资源或释放资源

- 提供`mmap_init`、`mmap_region_alloc`、`mmap_region_free`作为资源仓库的对外接口

**正常启动也需要初始化：**当前常规 `main()` 未调用 `mmap_init()`；实现时由启动核在 `proc_make_first()` 前初始化一次，且早于任何节点申请。下面的专项测试已经调用它，但恢复常规启动流程时仍须保留初始化。

## 测试3: mmap_region_node 仓库管理

我们先来测试一下, 作为资源仓库, 它能不能在多核竞争的条件下保证资源申请和释放的有序性

```c
// in main.c
volatile static int started = 0;
volatile static bool over_1 = false, over_2 = false;
volatile static bool over_3 = false, over_4 = false;

void* mmap_list[N_MMAP];

int main()
{
    int cpuid = r_tp();

    if(cpuid == 0) {
        
        print_init();
        printf("cpu %d is booting!\n", cpuid);
        pmem_init();
        kvm_init();
        kvm_inithart();
        trap_kernel_init();
        trap_kernel_inithart();
        
        // 初始化 + 初始状态显示
        mmap_init();
        mmap_show_nodelist();
        printf("\n");

        __sync_synchronize();
        started = 1;

        // 申请
        for(int i = 0; i < N_MMAP / 2; i++)
            mmap_list[i] = mmap_region_alloc();
        over_1 = true;

        // 屏障
        while(over_1 == false ||  over_2 == false);

        // 释放
        for(int i = 0; i < N_MMAP / 2; i++)
            mmap_region_free(mmap_list[i]);
        over_3 = true;

        // 屏障
        while (over_3 == false || over_4 == false);

        // 查看结束时的状态
        mmap_show_nodelist();        

    } else {

        while(started == 0);
        __sync_synchronize();
        printf("cpu %d is booting!\n", cpuid);
        kvm_inithart();
        trap_kernel_inithart();

        // 申请
        for(int i = N_MMAP / 2; i < N_MMAP; i++)
            mmap_list[i] = mmap_region_alloc();
        over_2 = true;

        // 屏障
        while(over_1 == false || over_2 == false);

        // 释放
        for(int i = N_MMAP / 2; i < N_MMAP; i++)
            mmap_region_free(mmap_list[i]);
        over_4 = true;
    }

    while (1);
}
```

测试结果见`picture/test-4(1)(2).png`

- 第一部分的输出应该是 `node X index = X` (X从0增加到255)

- 第二部分 `node` 应从 0 增加到 255；`index` 应覆盖 0–255 全部节点各一次，无重复、遗漏或成环。双核申请和释放会交错，最终链表不必由两个连续半段组成，也不要求符合示例图片的具体顺序。

## 任务4: mmap 与 munmap

资源仓库的建立使得 `mmap_region_t` 结构体的申请和释放更加方便和安全, 服务于mmap和munamp操作

我们以mmap为例, 从系统调用出发, 梳理它的逻辑过程:

- 用户程序发出 `sys_mmap(uint64 begin, uint32 len)` 申请一块内存空间

- 调用`uvm_mmap(begin, len / PGSIZE, PTE_R | PTE_W)`进行具体处理

- `uvm_mmap()`首先创建一个新的 mmap_region_t 用于描述这块新的地址空间

- 随后将将这块 new_mmap_region 插入进程 mmap 链表的合适位置 (保持整体有序)

- 新插入的节点可能和前面的节点相邻, 可能和后面的节点相邻, 也可能同时相邻

- 考虑到仓库里资源受限的问题, 我们应该将相邻的节点进行尽可能的合并 (逻辑较为复杂, 建议你画图分析)

- 我们提供了辅助函数`mmap_merge()`用于帮助你完成这些合并, 你可以研究一下怎么用

- 合并完成后, 进行物理页申请和页表修改的步骤 (这里比较简单)

**注意: 当用户传入的begin=0时, 从头到尾扫描, 找到第一个足够大的空间即可**

`sys_mmap` 成功必须返回最终映射起始地址。骨架中 `uvm_mmap` 仍为 `void`，实现时应同步将 `mem/uvm.c` 定义和 `mem/method.h` 声明改成 `uint64` 并返回选定地址；否则自动选址无法向用户正确返回结果。

**另外: 记得为proc结构体增加mmap字段, 并在proc_make_first函数中增加对应的初始化逻辑**

munmap的整体流程于mmap相近, 你应该具备举一反三的能力, 这里不做详细介绍

## 测试4: mmap 与 munmap

我们给出了测试用例用于检测uvm_mmap()和uvm_munmap()中可能的遗漏和错误

请你理解它在测试哪些情况, 以及预期的输出是什么样的

当然, 你应该补充更多测试用例, 以确保实现的完备性

```c
// in initcode.c
#include "sys.h"

// 与内核保持一致
#define VA_MAX       (1ul << 38)
#define PGSIZE       4096
#define MMAP_END     (VA_MAX - (16 * 256 + 2) * PGSIZE)
#define MMAP_BEGIN   (MMAP_END - 64 * 256 * PGSIZE)

int main()
{
    // 建议画图理解这些地址和长度的含义

    // sys_mmap 测试 
    syscall(SYS_mmap, MMAP_BEGIN + 4 * PGSIZE, 3 * PGSIZE);
    syscall(SYS_mmap, MMAP_BEGIN + 10 * PGSIZE, 2 * PGSIZE);
    syscall(SYS_mmap, MMAP_BEGIN + 2 * PGSIZE,  2 * PGSIZE);
    syscall(SYS_mmap, MMAP_BEGIN + 12 * PGSIZE, 1 * PGSIZE);
    syscall(SYS_mmap, MMAP_BEGIN + 7 * PGSIZE, 3 * PGSIZE);
    syscall(SYS_mmap, MMAP_BEGIN, 2 * PGSIZE);
    syscall(SYS_mmap, 0, 10 * PGSIZE);

    // sys_munmap 测试
    syscall(SYS_munmap, MMAP_BEGIN + 10 * PGSIZE, 5 * PGSIZE);
    syscall(SYS_munmap, MMAP_BEGIN, 10 * PGSIZE);
    syscall(SYS_munmap, MMAP_BEGIN + 17 * PGSIZE, 2 * PGSIZE);
    syscall(SYS_munmap, MMAP_BEGIN + 15 * PGSIZE, 2 * PGSIZE);
    syscall(SYS_munmap, MMAP_BEGIN + 19 * PGSIZE, 2 * PGSIZE);
    syscall(SYS_munmap, MMAP_BEGIN + 22 * PGSIZE, 1 * PGSIZE);
    syscall(SYS_munmap, MMAP_BEGIN + 21 * PGSIZE, 1 * PGSIZE);

    while(1);
    return 0;
}
```

请你在`sys_mmap()`和`sys_munmap()`中增加提示性输出

```c
    proc_t *p = myproc();
    uvm_show_mmaplist(p->mmap);
    vm_print(p->pgtbl);
    printf("\n");
```

测试结果见`picture/test-5(1)(2)(3)(4)(5)(6)(7)(8).png`

## 任务5: 页表的复制与销毁

虽然目前我们只有一个进程且永不退出，但是需要为下一个实验做一些准备

你需要完成页表复制和销毁的函数 uvm_destroy_pgtbl() 和 uvm_copy_pgtbl()

需要提醒的是:

- 第一个函数考虑如何使用递归完成

- 第二个函数深入理解用户地址空间各个区域的特点

## 测试5: 页表的复制与销毁

请你参考前4个测试点的设计, 自行决定如何测试页表的复制和销毁

**尾声**

本次实验大概分成以下三个逻辑阶段:

- 首先关注如何实现用户态和内核态的数据传递 (以trapframe为媒介), 并建立规范的系统调用流程

- 随后讨论了用户态内存空间的管理: 堆、栈、mmap_region

- 最后讨论了用户页表整体的复制和销毁, 为下一个实验做准备

经过两次实验的打磨, proczero现在已经比较强大和完善了, 但是似乎有些孤单?

**我们将在下一个实验引入它的子子孙孙, 从单进程走向多进程！**

---

# 附录：Lab5 代码现状分析、零基础阅读路线与任务规划

> 本附录只做分析与规划，**没有修改任何代码**。  
> 分析基准：commit `25949e3 syscall`、`3c93ac8 mem`，以及工作区中尚未提交的 `src/kernel/mem/uvm.c`。  
> 说明：`uvm.c` 和 `mmap.c` 已经创建，但绝大多数函数仍是空骨架；本附录的“已实现/待实现”清单已按最新代码更新。  
> 建议先通读本附录的 A、B、C 三节，再按 F 节的顺序动手。

## A. 一句话结论

Lab5 的**框架、文件和部分辅助函数已经就位**，但**大部分函数体仍为空**：

- 已经就位：`mmap_region_t`、`mmap_region_node_t`、`N_MMAP`、`USER_BASE`、`MMAP_BEGIN/END`、`proc_t.mmap`、`syscall/` 模块、`syscall()` 跳转表、参数读取函数、`mem/method.h` 中的全部 uvm/mmap 声明，以及 `uvm.c`/`mmap.c` 两个源文件。
- 已经写了但仍是骨架：`uvm_copyin/out/copyin_str`、`uvm_mmap/munmap`、`uvm_heap_grow/ungrow`、`uvm_ustack_grow`、`mmap_init/alloc/free`、`uvm_copy_pgtbl`、`destroy_pgtbl` 等。
- 已经可用的辅助函数：`uvm_show_mmaplist`、`mmap_merge`、`copy_range`、`mmap_show_nodelist`，以及部分 `uvm_destroy_pgtbl`（细节见 B.2.1）。
- 还缺失：`sysfunc.c` 的 6 个系统调用实现；`trap_user.c` 的 `syscall()` 分发与 page fault 处理；用户侧 `syscall_num.h` 的新系统调用号；`proc_make_first()` 的 mmap/初始堆边界初始化；`main()` 的 `mmap_init()`；`uvm_mmap` 仍为 `void`，需改成 `uint64`。

所以现在的状态是“骨架已搭、辅助函数已有一部分、主体逻辑待填”，而不是“从零创建文件”。

## B. 当前仓库与 README 框架的对照

### B.1 已经就位的框架与文件

| 位置 | 内容 | 说明 |
|---|---|---|
| `src/kernel/mem/type.h` | `mmap_region_t`、`mmap_region_node_t`、`N_MMAP`、`USER_BASE`、`MMAP_BEGIN`、`MMAP_END` | Lab5 新增，已符合 README 描述 |
| `src/kernel/proc/type.h` | `proc_t.mmap` 字段 | 已加，但缺少“初始堆边界”字段 |
| `src/kernel/syscall/type.h` | `SYS_copyin`~`SYS_munmap`、`SYS_MAX_NUM`、`STR_MAXLEN` | 已加，编号 1~6 |
| `src/kernel/syscall/method.h` | `syscall()`、`arg_uint32/64`、`arg_str`、6 个 `sys_*` 声明 | 已加 |
| `src/kernel/syscall/syscall.c` | 跳转表、`syscall()`、参数读取 | 已提交（commit `25949e3`） |
| `src/kernel/syscall/sysfunc.c` | 6 个系统调用的空壳 | 已提交骨架，6 个函数仍只有 `return 0/-1` |
| `src/kernel/trap/mod.h` | 引入 `../syscall/mod.h` | 已加 |
| `src/kernel/mem/method.h` | 全部 uvm/mmap 函数声明 | 已加，但 `uvm_mmap` 仍是 `void` |
| `src/kernel/mem/uvm.c` | 用户态虚拟内存管理主体 | 文件已创建；`uvm_show_mmaplist`、`mmap_merge`、`copy_range`、部分 `uvm_destroy_pgtbl` 已实现，其余为空骨架 |
| `src/kernel/mem/mmap.c` | mmap 节点资源仓库 | 文件已创建；`mmap_show_nodelist` 已实现，`mmap_init/alloc/free` 为空骨架 |
| `src/kernel/main.c` | CPU0/CPU1 启动、`pmem_init/kvm_init/kvm_inithart`、`proc_make_first` | Lab4 已跑通，但未调用 `mmap_init()` |
| `src/kernel/mem/kvm.c` | 内核页表、trampoline、KSTACK(0) | Lab4 已实现 |
| `src/kernel/trap/trap_kernel.c` | 时钟/串口中断、S-mode 中断开关 | Lab4 已实现 |
| `src/user/syscall_arch.h`、`sys.h` | a7=系统调用号，a0~a5=参数，a0=返回值 | 已具备 Lab5 所需 ABI |

### B.2 仍为空骨架或仍是 Lab4 状态的部分

| 位置 | README 期望 | 当前状态 | 影响 |
|---|---|---|---|
| `src/kernel/mem/uvm.c` | 数据迁移 | `uvm_copyin/out/copyin_str` 为空函数 | 测试 1 无输出；`arg_str` 依赖 `uvm_copyin_str` |
| `src/kernel/mem/uvm.c` | mmap 主体 | `uvm_mmap_find/uvm_mmap/uvm_munmap` 为空；`uvm_mmap` 仍为 `void` | 测试 4 失败，自动选址无法返回地址 |
| `src/kernel/mem/uvm.c` | 堆/栈管理 | `uvm_heap_grow/ungrow`、`uvm_ustack_grow` 为空 | 测试 2 失败 |
| `src/kernel/mem/uvm.c` | 页表复制/销毁 | `destroy_pgtbl`、`uvm_copy_pgtbl` 为空；`uvm_destroy_pgtbl` 有部分逻辑但释放池有误 | 测试 5 失败，见 B.2.1 |
| `src/kernel/mem/mmap.c` | mmap 节点仓库 | `mmap_init/alloc/free` 为空，`mmap_show_nodelist` 已实现 | 测试 3 无法申请节点；`mmap_merge` 无法归还节点 |
| `sys_copyin/out/instr` | 数据迁移 | `return 0` 桩 | 测试 1 无输出 |
| `sys_brk` | 堆伸缩/查询 | `return -1` 桩 | 测试 2 堆部分失败 |
| `sys_mmap/munmap` | 映射管理 | `return 0` 桩 | 测试 4 失败 |
| `trap_user.c` | `syscall()` 分发 + 13/15 号 page fault | 仍只处理 `SYS_helloworld`，其他异常 panic | 测试 1/2/4 都无法进入正确分支 |
| `proc_make_first()` | 初始化 `mmap`、初始堆边界、按镜像大小映射 | Lab4 版本：代码映射在 0、只支持 1 页、`heap_top=PGSIZE`、`mmap` 未显式初始化 | `brk` 无法正确判断下界，mmap 链表是未定义值 |
| `src/user/syscall_num.h` | 与内核一致的 1~6 | 仍只有 `SYS_helloworld 0` | 测试代码无法通过 `SYS_copyin` 等宏编译 |
| `src/user/initcode.c` | 按各测试替换 | 仍是 Lab4 的两次 `SYS_helloworld` | 不会触发 Lab5 测试 |
| `main.c` | 启动时调用 `mmap_init()` | 未调用 | 一旦 `mmap_region_alloc` 被使用就会访问未初始化的仓库 |
| `Makefile` | 正确生成 `initcode.h`、创建 syscall 目录 | 见 B.4 | 当前增量构建会失败 |

### B.2.1 已经实现的辅助函数与注意事项

这些函数已经有函数体，可以直接复用；但它们依赖的空函数还没实现，当前不能单独工作。

| 函数 | 现状 | 复用时要注意 |
|---|---|---|
| `uvm_show_mmaplist(mmap)` | 已实现 | 遍历并打印 `begin ~ begin+npages*PGSIZE`；本项目 `printf` 的 `%p` 只输出低 32 位，地址会截断，调试时可看低 32 位或用 `%x` |
| `mmap_merge(mmap_1, mmap_2, keep_mmap_1)` | 已实现 | 断言两个区域严格相邻；`keep_mmap_1=true` 时扩展前者并释放后者，否则扩展后者并释放前者；它调用 `mmap_region_free`，而该函数还是空骨架，所以必须先实现 `mmap.c` 的仓库 |
| `copy_range(old, new, begin, end)` | 已实现 | 逐页 `pmem_alloc(false)` + `memmove` + 用旧 PTE 的 flags 映射到新页表；要求旧 PTE 有效、`new` 对应 VA 尚未映射；目前 `uvm_copy_pgtbl` 为空，所以它还是“未被调用的静态函数” |
| `uvm_destroy_pgtbl(pgtbl)` | 部分实现 | 先 `vm_unmappages(TRAPFRAME, ..., true)` 再 `vm_unmappages(TRAMPOLINE, ..., false)`；TRAMPOLINE 不释放是对的，但 **TRAPFRAME 的释放池用错了**（见下）；`destroy_pgtbl` 仍是空函数，页表页/叶子页都还没释放 |
| `mmap_show_nodelist()` | 已实现 | 用 `tmp - &node_list[0]` 计算数组下标；必须在 `mmap_init()` 之后调用，否则仓库链表为空 |

**`uvm_destroy_pgtbl` 当前的释放池问题（重要）**

`proc_make_first()` 中 trapframe 是用 `pmem_alloc(true)`（内核物理页区）申请的，但 `vm_unmappages(..., freeit=true)` 内部固定执行：

```c
pmem_free(PTE_TO_PA(*pte), false);   // false = 用户物理页区
```

trapframe 的物理地址在内核区，`pmem_free(..., false)` 会因地址不在 `user_region` 而 panic。实现任务 5 时不能直接依赖现在的 `vm_unmappages(..., true)` 来释放 trapframe；要么单独 `pmem_free(trapframe_pa, true)` 后清 PTE，要么给 `vm_unmappages` 增加“释放到哪个池”的参数。**这是实现页表销毁前必须先解决的点。**

### B.3 当前构建状态（实测）

1. **当前最先出现的错误**是 `mmap.c` 的空返回值函数：

   ```text
   src/kernel/mem/mmap.c:19:1: error:
   control reaches end of non-void function [-Werror=return-type]
   ```

   原因是 `mmap_region_alloc()` 返回 `mmap_region_t *`，但函数体还是空的。

2. 把 `mmap_region_alloc/free` 填上之后，`uvm.c` 还会继续报同类错误：
   - `uvm_mmap_find`、`uvm_heap_grow`、`uvm_heap_ungrow`、`uvm_ustack_grow` 返回 `uint64`，但函数体为空；
   - `mmap_merge`、`copy_range` 是 `static` 函数，目前没有调用者，`-Wall` 会报 `-Wunused-function`。
   这两种错误都因为项目用 `-Werror` 编译，必须先有返回值/调用者才能通过。

3. 旧版本的 `undefined reference to uvm_copyin_str` **已经消失**，因为 `uvm.c` 已经提供了（空的）函数体。现在的问题从“链接缺少符号”变成了“编译期空函数/未使用静态函数”。

4. 如果 `target/` 是从旧实验留下来的，且缺少 `target/kernel/syscall`、`target/kernel/mem/uvm.o` 等目录/文件，先执行 `make clean`。Makefile 的目录创建规则只在 **整个 `target/` 不存在** 时才运行；增量目录不全会导致 `opening dependency file ... No such file or directory`。

5. `src/kernel/mem/uvm.c` 目前在 `git status` 中是未提交修改；继续实现前建议先确认/提交，避免丢失。

**结论**：现在 `make build` 的第一道坎是 `mmap_region_alloc` 的返回值；按“先 mmap 仓库、再 uvm 主体”的顺序实现，才能让编译错误逐步减少。

### B.4 需要特别注意的框架冲突与坑

1. **`src/user/syscall_num.h` 没更新**
   内核已经是 `SYS_copyin=1 ... SYS_munmap=6`，用户侧还是 `SYS_helloworld=0`。测试代码里的 `SYS_copyin` 等宏会直接编译失败。用户侧必须补齐同名同值的 1~6。

2. **`SYS_helloworld` 与跳转表索引 0**
   `syscalls[]` 使用指定初始化器，`SYS_copyin=1` 起步，所以下标 0 是 `NULL`。如果用户程序还调用 `SYS_helloworld`（值为 0），`syscall()` 会走 `unknown syscall` 并 panic。Lab5 开始后，用户程序应改用新的系统调用号；若要保留 helloworld，需要单独给它分配一个编号或保留特殊分支。

3. **`uvm_mmap` 的返回类型要改**
   README 任务 4 明确要求把 `uvm_mmap` 从 `void` 改成 `uint64`，并同步修改 `mem/method.h` 的声明，否则 `sys_mmap` 无法把“自动选中的起始地址”返回给用户。当前声明仍是 `void`。

4. **`proc_t` 缺少“初始堆边界”字段**
   README 任务 2 明确说：不能只检查 `USER_BASE`，也不能用已经变化的 `heap_top` 反推初始边界。需要给 `proc_t` 增加一个不可变的初始堆顶字段（例如 `heap_base`），在 `proc_make_first()` 中按实际镜像大小设置，`brk` 不得低于它。

5. **当前代码页映射在 0，而新框架引入了 `USER_BASE = PGSIZE`**
   Lab4 的 `proc_make_first()` 把 initcode 映射在虚拟地址 0，`epc=0`。Lab5 新增 `USER_BASE (PGSIZE)`，且 Lab4 原始注释本来就写着“最低 4096 字节不分配物理页”。这有两种处理方式：
   - 推荐：把用户镜像改映射到 `USER_BASE`，`epc=USER_BASE`，VA 0 保持不映射（可捕获空指针）；
   - 或者保留当前“映射在 0”的方案，但必须在报告中说明，并保证初始堆边界从“镜像结束页”计算，而不是硬编码。
   两种方案都能跑，但**不能一半按 0、一半按 `USER_BASE`**。

6. **`proc_make_first()` 只支持 1 页镜像**
   当前有 `assert(initcode_len <= PGSIZE)`。Lab5 的测试镜像可能仍然很小，但正确做法是按 `ALIGN_UP(initcode_len, PGSIZE)` 映射多页，并据此设置初始堆边界。

7. **`main()` 没有 `mmap_init()`**
   README 任务 3 的“正常启动也需要初始化”已经提醒：恢复常规 `main()` 时，必须在 `proc_make_first()` 之前调用一次 `mmap_init()`。测试 3 的专用 `main()` 里有调用，但不能只依赖测试代码。

8. **Makefile 的 `ELFUser` 路径不一致**
   `ELFUser = $(TARGET)/user/initcode.h`，但生成规则实际写到 `$(UserPath)/initcode.h`（即 `src/user/initcode.h`），而 `proc.c` 包含的也是 `src/user/initcode.h`。结果是指定的目标文件永远不存在，每次 make 都会重复生成，增量构建语义不正确。当前不影响编译，但建议修正为同一路径。

9. **`UserOBJ` 会把所有 `src/user/*.c` 都编译，但 initcode 链接只用 `initcode.o`**
   如果以后在 `src/user/` 下放多个测试文件（各自有 `main`），它们会参与 `ELFUser` 的依赖，却不会参与链接；其中一个文件报错就会阻断构建。Lab5 阶段建议测试代码都放在 `initcode.c`，或者同步调整 Makefile。

10. **`syscall.c` 的边界检查**
    `if (sys_num < 0 || sys_num > SYS_MAX_NUM || syscalls[sys_num] == NULL)` 在当前 `SYS_MAX_NUM=6`、数组最大下标为 6 的情况下是正确的，但它依赖“`SYS_MAX_NUM` 等于最大下标而不是数量”这一隐含约定。以后增加系统调用时，要么把检查改成 `>= 数组元素个数`，要么明确维护这个约定。

11. **工作区提交状态**
    `syscall/mod.h`、`syscall.c`、`sysfunc.c` 已经在 commit `25949e3` 中提交；目前 `git status` 里未提交的是 `src/kernel/mem/uvm.c`（以及 README/.gitignore 的文档/忽略项修改）。继续实现前建议先确认并提交/备份 `uvm.c`，避免切分支或 reset 时丢失。

12. **空函数在 `-Werror` 下会先卡住编译**
    `mmap_region_alloc`、`uvm_mmap_find`、`uvm_heap_grow/ungrow`、`uvm_ustack_grow` 都是非 `void` 返回类型但函数体为空，会报 `-Wreturn-type`；`mmap_merge`、`copy_range` 暂时没有调用者，会报 `-Wunused-function`。不要用无意义的 `return 0` 或 `(void)func` 去“压警告”，应按依赖顺序实现调用者，让这些辅助函数真正被用起来。

13. **`mmap_merge` 依赖 `mmap_region_free`**
    `uvm.c` 里的 `mmap_merge` 已经写好，但它会调用 `mmap.c` 的 `mmap_region_free`。因此实现顺序应该是：先完成 `mmap_init/alloc/free`，再完成 `uvm_mmap` 对 `mmap_merge` 的调用。否则即使编译通过，运行时也会进入空函数/错误链表。

14. **`uvm_destroy_pgtbl` 当前的 trapframe 释放池错误**
    见 B.2.1：trapframe 是 `pmem_alloc(true)` 申请的，不能直接用 `vm_unmappages(..., true)`（它按用户池释放）。这是任务 5 必须先修正的点，否则 `uvm_destroy_pgtbl` 一调用就会 `pmem_free: invalid page`。

15. **调试输出里的 `%p` 会截断**
    `uvm_show_mmaplist` 和 README 测试里的 `vm_print` 都使用本项目的 `printf`；它的 `%p` 实现按 32 位有符号数输出，高地址会只显示低 32 位。分析地址时不要因为“看起来不对”就误判页表错误，可以看低 32 位，或临时用 `%x`/自定义 64 位输出。

16. **`uvm_mmap_find` 的输出参数要先初始化**
    它的签名带有 `mmap_region_t **p_last_mmap, **p_tmp_mmap`，用于把“插入位置的前驱/后继”带回给 `uvm_mmap`。实现时要在所有返回路径上正确设置这两个指针（没有前驱/后继时置 `NULL`），否则 `uvm_mmap` 的插入/合并会使用未初始化指针。

## C. 零基础先建立这 10 个概念

不需要一上来就读懂汇编。先把下面这些概念建立起来，Lab5 的代码就会“有地方挂”。

1. **C 语言基本功**
   - 指针、数组、结构体、链表、函数指针、`typedef`、头文件与 `#pragma once`。
   - 本项目大量使用 `uint64`、`uint32`、`pte_t*`、`mmap_region_t*`。
   - 重点：`mmap_region_node_t` 的 `mmap` 是第一个成员，所以 `(mmap_region_node_t *)mmap_ptr` 可以完成“由成员反推容器”。这是 `mmap_region_free()` 的关键。

2. **RISC-V 特权级**
   - M-mode（机器态，启动/时钟硬件）、S-mode（内核）、U-mode（用户程序）。
   - 用户程序只能通过 `ecall` 陷入内核；内核通过 `sret` 返回用户。

3. **CSR 寄存器**
   - `satp`：当前页表根地址 + 分页模式。
   - `stvec`：S-mode trap 入口。
   - `sepc`：trap 的返回地址。
   - `scause`：trap 原因（最高位 1=中断，0=异常；低 4 位是编号）。
   - `stval`：page fault 时是出错地址。
   - `sstatus`：SPP/SPIE 等，控制 `sret` 回到哪个特权级、是否开中断。
   - `sscratch`：用户 trap 时用来交换出 trapframe 地址。
   这些都在 `src/kernel/arch/method.h` 里有读写封装。

4. **SV39 页表**
   - 三级页表：VPN[2] → VPN[1] → VPN[0] → offset。
   - PTE 的 `V/R/W/X/U` 位决定“有效/读/写/执行/用户可访问”。
   - `PTE_U=1` 的页在 U-mode 可访问；S-mode 访问 `PTE_U=1` 的页需要 `SUM`，本项目没开，所以内核缓冲区/页表页通常不带 `PTE_U`。
   - `vm_getpte()` 负责查页表，`vm_mappages()` 负责建立映射。

5. **物理页 vs 虚拟页**
   - `pmem_alloc(true/false)` 返回的是**物理地址**（当前内核直接映射，所以这个地址也能被内核直接当指针用）。
   - 用户程序看到的是虚拟地址；`uvm_copyin/out` 必须把用户 VA 翻译成 PA，才能在内核里读写。

6. **trapframe 与 context 的区别**
   - `trapframe`：同一个进程在 U-mode 和 S-mode 之间往返时保存的**用户现场**（全部通用寄存器、内核栈顶、内核页表、sepc 等）。
   - `context`：`swtch()` 在内核里切换执行流时保存的**内核现场**（ra、sp、s0~s11）。
   - 两者不要混：`proc_make_first()` 设置的是 context + 初始 trapframe；`trap_user_return()` 每次返回用户前刷新 trapframe 中的内核信息。

7. **一次系统调用的完整路径**
   ```text
   U-mode: syscall(SYS_xxx, args...)
     -> a7=系统调用号, a0~a5=参数
     -> ecall
     -> 硬件跳到 stvec = TRAMPOLINE
     -> user_vector 保存寄存器到 TRAPFRAME
     -> 切换到内核页表
     -> trap_user_handler()
          -> scause==8: syscall()
               -> 跳转表 -> sys_xxx()
               -> 返回值写回 tf->a0
          -> sepc += 4
     -> trap_user_return()
     -> user_return 恢复寄存器, sret
     -> U-mode 继续执行
   ```
   这条路径是 Lab5 的主线，测试 1 就是它。

8. **page fault 是什么**
   - 用户访问了“页表里没有映射”的地址，硬件产生 13 号（Load）或 15 号（Store）异常。
   - `stval` 保存出错地址，`sepc` 保存出错指令地址。
   - 内核处理后**不能**把 `sepc += 4`，要重新执行那条指令。
   - Lab5 用它实现用户栈自动增长。

9. **堆、栈、mmap 区的方向**
   - 堆：低地址 → 高地址增长，`brk` 手动调整。
   - 栈：高地址 → 低地址增长，page fault 自动扩展。
   - mmap：中间一段固定区间，按需分配/释放离散区域。
   - 边界：堆不能越过 `MMAP_BEGIN`，栈不能越过 `MMAP_END`。

10. **构建与调试工具**
    - `make clean && make run`：清空构建产物并启动 QEMU。
    - `make debug`：带 GDB stub 启动。
    - `riscv64-linux-gnu-objdump -d`：反汇编，确认用户程序入口、trampoline 偏移。
    - `riscv64-linux-gnu-nm`：看符号地址（`trampoline`、`user_return`、`user_vector`）。
    - GDB：`b trap_user_handler`、`b syscall`、`b uvm_copyin`、`info registers`、`x/8gx`。

## D. 从零阅读代码的顺序（建议 12 步）

不要从 `trapoline.S` 第一行开始硬啃。按“启动 → 内存 → trap → 进程 → 系统调用”的顺序读。

1. **`src/kernel/boot/entry.S`**
   看 `_entry` 如何设置每个 CPU 的栈，然后跳到 `start`。
   要能回答：启动时 `sp` 指向哪里？

2. **`src/kernel/boot/start.c`**
   看 M-mode 如何设置 `medeleg/mideleg`、时钟、`mepc=main`、`mstatus`，最后 `mret` 到 S-mode。
   要能回答：为什么内核一开始运行在 S-mode？

3. **`src/kernel/main.c`**
   看 CPU0/CPU1 的分工、`boot_ready` 屏障、初始化顺序。
   要能回答：`pmem_init/kvm_init/kvm_inithart/trap_kernel_init/trap_kernel_inithart/proc_make_first` 各自做什么、为什么 CPU1 要等？

4. **`src/kernel/arch/type.h`、`arch/method.h`**
   看基本类型、`NCPU`、CSR 读写封装。把 `satp/stvec/sepc/scause/stval/sstatus/sscratch` 的读写函数找出来。

5. **`src/kernel/lib/cpu.c`、`lib/print.c`、`lib/uart.c`、`lib/utils.c`**
   看 `mycpu()` 如何用 `tp` 找当前 CPU、`myproc()` 如何拿当前进程；`printf`、`panic`、`assert` 的用法；`memset/memmove` 的语义。

6. **`src/kernel/lock/spinlock.c`**
   看 `push_off/pop_off` 如何嵌套关中断，`spinlock_acquire/release` 的约束。
   要能回答：为什么 `uvm_show_mmaplist` 里不能随便拿锁？为什么 `mmap.c` 需要 `list_lk`？

7. **`src/kernel/mem/pmem.c`**
   看物理页空闲链表、`pmem_alloc` 为什么返回清零页、`pmem_free` 的参数 `in_kernel`。
   要能回答：用户页、页表页、trapframe 分别应该用 `true` 还是 `false`？

8. **`src/kernel/mem/kvm.c` + `mem/type.h`**
   看 `vm_getpte` 的三级查找、`vm_mappages` 的检查、`vm_unmappages` 的释放；看 `TRAMPOLINE/TRAPFRAME/KSTACK/USER_BASE/MMAP_BEGIN/MMAP_END`。
   要能回答：一个用户 VA 如何变成 PA？`PTE_U` 什么时候必须加？

9. **`src/kernel/trap/trap.S`、`trapoline.S`、`trap_user.c`**
   先把 `user_vector` 当作“保存寄存器 + 切内核页表 + 跳到 C 函数”的黑盒；重点看 `trap_user_handler` 和 `trap_user_return` 的 C 逻辑。
   要能回答：`scause==8` 时做了什么？page fault 时该做什么？为什么 `user_return` 要通过 `TRAMPOLINE + offset` 调用？

10. **`src/kernel/proc/type.h`、`proc.c`、`swtch.S`**
    看 `trapframe_t` 的字段偏移、`proc_t` 的字段、`proc_make_first` 如何把进程“跑起来”、`swtch` 保存哪些寄存器。
    要能回答：`tf->sp` 和 `tf->user_to_kern_sp` 分别是什么？`p->ctx.sp` 是什么？

11. **`src/kernel/syscall/type.h`、`syscall.c`、`sysfunc.c`**
    看系统调用号、跳转表、`arg_raw/arg_uint32/arg_uint64/arg_str`。
    要能回答：用户参数在 trapframe 的哪些偏移？返回值写到哪里？

12. **`src/user/syscall_arch.h`、`sys.h`、`initcode.c`**
    看用户侧如何把 `a7/a0~a5` 填好并 `ecall`；看 `initcode` 如何被链接到 0（或 `USER_BASE`）并嵌入内核。

**读代码时每看一个函数，问三个问题：**
1. 它的输入是什么（寄存器/参数/全局变量）？
2. 它的输出/副作用是什么（返回值/写内存/改页表/改 CSR）？
3. 谁调用它，调用前必须满足什么前提？

## E. 建议的调试方法

1. **先保证能编译**
   ```bash
   make clean
   make build
   ```
   每完成一个小函数就编译一次，不要一次写完五个任务。

2. **用 `printf` 做“最小证据”**
   - 在 `syscall()` 入口打印 `sys_num`。
   - 在 `sys_brk` 打印 `old/new/heap_base/MMAP_BEGIN`。
   - 在 `uvm_ustack_grow` 打印 `fault_addr/old_npage/new_npage`。
   - 在 `uvm_mmap` 打印 `begin/end/合并情况/返回值`。
   - 注意：`printf` 会拿 `print_lk`，不要在持锁时打印，避免死锁。

3. **用 `vm_print(p->pgtbl)` 看页表**
   - 确认某个 VA 是否有 PTE、PA 是什么、flags 是否含 `PTE_U`。
   - 测试 4 的 README 示例就是配合 `uvm_show_mmaplist` 使用。

4. **用 `uvm_show_mmaplist` 看链表**
   - 每次 mmap/munmap 后打印，检查 `begin/npages/next` 是否有序、是否成环。

5. **用 GDB 定位 page fault / 非法访问**
   ```bash
   make debug
   # 另一个终端
   riscv64-linux-gnu-gdb target/kernel/kernel-qemu.elf
   (gdb) b trap_user_handler
   (gdb) b syscall
   (gdb) c
   ```
   在 `trap_user_handler` 里看 `r_scause()`、`r_sepc()`、`r_stval()`。

6. **QEMU 退出**
   `-nographic` 下按 `Ctrl-A` 再按 `X`。

7. **常见 panic 的含义**
   - `vm_mappages: remap`：同一个 VA 被映射两次，检查 mmap 合并/brk 是否重复映射。
   - `vm_unmappages: already unmapped`：释放了未映射的页，检查 unmap 范围。
   - `pmem_alloc: out of memory`：物理页耗尽，检查是否泄漏/重复申请。
   - `trap_user_handler` 的 `unexpected exception`：看 `scause/stval`，通常是用户指针非法或页表权限错误。

## F. Lab5 任务分解与实施顺序

下面按“依赖关系”排序。**不要跳阶段**：任务 1 是后面所有测试的调试基础。

### 当前实现进度快照（基于当前代码）

| 文件/函数 | 状态 |
|---|---|
| `mmap.c`：`mmap_show_nodelist` | 已实现 |
| `mmap.c`：`mmap_init`、`mmap_region_alloc`、`mmap_region_free` | 空函数 |
| `uvm.c`：`uvm_show_mmaplist` | 已实现 |
| `uvm.c`：`mmap_merge` | 已实现（依赖 `mmap_region_free`） |
| `uvm.c`：`copy_range` | 已实现（暂时没有调用者） |
| `uvm.c`：`uvm_destroy_pgtbl` | 部分实现（trapframe 释放池有误，见 B.2.1） |
| `uvm.c`：`uvm_copyin/out/copyin_str` | 空函数 |
| `uvm.c`：`uvm_mmap_find/uvm_mmap/uvm_munmap` | 空函数（`uvm_mmap` 还是 `void`） |
| `uvm.c`：`uvm_heap_grow/ungrow`、`uvm_ustack_grow` | 空函数 |
| `uvm.c`：`destroy_pgtbl`、`uvm_copy_pgtbl` | 空函数 |
| `syscall.c`：`syscall`、`arg_*` | 已提交可用 |
| `sysfunc.c`：6 个 `sys_*` | 全是桩 |
| `trap_user.c`：syscall / page fault | 仍是 Lab4 逻辑 |
| `user/syscall_num.h` | 仍是旧编号 |
| `proc.c` / `main.c` | 未接 `mmap`、`heap_base`、`mmap_init` |

### 阶段 0：准备（不写业务逻辑，先把“能编译、能定位”做好）

1. 更新 `src/user/syscall_num.h`，与 `src/kernel/syscall/type.h` 保持一致：
   `SYS_copyin=1`、`SYS_copyout=2`、`SYS_copyinstr=3`、`SYS_brk=4`、`SYS_mmap=5`、`SYS_munmap=6`。
   如果还要保留 helloworld，给它单独编号或保留 trap_user 里的特殊分支，不要让 0 和跳转表冲突。

2. 决定用户镜像基址：
   - 方案 A（推荐，符合 `USER_BASE` 和“最低页不映射”）：镜像映射到 `USER_BASE`，`epc=USER_BASE`。
   - 方案 B（沿用 Lab4）：镜像映射到 0，`epc=0`，并在报告中说明。
   无论哪种，初始堆边界都从“镜像结束页”计算。

3. 给 `proc_t` 增加初始堆边界字段（例如 `uint64 heap_base;`）。注意 Lab6 fork/exec 要继承或重设它，不能在 `brk` 里用 `heap_top` 反推。

4. `uvm.c` 和 `mmap.c` **已经创建，不要重复创建**；按依赖顺序补齐空函数：
   - 先 `mmap.c`：`mmap_init` → `mmap_region_alloc` → `mmap_region_free`（`mmap_show_nodelist` 已有）；
   - 再 `uvm.c`：`copyin/out/copyin_str` → `heap/stack` → `mmap/munmap` → `copy/destroy`。
   两个文件都已经 `#include "mod.h"`，Makefile 用 wildcard 收集 `.c`，不需要手动加文件名。

5. 修正构建环境：
   - 先 `make clean`，让 `target/kernel/syscall` 等目录被创建；
   - 若修改 Makefile，建议把 `ELFUser` 与实际生成路径统一，并让 `proc.o` 显式依赖 `src/user/initcode.h`。

**验收**：`make clean && make build` 不再出现 `-Wreturn-type` / `-Wunused-function`，能链接出 `kernel-qemu.elf`。此时业务功能可能仍未实现，但骨架应完整、可继续调试。

---

### 阶段 1：任务 1——用户态/内核态数据迁移

**目标**：用户能把数组/字符串传给内核，内核能读出来打印。

**要实现的函数**

| 文件 | 函数 | 要点 |
|---|---|---|
| `mem/uvm.c` | `uvm_copyin(pgtbl, dst, src, len)` | `dst` 是内核缓冲区，`src` 是用户 VA；逐页查 PTE，检查 `PTE_V` 和 `PTE_U`，按页内偏移拷贝 |
| `mem/uvm.c` | `uvm_copyout(pgtbl, dst, src, len)` | `dst` 是用户 VA，`src` 是内核缓冲区；方向与 copyin 相反 |
| `mem/uvm.c` | `uvm_copyin_str(pgtbl, dst, src, maxlen)` | 最多读 `maxlen` 字节；遇到 `\0` 停止；**不要写 `dst[maxlen]`**；返回 `void`，不保证终止 |
| `syscall/sysfunc.c` | `sys_copyin` | `arg_uint64(0,&addr)`、`arg_uint32(1,&len)`；把用户 `int` 数组拷到内核 `int kbuf[...]`，再打印 |
| `syscall/sysfunc.c` | `sys_copyout` | 内核数组 `{1,2,3,4,5}` 拷到用户 `addr`，返回元素数 |
| `syscall/sysfunc.c` | `sys_copyinstr` | 用 `STR_MAXLEN+1` 的内核缓冲区，`uvm_copyin_str(..., STR_MAXLEN)`，手动 `buf[STR_MAXLEN]=0`，再 `printf("%s")` |
| `trap/trap_user.c` | syscall 分支 | `scause==8` 时调用 `syscall()`，然后 `tf->user_to_kern_epc += 4`；不要再用 lab4 的 `SYS_helloworld` 分支 |

**关键算法（copyin/copyout）**

```text
while len > 0:
    page = va & ~(PGSIZE-1)
    offset = va - page
    n = MIN(len, PGSIZE - offset)
    pte = vm_getpte(pgtbl, page, false)
    如果 pte==0 或 !(*pte & PTE_V) 或 !(*pte & PTE_U): panic
    pa = PTE_TO_PA(*pte) + offset
    memmove(dst, (void*)pa, n)   // copyin
    va += n; dst += n; len -= n
```

**测试 1 验收**

把 `initcode.c` 换成 README 的测试 1，运行后应看到：
- 用户读内核数组并回传后，内核打印 `1 2 3 4 5`；
- 字符串打印 `hello, world`。

**常见错误**
- 只处理单页，跨页时越界/错位；
- 忘记检查 `PTE_U`，把内核页也当用户页访问；
- `uvm_copyin_str` 写了 `dst[maxlen]`；
- `syscall()` 后忘记 `epc += 4`，用户程序重复执行 `ecall`。

---

### 阶段 2：任务 2——堆与栈

#### 2.1 堆：`sys_brk` + `uvm_heap_grow/ungrow`

**`sys_brk` 逻辑**
```text
arg_uint64(0, &new_top)
if new_top == 0: return p->heap_top          // 查询
if new_top < p->heap_base: return -1         // 初始边界（新字段）
if new_top > MMAP_BEGIN: return -1           // 不能压到 mmap 区
if new_top > p->heap_top:
    p->heap_top = uvm_heap_grow(p->pgtbl, p->heap_top, new_top - p->heap_top)
else if new_top < p->heap_top:
    p->heap_top = uvm_heap_ungrow(p->pgtbl, p->heap_top, p->heap_top - new_top)
return p->heap_top
```

**`uvm_heap_grow` 要点**
- 只映射 `[ALIGN_UP(cur_top,PGSIZE), ALIGN_UP(new_top,PGSIZE))` 中尚未映射的整页；
- 用 `pmem_alloc(false)` 申请用户页，`vm_mappages(..., PTE_R|PTE_W|PTE_U)`；
- 返回 `new_top`（不是页对齐后的值，`brk` 的字节语义要保留）；
- 失败要回滚已申请的页。

**`uvm_heap_ungrow` 要点**
- 只释放 `[ALIGN_UP(new_top,PGSIZE), ALIGN_UP(cur_top,PGSIZE))` 的整页；
- 用 `vm_unmappages(pgtbl, page, PGSIZE, true)`（`freeit=true`）；
- 不能释放仍包含 `new_top` 的那一页；
- 返回 `new_top`。

**初始堆边界**
- 在 `proc_make_first()` 中：
  `p->heap_base = 镜像结束地址; p->heap_top = p->heap_base;`
- 如果镜像从 `USER_BASE` 开始且占 `n` 页：
  `heap_base = USER_BASE + n * PGSIZE`。
- `brk` 只和 `heap_base` 比较，不要用 `USER_BASE` 或当前 `heap_top` 反推。

#### 2.2 栈：page fault + `uvm_ustack_grow`

**`trap_user_handler` 增加分支**
```text
if scause == 13 or scause == 15:
    p->ustack_npage = uvm_ustack_grow(p->pgtbl, p->ustack_npage, r_stval())
    // 注意：不要 epc += 4，返回后要重新执行出错指令
else if scause == 8:
    syscall(); epc += 4
```

**`uvm_ustack_grow` 逻辑**
- `fault_page = stval & ~(PGSIZE-1)`；
- 当前栈底 = `TRAPFRAME - old_ustack_npage * PGSIZE`；
- 合法条件：`fault_page >= MMAP_END` 且 `fault_page < 当前栈底`；
- 从 `当前栈底 - PGSIZE` 向下逐页申请/映射，直到覆盖 `fault_page`；
- 权限 `PTE_R|PTE_W|PTE_U`；
- 返回新的 `ustack_npage`；
- 栈只增不减（本实验不做收缩）。

**测试 2 验收**
- 堆测试：`brk(0)` 返回当前堆顶；先加 9 页、再减 5 页，调试输出应显示 `heap_top` 按预期变化，且不越过 `MMAP_BEGIN`。
- 栈测试：`char tmp[PGSIZE*4]` 或深递归触发 13/15 号异常，系统应自动扩栈并打印 `hello`、`world`，不能 panic。

**常见错误**
- page fault 后仍然 `epc += 4`；
- 扩栈方向搞反，扩到 `MMAP_END` 以下；
- `brk` 用 `USER_BASE` 做下界，而不是 `heap_base`；
- 释放堆页时把半页也释放了。

---

### 阶段 3：任务 3——mmap 节点仓库

**目标**：实现一个全局的、带自旋锁的 `mmap_region_node_t` 资源池。

**`mem/mmap.c` 的结构**
```c
static mmap_region_node_t node_list[N_MMAP];
static mmap_region_node_t list_head;
static spinlock_t list_lk;
```

**函数（当前状态已标注）**
- `mmap_init()`：**空骨架，待实现**
  - `spinlock_init(&list_lk, "mmap_list")`；
  - 把 `node_list[0..N_MMAP-1]` 串成空闲链表，挂到 `list_head.next`；
  - 必须在使用任何 `mmap_region_alloc` 之前调用一次。
- `mmap_region_alloc()`：**空骨架，待实现；当前 `make build` 的 `-Wreturn-type` 就来自这里**
  - 加锁，取 `list_head.next`；
  - 若为空返回 `0`；
  - 从链表摘下，清零 `node->mmap`，返回 `&node->mmap`；
  - 解锁。
- `mmap_region_free(mmap_region_t *m)`：**空骨架，待实现；`uvm.c: mmap_merge` 依赖它**
  - 因为 `mmap` 是 `mmap_region_node_t` 的第一个成员，可以直接 `(mmap_region_node_t *)m`；
  - 加锁，插回空闲链表，解锁。
- `mmap_show_nodelist()`：**已实现**
  - 遍历仓库链表，打印 `node <数组下标> index = <链表位置>`；
  - 测试 3 期望第一部分 `node X index = X`（X 从 0 到 255），第二部分 node 覆盖 0~255、index 覆盖 0~255 各一次，无重复/遗漏/成环。

**测试 3**
- README 给出了一个临时 `main()`：双核各申请/释放一半节点，用 `over_1..over_4` 做屏障。
- 这个 `main()` 是**测试专用**，测完要恢复常规启动流程。
- 常规 `main()` 也要在 `proc_make_first()` 前调用 `mmap_init()`（README 已特别提醒）。

**验收**
- 双核并发申请/释放后，链表节点数正确、无重复、无环。
- 不出现 `pmem_alloc` 错误（mmap 仓库本身用的是静态数组，不涉及物理页）。

---

### 阶段 4：任务 4——mmap 与 munmap

**先改接口**
- `mem/method.h`：`void uvm_mmap(...)` → `uint64 uvm_mmap(...)`；
- `mem/uvm.c` 的定义同步改成 `uint64`；
- `sys_mmap` 返回最终起始地址（自动选址时尤其重要）。

**`sys_mmap` 逻辑**
```text
arg_uint64(0, &begin)
arg_uint64(1, &len)
if len == 0 || len % PGSIZE != 0: return -1
npages = len / PGSIZE
if begin != 0 && begin % PGSIZE != 0: return -1
return uvm_mmap(begin, npages, PTE_R | PTE_W)
```

**`uvm_mmap` 要点**
- 先 `mmap_region_alloc()` 拿节点；失败返回 `-1`。
- `begin == 0`：从 `MMAP_BEGIN` 开始扫描链表，找到第一个能容纳 `npages` 的空洞；找不到返回 `-1`。
- 否则检查 `begin` 在 `[MMAP_BEGIN, MMAP_END)` 内，且 `begin + npages*PGSIZE <= MMAP_END`。
- 按 `begin` 有序插入；检查与**前驱/后继**是否相邻：
  - 只与前者相邻：`prev->npages += npages`；
  - 只与后者相邻：`next->begin = begin; next->npages += npages`；
  - 两边都相邻：把三者合并成一个节点，释放多余的节点回仓库；
  - 都不相邻：插入新节点。
- 合并完成后，只为**本次新申请的区间**分配物理页并 `vm_mappages`，不要重复映射已经映射的相邻页。
- 失败时回滚：释放已申请的页、把节点还回仓库、恢复链表。

**`sys_munmap` / `uvm_munmap` 要点**
- 参数 `begin/len` 同样检查页对齐、范围、`len>0`。
- 遍历链表，处理与 `[begin, begin+npages*PGSIZE)` 有交集的每个节点：
  - 完全覆盖：`vm_unmappages` 释放页，摘除节点并 `mmap_region_free`；
  - 只覆盖左半：缩小 `npages`；
  - 只覆盖右半：`begin` 右移、`npages` 减少；
  - 覆盖中间：拆成两个节点（需要再从仓库申请一个节点）；
- 只释放实际被 unmap 的页；不要释放范围外的页。
- 返回 0 成功，-1 失败。

**测试 4**
- README 的测试用例覆盖了：相邻合并、间隔插入、自动选址（`begin=0`）、跨多个 region 的 munmap、部分 munmap、再次 munmap 已释放区域等。
- 在 `sys_mmap/sys_munmap` 里加 `uvm_show_mmaplist` 和 `vm_print` 调试输出，和 README 的预期图对照。
- 建议自己补充：重复 mmap 同一区域、跨越 `MMAP_BEGIN/END`、`len` 非页对齐、`begin` 非页对齐、自动选址刚好卡在两个 region 之间。

**验收**
- 每次 mmap/munmap 后链表有序、无重叠、无环；
- 自动选址返回第一个足够大的空洞；
- 用户页表映射与链表一致；
- 失败路径不泄漏节点/物理页。

---

### 阶段 5：任务 5——页表复制与销毁

**当前状态**：
- `copy_range` 已经实现，可以直接作为 `uvm_copy_pgtbl` 的辅助函数；
- `uvm_destroy_pgtbl` 只完成了“先清掉 TRAPFRAME/TRAMPOLINE 两项映射”的外壳，`destroy_pgtbl` 仍为空；
- `uvm_copy_pgtbl` 仍为空；
- **实现前先修正 B.2.1 指出的 trapframe 释放池问题**，否则一调用就会 `pmem_free: invalid page`。

**`uvm_destroy_pgtbl(pgtbl)`**
- 递归遍历三级页表；
- 用 `PTE_CHECK(pte)` 区分“页表页”和“叶子页”：
  - 非叶子：递归下一级，然后释放该级页表页（`pmem_free(pa, true)`）；
  - 叶子：释放对应的用户物理页（`pmem_free(pa, false)`）；
- 最后释放根页表页（`pmem_free(pgtbl, true)`）；
- **不要释放共享的 trampoline 物理页**（它属于内核，所有用户页表共享）；
- trapframe 是每进程独立的，要释放；
- 区分用户页（`false`）和页表页/内核页（`true`）。

**`uvm_copy_pgtbl(old, new, heap_top, ustack_npage, mmap)`**
- `new` 应是已经分配并清零的根页表；
- 共享映射：`TRAMPOLINE` 直接映射同一个 trampoline PA（`PTE_R|PTE_X`）；
- 独立映射：`TRAPFRAME` 新申请一页，拷贝旧 trapframe 内容，映射到 `TRAPFRAME`；
- 用户代码/堆/栈/mmap：按参数给出的范围（或遍历 PTE）逐页 `pmem_alloc(false)`、`memmove` 一页、用相同权限映射；
- 不要复制内核页表项；用户页表里只有用户映射 + trampoline + trapframe；
- 复制完成后，新的 `proc_t` 还要设置 `heap_top/ustack_npage/mmap/heap_base` 等元数据。

**测试 5**
- README 没有给固定测试，建议：
  1. 创建新页表，复制 proczero 的页表；
  2. 用 `vm_print` 对比新旧页表的 VA/PA/flags；
  3. 修改新页表中的用户数据，确认旧页表不受影响（深拷贝）；
  4. 调用 `uvm_destroy_pgtbl`，确认不 panic、不重复释放、trampoline 仍可被其他页表使用；
  5. 检查 `pmem_alloc` 的可分配页数是否回到合理值（可临时打印 `kern_region/user_region.allocable`，但注意它们已不在 `mem/method.h` 导出）。

**常见错误**
- 把 `PTE_U=1` 的用户页当成页表页递归；
- 释放 trampoline（多个页表共享，重复释放会破坏物理页链表）；
- 忘记释放页表页，只释放叶子页；
- 复制时把 trapframe 也共享了（每个进程必须有独立 trapframe）。

## G. 每个阶段的验收清单

- [x] 阶段 0（部分）：`uvm.c`、`mmap.c` 已创建；`mmap_show_nodelist`、`uvm_show_mmaplist`、`mmap_merge`、`copy_range`、部分 `uvm_destroy_pgtbl` 已实现。
- [ ] 阶段 0（剩余）：`make clean && make build` 不再有 `-Wreturn-type`/`-Wunused-function`；用户系统调用号与内核一致；`proc_t` 有初始堆边界字段。
- [ ] 任务 1：`SYS_copyout` 让用户拿到 `1 2 3 4 5`；`SYS_copyin` 打印该数组；`SYS_copyinstr` 打印 `hello, world`。
- [ ] 任务 2：`brk(0)` 查询正确；加/减堆不越界、不重复映射；栈 page fault 自动扩栈，`hello/world` 正常打印。
- [ ] 任务 3：`mmap_init/alloc/free` 完成；双核申请/释放 256 个节点后，`mmap_show_nodelist` 输出无重复、无遗漏、无环；常规 `main` 调用 `mmap_init()`。
- [ ] 任务 4：mmap/munmap 的 7+7 个测试用例全部符合预期；自动选址返回第一个足够大的空洞；链表与页表一致。
- [ ] 任务 5：`uvm_copy_pgtbl` 深拷贝正确；`uvm_destroy_pgtbl` 不 panic、不泄漏、不重复释放 trampoline；先修正 trapframe 释放池问题。

## H. 推荐的学习资料与使用方式

1. **xv6-riscv 教材/源码**
   - 重点章节：页表、trap、系统调用、进程、page fault。
   - 对照本项目的 `vm_getpte/vm_mappages/trap_user_handler/uvm_*` 看，不要逐行照搬。

2. **RISC-V Privileged Specification**
   - 只需要看：特权级、CSR、`satp`、`stvec/sepc/scause/stval/sstatus/sscratch`、`ecall/sret`、SV39 页表格式。

3. **MIT 6.S081 / 6.1810 的 xv6 labs**
   - `copyin/copyout`、`mmap`、`page fault` 的实验思路与本 Lab5 高度相似，可以用来建立直觉。

4. **C 语言**
   - 重点补：指针运算、结构体偏移、链表插入/删除、函数指针数组、`typedef`、头文件依赖。

5. **工具**
   - `grep -rn "函数名" src/` 找定义/调用；
   - `riscv64-linux-gnu-nm` 看符号；
   - `riscv64-linux-gnu-objdump -d` 看入口和指令；
   - GDB 看寄存器/内存。

## I. 常见问题 FAQ

**Q1：为什么 `uvm_copyin` 不能直接 `memmove(dst, src, len)`？**
因为 `src` 是用户虚拟地址，当前内核用的是内核页表，内核页表里没有这个用户 VA 的映射。必须先用用户页表 `p->pgtbl` 查出 PA，再访问。

**Q2：为什么 `uvm_copyin_str` 不能写 `dst[maxlen]`？**
调用方只保证 `dst` 有 `maxlen` 字节；写 `dst[maxlen]` 会越界。按 README 的约定，函数最多复制 `maxlen` 字节，终止由调用方负责。

**Q3：为什么 page fault 后不能 `epc += 4`？**
page fault 的 `sepc` 指向“出错的那条访存指令”，不是 `ecall`。扩栈后必须重新执行这条指令，所以 `epc` 保持不变。只有 `ecall`（`scause==8`）才需要 `+4`。

**Q4：为什么 mmap 要先合并再映射？**
如果先把新区域的页映射了，再发现它与相邻节点合并，可能对同一页重复 `vm_mappages`，触发 `remap` panic。先确定最终的区间，再只为“新申请的、尚未映射的页”建立映射。

**Q5：为什么 `brk` 不能只检查 `USER_BASE`？**
`USER_BASE` 只是用户空间起点；实际镜像可能占多页，后面还可能保留映射。初始堆顶应由“镜像结束地址（以及保留映射）”决定，并保存为不可变字段。否则 `heap_top` 变化后就无法恢复下界。

**Q6：为什么栈增长不需要收缩？**
本实验约定栈只增不减；用户栈的 `ustack_npage` 只增加，不主动回收。这样简化了 page fault 处理，也避免频繁改页表。

**Q7：为什么 `uvm_destroy_pgtbl` 要区分“页表页”和“叶子页”？**
页表页是内核物理页（`pmem_alloc(true)`），叶子页是用户物理页（`pmem_alloc(false)`）。释放错池子会破坏空闲链表。`PTE_CHECK(pte)`（R/W/X 全 0）可以用来判断非叶子页表项。

**Q8：为什么 `user_return` 必须通过 `TRAMPOLINE + (user_return - trampoline)` 调用？**
`user_return` 的第一条指令会 `csrw satp` 切到用户页表。直接调用内核符号地址（`0x8000xxxx`）在用户页表里没有映射，切换后会立刻 instruction page fault。只能调用两张页表都映射的 `TRAMPOLINE` 虚拟地址。

**Q9：`syscall()` 的返回值写到哪里？**
写到 `p->tf->a0`。`user_return` 恢复寄存器时，`a0` 会回到用户程序，用户侧的 `syscall()` 宏正是从 `a0` 读返回值。

**Q10：我应该先做哪个任务？**
先做任务 1。它把“用户态/内核态数据迁移”和“syscall 分发”打通，是后面 brk/mmap 调试的基础。任务 2 依赖任务 1 的 copyinstr 输出；任务 3 是独立资源池，可以并行做；任务 4 依赖任务 2/3；任务 5 最后做。

---

**最后提醒**：本附录没有改动任何代码。开始实现时，建议每完成一个函数就 `make build`，每完成一个任务就 `make clean && make run` 做一次端到端验证；遇到 panic 先看 `scause/sepc/stval`，再对照本文的排查顺序定位。