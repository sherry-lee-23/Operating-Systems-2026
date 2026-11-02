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
    return -1;
}

/*
    增加一段内存映射
    uint64 start 起始地址
    uint32 len   范围 (字节,需检查是否是page-aligned)
    成功返回映射空间的起始地址, 失败返回-1
*/
uint64 sys_mmap()
{
    return 0;
}

/*
    解除一段内存映射
    uint64 start 起始地址
    uint32 len   范围 (字节, 需检查是否是page-aligned)
    成功返回0 失败返回-1
*/
uint64 sys_munmap()
{
    return 0;
}