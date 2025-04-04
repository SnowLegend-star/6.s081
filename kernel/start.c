#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

void main();
void timerinit();

// entry.S 需要为每个 CPU 分配一个栈。
__attribute__ ((aligned (16))) char stack0[4096 * NCPU];

// 用于定时器中断的临时区域，每个 CPU 一个。
uint64 mscratch0[NCPU * 32];

// machine 模式下用于定时器中断的汇编代码（在 kernelvec.S 中定义）
extern void timervec();

// entry.S 在 machine 模式下跳转到这里，使用 stack0 作为栈。
void
start()
{
  // 设置 M 模式下的 Previous Privilege 模式为 Supervisor 模式，以便 mret 指令切换到 S 模式。
  unsigned long x = r_mstatus();
  x &= ~MSTATUS_MPP_MASK;
  x |= MSTATUS_MPP_S;
  w_mstatus(x);

  // 设置 M 模式异常返回地址（MEPC）为 main 函数地址，以便 mret 跳转执行。
  // 要求使用 gcc 的 -mcmodel=medany 选项编译。
  w_mepc((uint64)main);

  // 暂时禁用分页。
  w_satp(0);

  // 将所有中断和异常委托给 S（Supervisor）模式处理。
  w_medeleg(0xffff);
  w_mideleg(0xffff);
  w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE); // 使能外部中断、定时器中断、软件中断

  // 请求定时器中断。
  timerinit();

  // 将当前 CPU 的 hartid 写入 tp 寄存器，供 cpuid() 使用。
  int id = r_mhartid();
  w_tp(id);

  // 切换到 S 模式并跳转到 main()。
  asm volatile("mret");
}

// 设置 machine 模式下接收定时器中断，
// 中断会跳转到 kernelvec.S 中的 timervec，
// 然后被转换成软件中断，交由 trap.c 中的 devintr() 处理。
void
timerinit()
{
  // 每个 CPU 都有一个独立的定时器中断源。
  int id = r_mhartid();

  // 请求 CLINT 提供定时器中断。
  int interval = 1000000; // 周期数；在 qemu 中约为 1/10 秒
  *(uint64*)CLINT_MTIMECMP(id) = *(uint64*)CLINT_MTIME + interval;

  // 为 timervec 准备 mscratch 中的内容：
  // scratch[0..3]：供 timervec 保存寄存器使用。
  // scratch[4]：CLINT 的 MTIMECMP 寄存器地址。
  // scratch[5]：两次中断之间的时间间隔（周期数）。
  uint64 *scratch = &mscratch0[32 * id];
  scratch[4] = CLINT_MTIMECMP(id);
  scratch[5] = interval;
  w_mscratch((uint64)scratch);

  // 设置 machine 模式的 trap 处理函数为 timervec。
  w_mtvec((uint64)timervec);

  // 启用 machine 模式中断。
  w_mstatus(r_mstatus() | MSTATUS_MIE);

  // 启用 machine 模式下的定时器中断。
  w_mie(r_mie() | MIE_MTIE);
}
