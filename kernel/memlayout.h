// 物理内存布局

// qemu -machine virt 的内存布局如下：
// 参考 qemu 的 hw/riscv/virt.c:
//
// 00001000 -- 启动 ROM，由 qemu 提供
// 02000000 -- CLINT（Core Local Interruptor，本地中断控制器）
// 0C000000 -- PLIC（Platform-Level Interrupt Controller）
// 10000000 -- uart0（串口）
// 10001000 -- virtio 磁盘设备
// 80000000 -- boot ROM 在 machine 模式下跳转到这里
//             - 内核会加载到此地址
// 80000000 之后的未使用 RAM。

// 内核对物理内存的使用如下：
// 80000000 -- entry.S，然后是内核的代码和数据段
// end -- 内核页面分配区域的起始位置
// PHYSTOP -- 内核可用的 RAM 结束位置

// qemu 将 UART 寄存器映射到这个物理地址
#define UART0 0x10000000L
#define UART0_IRQ 10

// virtio mmio 接口（用于虚拟磁盘）
#define VIRTIO0 0x10001000
#define VIRTIO0_IRQ 1

// 本地中断控制器，包含定时器功能
#define CLINT 0x2000000L
#define CLINT_MTIMECMP(hartid) (CLINT + 0x4000 + 8*(hartid)) // 每个 hart 的定时器比较寄存器
#define CLINT_MTIME (CLINT + 0xBFF8) // 自启动以来的周期数（时间戳）

// qemu 将可编程中断控制器（PLIC）映射到这个地址
#define PLIC 0x0c000000L
#define PLIC_PRIORITY (PLIC + 0x0) // 中断优先级寄存器
#define PLIC_PENDING (PLIC + 0x1000) // 挂起中断标志
#define PLIC_MENABLE(hart) (PLIC + 0x2000 + (hart)*0x100) // Machine 模式启用的中断
#define PLIC_SENABLE(hart) (PLIC + 0x2080 + (hart)*0x100) // Supervisor 模式启用的中断
#define PLIC_MPRIORITY(hart) (PLIC + 0x200000 + (hart)*0x2000) // Machine 模式中断优先级
#define PLIC_SPRIORITY(hart) (PLIC + 0x201000 + (hart)*0x2000) // Supervisor 模式中断优先级
#define PLIC_MCLAIM(hart) (PLIC + 0x200004 + (hart)*0x2000) // Machine 模式中断请求读取/完成写入
#define PLIC_SCLAIM(hart) (PLIC + 0x201004 + (hart)*0x2000) // Supervisor 模式中断请求读取/完成写入

// 内核期望从物理地址 0x80000000 到 PHYSTOP 之间是内存区域，
// 这段区域用于内核和用户程序的页面分配。
#define KERNBASE 0x80000000L
#define PHYSTOP (KERNBASE + 128*1024*1024) // 内核可用内存最大为 128MB

// 将 trampoline 页映射到虚拟地址空间的最高地址处，
// 同时映射到用户空间和内核空间。
#define TRAMPOLINE (MAXVA - PGSIZE)

// 在 trampoline 下方为每个内核线程分配栈空间，
// 每个栈空间周围都有无效的保护页。
#define KSTACK(p) (TRAMPOLINE - ((p)+1)* 2*PGSIZE)

// 用户虚拟内存布局（从地址 0 开始）：
//   text 代码段
//   原始数据段和 BSS 段
//   固定大小的栈
//   可扩展的堆
//   ...
//   TRAPFRAME（p->trapframe，用于保存陷入时的寄存器状态）
//   TRAMPOLINE（和内核中的 trampoline 是同一页）
#define TRAPFRAME (TRAMPOLINE - PGSIZE)
