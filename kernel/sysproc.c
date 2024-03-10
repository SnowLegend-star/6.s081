#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sysinfo.h"

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

//excesize1 查看可用内存，单位是B
uint64 
sys_checkmem(void){
  return (uint64)(kfmstat());
}

//跟踪系统调用情况
uint64
sys_trace(void){
  int mask;
  if(argint(0,&mask)<0)
    return -1;
  myproc()->mask=mask;
  return 0;
}

//检查系统的内存和进程信息
uint64
sys_sysinfo(void){
  struct sysinfo sysINFO;
  struct proc *p=myproc();
  uint64 sysINFO_user;    //user pointer to struct sysinfo     
  sysINFO.freemem=kfmstat();  //刚才用sys_checkmem是有问题的
  sysINFO.nproc=sum_UNUSED();

  // printf("In kernel, available memory: %dB\n", sysINFO.freemem);
  // printf("In kernel, UNUSED process: %d\n", sysINFO.nproc);

  if(argaddr(0,&sysINFO_user)<0)
    return -1;
  // sysINFO_user=(struct sysinfo)sysINFO_user;
  // printf("The sysinfo from user space: \n")
  // printf("available memory: %d",sysINFO_user);
  if(copyout(p->pagetable, sysINFO_user, (char *)&sysINFO, sizeof(sysINFO)) < 0)
    return -1;

  return 0;
}