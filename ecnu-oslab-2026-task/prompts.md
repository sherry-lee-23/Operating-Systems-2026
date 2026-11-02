## 协作 1：最小补桩授权（让 Lab4 基线可运行）

- **提示词要点**：
  - 允许仅为通过编译给 Lab5 空函数补**最简返回值**（`return NULL/0/(uint64)-1`），不实现逻辑、不初始化
  - 目标：编出 `kernel-qemu.elf` 并跑一次 QEMU，验证 `proczero: hello world`。
  - 同样禁止实现 `uvm_copyin/copyout/copyinstr/brk/mmap/munmap` 真实逻辑。

- **AI 发现/动作**：
  - 补 `mmap_region_alloc → NULL`；`uvm_mmap_find / uvm_heap_grow / uvm_heap_ungrow → 0`；`uvm_ustack_grow → (uint64)-1`（按其注释"失败返回 -1"，未盲目填 0）。
  - 再遇 `-Wunused-function`（`mmap_merge / uvm_mmap_find / copy_range` 因 `uvm_mmap/copy_pgtbl` 仍空而无调用者），弹 `AskUser` 三选一。

- **我的决策**：选 `__attribute__((unused))`——只动声明行，不改逻辑、不改调用方、不扩大改动范围。

- **结果**：`make clean && make build` 成功；QEMU 输出：
  ```text
  cpu 0 is booting!
  cpu 1 is booting!
  cpu 0: trap enabled
  cpu 1: trap enabled
  proczero: hello world
  proczero: hello world


#### pic

[tests/official-test-1.png](image.png)