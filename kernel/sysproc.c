#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  backtrace();
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64
sys_sigalarm(void){
  struct proc *p=myproc();
  uint64 handler_function;
  if(argint(0,&(p->time_interval))<0)
    return -1;
  if(argaddr(1,&handler_function)<0)
    return -1;
  if((p->time_interval)==0 && handler_function==0)
    return -1;
  p->handler_function=(void*)handler_function; 
  p->ticks_passed=0;
  // p->ticks_passed=sys_uptime();
  printf("The time interval from user space is: %d\n",p->time_interval);
  printf("The handler_function address from user space is: %p\n",p->handler_function);
  return 0;
}

uint64
sys_sigreturn(void){
  struct proc *p=myproc();
  p->trapframe->epc=p->re_epc;      //存储原本应该返回到的地址(ecall的后一句指令)
  p->trapframe->s0=p->s0;
  p->trapframe->s1=p->s1;
  p->trapframe->ra=p->ra;
  p->trapframe->sp=p->sp;  
  p->trapframe->a1=p->a1;
  p->trapframe->a0=p->a0;
  p->trapframe->a5=p->a5;

  p->flag=0;
  return 0;
}
