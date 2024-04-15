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
// extern int pgtbl_index[32*1024];    //物理内存最多可以分成128MB/4KB=32K

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void
usertrap(void)
{
  int which_dev = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  
  // save user program counter.
  p->trapframe->epc = r_sepc();
  
  if(r_scause() == 8){
    // system call

    if(p->killed)
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;

    // an interrupt will change sstatus &c registers,
    // so don't enable until done with those registers.
    intr_on();

    syscall();
  } else if((which_dev = devintr()) != 0){
    // ok
  } 
  else if(r_scause()==15 || r_scause()==13 ){
    pte_t*  pte;
    char*   mem;
    int     perm;                     //pte低十位的标志
    uint64  pa;                       //发生trap的物理地址
    uint64 addr=r_stval();            //获取出错的地址
    // printf("发生trap的va是: %p\n", addr);
    // //判断出错地址的合法性
    
    if(addr > p->sz || addr > MAXVA)    
      exit(-1);
    // if(addr < p->trapframe->sp )                   //即出错的地址位于guard page   傻逼判断害苦了我啊！
    //   exit(-1);
    
    // printf("当前运行进程为%s, pid为%d\n", p->name, p->pid);
    pte=walk(p->pagetable, addr, 0);                  //获取出错地址对应的pte
    if(pte==0)
      exit(-1);

    // printf("发生trap的PTE为: %p\n",*pte);
    if( (*pte & PTE_RSW) && (*pte & PTE_V) ){         //只处理COW页表   if((*pte) & PTE_RSW & PTE_V) 我是傻逼
    // if(cowpage(p->pagetable,addr)==0){
      addr=PGROUNDDOWN(r_stval());
      pte=walk(p->pagetable, addr, 0);
      // pa=PTE2PA(*pte);
      pa = walkaddr(p->pagetable, addr);
      if(pa==0)                                       //为了确保映射到guard page时不出错
        exit(-1);
      // printf("发生trap的pa是: %p\n", pa);
      // printf("当前运行进程为%s, pid为%d\n", p->name, p->pid);
 
      if(physiaclPage_refcnt((void*)pa) ==1){         //如果对发生trap的页面引用是1
        *pte=(*pte | PTE_W) & ~PTE_RSW;
      }
      else{
        mem=kalloc();
        if(mem==0)
          exit(-1);
        *pte &= ~PTE_V;                                //不加这句会有remap
          //开始给这个出错的pte分配的实际的物理地址
        memmove(mem, (char*)pa, PGSIZE);
        // uvmunmap(p->pagetable, addr, 1, 0);
        perm=(PTE_FLAGS(*pte) | PTE_W) & ~PTE_RSW;
        if(mappages(p->pagetable, addr, PGSIZE, (uint64)mem, perm) <0){
          kfree((void*)mem);
          *pte=*pte&~PTE_V;
        }
        kfree((void*)PGROUNDDOWN(pa));                 //发生trap的pa引用应该减一
      }
    }
    else                                               //一定要及时kill有问题的进程  不然会卡在sbrkfail
      exit(-1);
  }
  else {
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }

  if(p->killed)
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2)
    yield();

  usertrapret();
}

//
// return to user space
//
void
usertrapret(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off();

  // send syscalls, interrupts, and exceptions to trampoline.S
  w_stvec(TRAMPOLINE + (uservec - trampoline));

  // set up trapframe values that uservec will need when
  // the process next re-enters the kernel.
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  uint64 fn = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64,uint64))fn)(TRAPFRAME, satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
    yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
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

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()
{
  uint64 scause = r_scause();

  if((scause & 0x8000000000000000L) &&
     (scause & 0xff) == 9){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000001L){
    // software interrupt from a machine-mode timer interrupt,
    // forwarded by timervec in kernelvec.S.

    if(cpuid() == 0){
      clockintr();
    }
    
    // acknowledge the software interrupt by clearing
    // the SSIP bit in sip.
    w_sip(r_sip() & ~2);

    return 2;
  } else {
    return 0;
  }
}

