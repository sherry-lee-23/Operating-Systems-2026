# 阶段 5 自测：用户页表的复制与销毁（`official-test-5.png` 证据）

本目录下的 `stage5-selftest.c` 是 Lab5 **阶段 5（任务 5：`uvm_copy_pgtbl` / `uvm_destroy_pgtbl`）**
的内核自测代码。它构造一个与 `proczero` 同布局的"父页表"，用 `uvm_copy_pgtbl` 复制出"子页表"，
然后逐项断言复制语义与销毁行为，最后打印 `PASS` / `FAIL`。

## 0. 它为什么不在 `src/` 里

- `tests/` 不在 Makefile 的源码通配范围（`src/kernel/*.c`、`src/kernel/*/*.c`）内，
  因此**不会参与常规构建**，也不会污染内核代码。
- 但它是内核态代码（要直接操作页表），只能在 `make run` 时由内核执行，
  所以需要"临时接入"一次。

## 1. 何时运行

在与阶段 5 相关的提交里、需要生成 `tests/official-test-5.png` 截图时运行一次即可。
平时（阶段 1–4 的回归、最终提交的常规启动）**不需要**接入本自测。

## 2. 临时接入步骤

```bash
# 1) 把自测文件拷贝到内核源码树（文件名固定为 uvm_selftest.c，
#    Makefile 的 src/kernel/*/*.c 通配符会自动把它编进内核）
cp tests/stage5-selftest.c src/kernel/mem/uvm_selftest.c
```

```c
// 2) 在 src/kernel/main.c 中临时加钩子（只在 CPU0 的全局初始化里，mmap_init() 之后）
if (mycpuid() == 0) {
    pmem_init();
    kvm_init();
    kvm_inithart();
    trap_kernel_init();

    mmap_init();

    // ===== 临时：阶段5自测 =====
    extern void uvm_selftest(void);
    uvm_selftest();
    // ==========================

    __sync_synchronize();
    boot_ready = 1;
}
```

> 说明：自测会自己 `pmem_alloc` 构造页表、跑完自行销毁，不依赖 `proczero`；
> 放在 `mmap_init()` 之后、`proc_make_first()` 之前即可（此时物理页管理器与内核页表已就绪）。

## 3. 构建与运行

```bash
make clean && make build && make run
```

`make run` 会常驻，观察串口输出到 `===== stage5 selftest PASS =====` 后截图，
按 `Ctrl-A` 再按 `X` 退出 QEMU（或用 `timeout -s KILL 20 make run` 限时采集）。

## 4. 预期输出要点

```text
===== stage5 selftest begin =====
copy page[0] va=0x0000000000000000 parent_pa=0x...fff000 child_pa=0x...ffb000 独立=1 内容一致=1
copy page[1] va=0x0000000000001000 parent_pa=0x...ffe000 child_pa=0x...ffa000 独立=1 内容一致=1
copy page[2] va=0xfffffffffaffe000 parent_pa=0x...ffd000 child_pa=0x...ff9000 独立=1 内容一致=1
copy page[3] va=0xffffffffffffd000 parent_pa=0x...ffc000 child_pa=0x...ff8000 独立=1 内容一致=1
trapframe  parent_pa=0x...3bf000 child_pa=0x...3b8000 私有=1
trampoline parent_pa=0x...003000 child_pa=0x...003000 共享=1
write isolation: 父写不影响子=1 子写不影响父=1
destroy child: 释放用户页=4(期望4) 释放内核页=7(根+trapframe+页表页, >=2)
destroy child 后父页表仍可读写: 1
destroy parent: 释放用户页=4(期望4) 释放内核页=7, 未 panic => trampoline 未被释放
===== stage5 selftest PASS =====
```

逐条含义：

| 行 | 断言内容 |
|---|---|
| `copy page[0..3]` | 代码（VA 0）/ 堆（`PGSIZE`）/ mmap（`MMAP_BEGIN`）/ 栈（`TRAPFRAME-PGSIZE`）四个用户页：父子里 **PA 不同（独立=1）**、**内容相同（内容一致=1）**，即深拷贝而非共享 |
| `trapframe ... 私有=1` | 子页表的 `TRAPFRAME` 指向复制前为子进程单独申请的页（`c_tf == ctf`），且与父不同 |
| `trampoline ... 共享=1` | 父子 `TRAMPOLINE` 的 PA 相同，且等于符号 `trampoline` 的地址（内核镜像内） |
| `write isolation` | 父页写 `X` 后子页仍是 `H`；子页写 `Y` 后父页仍是 `M` —— 双向物理隔离 |
| `destroy child: 释放用户页=4` | 销毁子进程精确归还 4 个用户物理页（不多不少） |
| `释放内核页=...` | 归还根页表 + 私有 trapframe + 中间级页表页（父/子同布局时数值相同） |
| `destroy child 后父页表仍可读写: 1` | 销毁一张页表不影响另一张 |
| `destroy parent: ... 未 panic` | 销毁时 `trampoline` 只清 PTE、**不释放物理页**；若误释放会 `panic: pmem_free: invalid page`（它位于内核镜像，不在 pmem 池） |
| `PASS` | 以上全部通过 |

退出后如需再验证"用户进程仍能 ecall 往返"，可把 `src/user/initcode.c` 换回
README 的任意测试（如测试 1/4），`make clean && make build && make run`，
应能看到对应串口输出且无 `panic`。

## 5. 截图后清理

```bash
# 1) 删除临时接入的内核文件
rm src/kernel/mem/uvm_selftest.c

# 2) 删除 main.c 中的 extern 声明与 uvm_selftest(); 调用（恢复成只剩 mmap_init()）

# 3) 重新构建，确认去掉钩子后仍可正常编译运行
make clean && make build && make run

# 4) 确认内核侧无残留；tests/ 下的两个文件必须保留
git status
#   期望：只看到 tests/stage5-selftest.c 与 tests/stage5-selftest.md 为新增，
#   src/ 下没有任何与 selftest 相关的改动
```

> 注意：`make clean` 会删除仓库中受版本控制的 `.gdbinit`（Makefile 把它当生成物）。
> 若 `git status` 出现 `D .gdbinit`，用 `git checkout -- .gdbinit` 恢复即可。

## 6. 实现备注（给评审/助教）

- 复制：`uvm_copy_pgtbl` 递归遍历用户页表三级结构，对每个有效叶子 PTE 用
  `copy_range`（`pmem_alloc(false)` + `memmove` + `vm_mappages`，权限沿用旧 PTE）逐页深拷贝；
  按虚拟地址跳过 `TRAPFRAME` 与 `TRAMPOLINE`。
- 销毁：`uvm_destroy_pgtbl` 先按**内核物理区**释放私有 trapframe（`pmem_free(pa, true)`，
  修正了骨架原先用 `vm_unmappages(..., freeit=true)` 会按用户区释放导致
  `pmem_free: invalid page` 的问题），再仅清除 `TRAMPOLINE` 的 PTE，
  然后 `destroy_pgtbl(pgtbl, 3)` 递归释放用户页与中间级页表页，最后释放根页表页。
