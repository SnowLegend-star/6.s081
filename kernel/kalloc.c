// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"
// #include<stddef.h>

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
  int sz; //记录当前CPU的freelist有多少个PAGE
} kmem[NCPU];

int
cpuid_Modify(){
  //用原子操作来获取当前CPU
  push_off();
  int cpuid_Cur=cpuid();
  pop_off();
  return cpuid_Cur;
}

void
kinit()
{
  int i;
  for(i=0; i<NCPU; i++){
    initlock(&kmem[i].lock, "kmem");
    kmem[i].sz=0;
  }
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  push_off();
  //初始状态下，当前运行的cpuid一下子获得所有的空闲空间
  // int cpuid=cpuid_Modify();
  int cpuid_Cur=cpuid();
  acquire(&kmem[cpuid_Cur].lock);
  r->next = kmem[cpuid_Cur].freelist;
  kmem[cpuid_Cur].freelist = r;
  kmem[cpuid_Cur].sz++;
  release(&kmem[cpuid_Cur].lock);
  pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  struct run *node_steal;
  int i;

  push_off();
  // int cpuid=cpuid_Modify();
  int cpuid_Cur=cpuid();

  acquire(&kmem[cpuid_Cur].lock);
  r = kmem[cpuid_Cur].freelist;

  //如果当前CPU的freelist为空
  if(!r)
  {
    for (i = 0; i < NCPU; i++)
    {
      if (i == cpuid_Cur)
        continue;
      acquire(&kmem[i].lock);
      // if (kmem[i].freelist && kmem[i].freelist->next)
      if (kmem[i].freelist)
      {
        // 从第一个不空的CPU的freelist截取后半段，同时这个freelist得大于等于两个PAGE
        struct run *tmp = kmem[i].freelist;
        node_steal = kmem[i].freelist;
        for (int j = 1; j < kmem[i].sz / 2; j++)   //西巴，千算万算没看懂这里混进去了i
        {
          // tmp=tmp->next;
        }
        // 已经遍历到了当前freelist的中间部分
        kmem[i].freelist=tmp->next;
        tmp->next = (void *)0;        // 截断当前的freelist
        r = node_steal;
        kmem[i].sz-=kmem[i].sz/2;
        kmem[cpuid_Cur].sz=kmem[i].sz/2;
        release(&kmem[i].lock);
        break;                        //找到合适的freelist就直接退出
      }
      release(&kmem[i].lock);
    }
  }
  // r=node_steal; //可恶，这句话怎么写在if(!r)外面了
  if(r)
    kmem[cpuid_Cur].freelist = r->next;
  kmem[cpuid_Cur].sz--;
  release(&kmem[cpuid_Cur].lock);
  pop_off();

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk

  // struct run* xx=r;
  // if(xx)
  //   printf("xx: %p", xx);
  return (void*)r;
}
