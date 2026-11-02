# 边界自测：`new_heap_top < heap_base` 必须被拒绝（D2 第 1 问）

本目录下的 `self-test-initcode.c` 是 Lab5 的**边界自测**用户程序，
用于验证 D2 契约第 1 问中"必须拒绝"的分支，并证明拒绝之后进程的代码/数据页仍然可用。

对应提交件：`tests/self-test.md` + `tests/self-test.png`（截图）。

---

## 1. 测试的边界是什么

**边界条件**：`sys_brk(new_heap_top)` 中 `new_heap_top < p->heap_base`。

- `heap_base` 是创建进程时按**实际装入的镜像末尾（按页向上对齐）**写入 `proc_t` 的字段，
  在本分支里 `proc_make_first()` 中为：

  ```c
  p->heap_base = ALIGN_UP(initcode_len, PGSIZE);   // 本分支 initcode < 1 页 => 0x1000
  p->heap_top  = p->heap_base;
  ```

- 测试请求 `brk(base - PGSIZE)`，即"只比下界低一页"，正好压在边界之外
  （既不是远低于下界，也不是恰好等于下界），是能触发检查的最"贴身"的非法值。
- 同时验证反面：**恰好等于 `heap_base` 的查询（`brk(0)`）应正常返回**，
  说明边界本身没有被错误地排除掉。

## 2. 为什么这是边界

用户虚拟地址空间自低向高依次是：

```text
0 ~ heap_base        装入的代码/数据镜像（不可解除）
heap_base ~ heap_top 用户堆（brk 手动伸缩，可增可减）
...
MMAP_BEGIN ~ MMAP_END   mmap 区
MMAP_END ~ TRAPFRAME   用户栈预留
```

`heap_base` 之下就是**已经装入并正在执行的代码/数据页**。
若 `sys_brk` 只用 `USER_BASE` 或"当前 `heap_top`"做下界，而没有用 `heap_base`：

- `uvm_heap_ungrow` 会按 `[ALIGN_UP(new_top), ALIGN_UP(cur_top))` 释放整页；
  当 `new_top < heap_base` 时，这个区间会**覆盖代码页**（VA 0 那一页），
  进而 `vm_unmappages(..., freeit=true)` 把代码页 `pmem_free` 归还并清 PTE；
- 用户态随后取指/取常量串立刻缺页 —— 由于本实验不做缺页回填，
  会走到 `trap_user_handler` 的 `unexpected exception` 分支直接 `panic`；
- 即使侥幸没有立刻崩，堆与镜像重叠也会造成越界写、静默数据破坏。

所以这条边界是"进程还能不能活下去"的红线，属于 Spec 要求的必测边界。

## 3. 预期结果

以实际查询值为准（本分支实测 `heap_base == heap_top == 0x1000`，**仅作示例**）：

| 步骤 | 预期串口输出 | 说明 |
|---|---|---|
| `brk(0)` 查询 | `sys_brk: query heap_top = 0x0000000000001000, heap_base = 0x0000000000001000` | 查询返回值 = `heap_base` |
| `brk(base - PGSIZE)` | `sys_brk: invalid heap_top = 0x0000000000000000 (heap_base = 0x0000000000001000, MMAP_BEGIN = 0x...)` | 命中 `new_heap_top < heap_base` 分支，返回 `(uint64)-1` |
| 再次 `brk(0)` | `sys_brk: query heap_top = 0x0000000000001000 ...` | 被拒绝的请求**没有**改动 `heap_top`（仍等于 `base`） |
| `SYS_copyout` + `SYS_copyin` | `sys_copyin: 1 2 3 4 5 ` | 代码页 / 用户数组所在栈页均可正常读写 |
| `SYS_copyinstr` | `sys_copyinstr: still alive` | 代码段中的字符串常量仍可读 |
| 全程 | **无 `page fault`、无 `panic`、无 `unexpected exception`** | 没有任何页被误解除 |

关键行（供截图后核对）：

```text
sys_brk: query heap_top = 0x0000000000001000, heap_base = 0x0000000000001000
sys_brk: invalid heap_top = 0x0000000000000000 (heap_base = 0x0000000000001000, MMAP_BEGIN = 0x...)
sys_brk: query heap_top = 0x0000000000001000, heap_base = 0x0000000000001000
sys_copyin: 1 2 3 4 5
sys_copyinstr: still alive
```

若打印出 `FAIL: brk accepted below heap_base` 或 `FAIL: heap_top changed by rejected brk`，
说明边界检查失效（该分支在正确实现下永远不会执行）。

> 说明：`%x` 走的既有 `printf` 实现用 `va_arg(ap, int)` 取值，只显示低 32 位地址，
> 因此 `0x1000` / `0x0` 这类小值显示为 `0x0000000000001000` / `0x0000000000000000`；
> 大地址（如 `MMAP_BEGIN`）只保证低 32 位正确，不影响本测试判定。

## 4. 如何运行

```bash
# 1) 拷入用户程序（截图时才做；tests/ 下保留原文件）
cp tests/self-test-initcode.c src/user/initcode.c

# 2) 必须 clean 重建：本仓库 Makefile 未 include .d，
#    改 initcode 后增量构建不会重编 proc.o
make clean && make build && make run
```

`make run` 会常驻，看到 `sys_copyinstr: still alive` 后截图保存为 `tests/self-test.png`，
然后按 `Ctrl-A` 再按 `X` 退出 QEMU（或用 `timeout -s KILL 15 make run` 限时采集）。

截图后恢复：

```bash
git checkout -- src/user/initcode.c    # 或 cp 回你自己的测试版本
make clean && make build
git status                             # src/ 下应无本测试残留
```

注意事项：

- `make clean` 会删除仓库中受版本控制的 `.gdbinit`（Makefile 把它当生成物），
  若 `git status` 出现 `D .gdbinit`，用 `git checkout -- .gdbinit` 恢复。
- 本测试**只调用已有 syscall**（`SYS_brk` / `SYS_copyout` / `SYS_copyin` / `SYS_copyinstr`），
  不新增 syscall、不修改内核。

## 5. 对应 Spec 的要求

Spec 第 4 节要求"自主测试必须覆盖一个边界"，并在 D2 中明确：

> 必须拒绝：`new_heap_top < heap_base`（初始镜像保留边界，会解除代码/数据页；
> `heap_base` 为创建进程时存入的字段，不得用当前 `heap_top` 反推）

本测试正是针对该"必须拒绝"条件的**下边界**（低一页）做黑盒验证：
既检查返回值，也检查副作用（`heap_top` 不变），并用后续 ecall 证明镜像未被破坏。

## 6. 断言依据（为什么这样就能证明）

- 若实现**没有**检查 `heap_base`：`sys_brk` 会接受 `base - PGSIZE`，
  `uvm_heap_ungrow` 释放 `[ALIGN_UP(0), ALIGN_UP(0x1000))` = `[0, 0x1000)`
  ——也就是**代码页所在的第 0 页**，`pmem_free` 归还物理页并清 PTE。
  紧接着的 `syscall(SYS_copyout, L)` 需要实例化/执行用户代码，
  取指即触发 13 号异常；因为本实验不做"非法地址"的缺页回填，
  `trap_user_handler` 会打印 `unexpected exception` 并 `panic`。
  ⇒ **只要后续三次 ecall 正常返回，就证明代码页没有被解除映射。**
- `FAIL: brk accepted below heap_base` 是显式反证分支：
  返回值不是 `-1` 就说明检查缺失（等价于"接受了非法请求"）。
- 第二次 `brk(0)` 与 `base` 比较，验证"拒绝"是**无副作用**的拒绝
  （而不是"先改 `heap_top` 再报错"）。
- `sys_copyinstr: still alive` 额外覆盖代码段中的**只读字符串**：
  数据/字符串仍可读，说明镜像内容保持完好（未发生整页释放后的静默复用）。

## 7. 与 D2 其他分支的关系（本测试不覆盖的部分）

本测试只覆盖"下界拒绝"。D2 第 1 问中的另一条必须拒绝条件
（`new_heap_top > MMAP_BEGIN`，防止堆压到 mmap 区）以及合法增长/缩减、
栈自动扩展等场景，已分别由官方测试 4（mmap/munmap 边界）与官方测试 2（堆增 9 页、减 5 页、
栈跨页缺页）覆盖。
