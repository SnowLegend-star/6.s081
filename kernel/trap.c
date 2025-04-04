#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// 设置内核时处理异常和陷阱
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

//
// 处理来自用户空间的中断、异常或系统调用。
// 由 trampoline.S 调用
//
void
usertrap(void)
{
  int which_dev = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: 不是从用户模式触发的");

  // 将中断和异常发送到 kerneltrap()，
  // 因为我们现在已经进入内核模式
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  
  // 保存用户程序计数器
  p->trapframe->epc = r_sepc();
  
  if(r_scause() == 8){
    // 系统调用

    if(p->killed)
      exit(-1);

    // sepc 指向 ecall 指令，
    // 但我们希望返回到下一条指令
    // 要不然就会一直陷入ecall函数调用->返回原函数->ecall函数调用这个死循环里面了
    // 而其他类型的trap则不是因为ecall引发的，所以需要返回到原地继续执行原指令
    p->trapframe->epc += 4;   

    // 中断会改变 sstatus 和其他寄存器，
    // 所以在处理完这些寄存器之前不要启用中断
    intr_on();

    syscall();
  } else if((which_dev = devintr()) != 0){
    // 处理中断
  } 
  else if(r_scause()==15 || r_scause()==13){  // 如果是缺页错误

    uint64 mem;
    uint64 low_addr=r_stval();
    // printf("页错误 %p\n", r_stval());
    if(low_addr >= p->sz)    // 这里如果用 low_addr > maxva 就无法通过 unmap 测试
      // p->killed=1 ;
      exit(-1);
    if(low_addr < p->trapframe->sp )  // 如果出错的地址位于 guard page
      // p->killed=1;
      exit(-1);
    
    // if(low_addr>=PGROUNDUP(p->trapframe->sp)||low_addr<PGROUNDDOWN(p->trapframe->sp)){
    //   exit(-1);
    // }


    mem=(uint64)kalloc();
    if(mem==0){
      // uvmdealloc(p->pagetable, low_addr, p->sz - low_addr);
      // 用 killed 参数来杀死进程，而不是直接 return  用 kill 的方式会导致  test copyinstr3: unlink(x) 返回0，而不是 -1
      // p->killed=1;
      exit(-1);         // 用 exit(-1) 也可以，一步到位
    }
    else{
      memset((void *)mem, 0, PGSIZE);
      low_addr=PGROUNDDOWN(low_addr);
      if(mappages(p->pagetable, low_addr, PGSIZE, mem, PTE_W|PTE_R|PTE_U)!=0){
        kfree((void*)mem);
        // uvmdealloc(p->pagetable, low_addr, p->sz - low_addr);
        // p->killed=1;   
        exit(-1);   
      }
    }

  }
  else {
    printf("usertrap(): 未知的 scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }

  if(p->killed)
    exit(-1);

  // 如果这是一个定时器中断，则交出CPU
  if(which_dev == 2)
    yield();

  usertrapret();
}

//
// 返回用户空间
//
void
usertrapret(void)
{
  struct proc *p = myproc();

  // 我们即将将陷阱的目的地从 kerneltrap() 改为 usertrap()，
  // 所以在回到用户空间之前禁用中断，确保 usertrap() 正常工作
  intr_off();

  // 将系统调用、中断和异常发送到 trampoline.S
  w_stvec(TRAMPOLINE + (uservec - trampoline));

  // 设置 trapframe 值，供 uservec 使用
  // 当进程下次重新进入内核时
  p->trapframe->kernel_satp = r_satp();         // 内核页表
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // 进程的内核栈
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid 用于 cpuid()

  // 设置 trampoline.S 的 sret 将使用的寄存器
  // 来跳转到用户空间

  // 将 S Previous Privilege mode 设置为用户模式
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // 清除 SPP 为 0，进入用户模式
  x |= SSTATUS_SPIE; // 启用用户模式中的中断
  w_sstatus(x);

  // 设置 S Exception Program Counter 为保存的用户 pc。
  w_sepc(p->trapframe->epc);

  // 告诉 trampoline.S 切换到用户页表。
  uint64 satp = MAKE_SATP(p->pagetable);

  // 跳转到 trampoline.S 内存顶部，
  // 在那里切换到用户页表，恢复用户寄存器，
  // 并通过 sret 切换到用户模式。
  uint64 fn = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64,uint64))fn)(TRAPFRAME, satp);
}

// 内核代码中的中断和异常通过 kernelvec 进入这里，
// 使用当前的内核栈处理。
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: 不是从 supervisor 模式触发的");
  if(intr_get() != 0)
    panic("kerneltrap: 中断已启用");

  if((which_dev = devintr()) == 0){
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // 如果这是一个定时器中断，且当前进程正在运行，交出CPU
  if(which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
    yield();

  // yield() 可能导致一些陷阱发生，
  // 所以恢复陷阱寄存器以供 kernelvec.S 的 sepc 指令使用
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  acquire(&tickslock);
  ticks++;
  wakeup(&ticks);
  release(&tickslock);
}

// 检查是否是外部中断或软件中断，
// 如果是，处理它。
// 如果是定时器中断，返回 2；
// 如果是其他设备中断，返回 1；
// 如果不是识别的中断，返回 0。
int
devintr()
{
  uint64 scause = r_scause();

  if((scause & 0x8000000000000000L) &&
     (scause & 0xff) == 9){
    // 这是一个来自 PLIC 的 supervisor 外部中断

    // irq 表示哪个设备触发了中断
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    } else if(irq){
      printf("意外的中断 irq=%d\n", irq);
    }

    // PLIC 允许每个设备最多产生一个中断；
    // 告诉 PLIC 该设备可以再次产生中断
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000001L){
    // 来自机器模式定时器中断的软中断，
    // 通过 timervec 在 kernelvec.S 中转发。

    if(cpuid() == 0){
      clockintr();
    }
    
    // 清除 SSIP 位来确认软件中断
    w_sip(r_sip() & ~2);

    return 2;
  } else {
    return 0;
  }
}

