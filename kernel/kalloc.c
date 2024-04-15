// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

struct {
  int pgtbl_index[PHYSTOP/PGSIZE];   //物理内存最多可以分成128MB/4KB=32K
  struct spinlock lock;        //物理页表引用锁
}ref_cnt;

void
kinit()
{
  initlock(&ref_cnt.lock, "kref_cnt");
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE){
    ref_cnt.pgtbl_index[(uint64)p/PGSIZE]=1;
    kfree(p);
  }

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

  acquire(&ref_cnt.lock);
  ref_cnt.pgtbl_index[(uint64)pa/PGSIZE]--;
  if(ref_cnt.pgtbl_index[(uint64)pa/PGSIZE]==0){
  release(&ref_cnt.lock); //什么时候释放都可以
  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
  }
  else
    release(&ref_cnt.lock); //什么时候释放都可以


}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r){
    kmem.freelist = r->next;
    //设置pgtbl_index
    acquire(&ref_cnt.lock);
    ref_cnt.pgtbl_index[(uint64)r/PGSIZE]=1;
    release(&ref_cnt.lock);
  }
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

int physiaclPage_refcnt(void* pa){
  return ref_cnt.pgtbl_index[(uint64)pa/PGSIZE];
}

void modify_pgtbl(void* pa){
  if( (uint64)pa%PGSIZE!=0 || (char*)pa < end || (uint64)pa > PHYSTOP) 
    return ;
  acquire(&ref_cnt.lock);
  ref_cnt.pgtbl_index[(uint64)pa/PGSIZE]++;
  release(&ref_cnt.lock);
}
