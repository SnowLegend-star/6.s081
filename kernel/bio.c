// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKET 13
// uint ticks;


struct Hash_Bucket{
  struct spinlock lock;
  struct buf head;      //哈希桶内部由链表构成
};

struct {
  struct spinlock lock;
  struct buf buf[NBUF];
  struct Hash_Bucket Hash_Bucket[NBUCKET];
  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  // struct buf head;
} bcache;

//用blockno进行哈希映射
int Hash(int blockno){
  return blockno%NBUCKET;
}

//获取当前的ticks
uint get_ticks(){
  uint tick_Cur;
  acquire(&tickslock);
  tick_Cur=ticks;
  release(&tickslock);
  return tick_Cur;
}

void
binit(void)
{
struct buf *b;
// struct buf *tmp;
//  initlock(&bcache.lock, "bcache");
  
  for(int i=0; i<NBUCKET; i++){
    initlock(&bcache.Hash_Bucket[i].lock, "bcache");             //把整个缓冲区的大锁换成每个哈希桶的小锁
    bcache.Hash_Bucket[i].head.prev=&bcache.Hash_Bucket[i].head;
    bcache.Hash_Bucket[i].head.next=&bcache.Hash_Bucket[i].head;
  }

  // Create linked list of buffers
  for(int i=0; i<NBUF; i++){
    b = &bcache.buf[i];
    // struct Hash_Bucket bucket = bcache.Hash_Bucket[i%NBUCKET];  //bucket[i%NBUCKET]可以拥有buf[i]这个缓冲区  这句话直接坏事
    b->ticks=0;
    b->next = bcache.Hash_Bucket[i%NBUCKET].head.next;
    b->prev = &bcache.Hash_Bucket[i%NBUCKET].head;
    initsleeplock(&b->lock, "buffer");
    bcache.Hash_Bucket[i%NBUCKET].head.next->prev=b;
    bcache.Hash_Bucket[i%NBUCKET].head.next=b;
    
    // b->next = bcache.Hash_Bucket[0].head.next;
    // b->prev = &bcache.Hash_Bucket[0].head;
    // initsleeplock(&b->lock, "buffer");
    // bcache.Hash_Bucket[0].head.next->prev=b;
    // bcache.Hash_Bucket[0].head.next=b;

    // printf("bucket[%d]的内容如下：", i%NBUCKET);
    // for(tmp=bcache.Hash_Bucket[i%NBUCKET].head.next; tmp!=&bcache.Hash_Bucket[i%NBUCKET].head; tmp=tmp->next){
    //   printf("%p ",tmp);
    // }
    // printf("\n");
  }

  // printf("\n");
  // for(int i=0; i<NBUCKET; i++){
  //   // struct buf *tmp=&bcache.Hash_Bucket[i].head;
  //   printf("bucket[%d]的内容如下：", i);
  //   for(b=bcache.Hash_Bucket[i].head.next; b!=&bcache.Hash_Bucket[i].head; b=b->next){
  //     printf("%p ",b);
  //   }
  //   printf("\n");
  // }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  int index=Hash(blockno);    //查找当前块应该在哪个bucket里面

  // acquire(&bcache.lock);

  // // Is the block already cached?
  acquire(&bcache.Hash_Bucket[index].lock);         //先获得这个bucket的自旋锁
  //从bucket真正的第一个buf元素开始查找
  // struct buf tmp= bcache.Hash_Bucket[index].head;   //增加代码可读性
  // printf("tmp->next的值是: %p\n", tmp.next);
  //查找当前块是不是已经被缓存了
  for(b = bcache.Hash_Bucket[index].head.next ; b != &bcache.Hash_Bucket[index].head; b = b->next){
    if(b->dev==dev && b->blockno==blockno){
      b->refcnt++;
      release(&bcache.Hash_Bucket[index].lock);
      acquiresleep(&b->lock);                       //这句话的目的是什么？
      return b;
    }
  }
  // printf("卡在“查找当前块是不是已经被缓存了” \n");

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.

  // tmp= bcache.Hash_Bucket[index].head;
  // printf("tmp->next的值是: %p\n", tmp.next);
  for(b = bcache.Hash_Bucket[index].head.next ; b != &bcache.Hash_Bucket[index].head; b = b->next){
      if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.Hash_Bucket[index].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  //如果当前bucket没有buf了，从其他bucket里面抢夺一个用用(:
  //这里的思路和kalloc的修改差不多
  struct buf *buf_available=0;
  int buf_from=-1;             //记录这块空闲的buf来自于哪个bucket
  for(int i=0; i<NBUCKET; i++){
    if(i==index)
      continue;
    acquire(&bcache.Hash_Bucket[i].lock);
    for(b = bcache.Hash_Bucket[i].head.next; b != &bcache.Hash_Bucket[i].head; b = b->next){
      if(b->refcnt==0){
        if(buf_available==0 || buf_available->ticks > b->ticks){  //利用LRU选择一个空闲块,效率是真低    用ticks简直是画蛇添足
          buf_available=b;
          buf_from=i;
        }
      }
    }
    release(&bcache.Hash_Bucket[i].lock);
  }

  if(buf_available){
    b->dev = dev;
    b->blockno = blockno;
    b->valid = 0;
    b->refcnt = 1;

    //从原来的bucket中取出这个buf
    acquire(&bcache.Hash_Bucket[buf_from].lock);
    buf_available->next->prev = buf_available->prev;
    buf_available->prev->next = buf_available->next;
    release(&bcache.Hash_Bucket[buf_from].lock);

    //把这个空闲buf插入到bucket[index]中   头插法
    buf_available->next = bcache.Hash_Bucket[index].head.next;
    buf_available->prev = &bcache.Hash_Bucket[index].head;
    bcache.Hash_Bucket[index].head.next->prev=buf_available;
    bcache.Hash_Bucket[index].head.next=buf_available;
    release(&bcache.Hash_Bucket[index].lock);

    acquiresleep(&buf_available->lock);
    return buf_available;
  }
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }

  b->ticks=get_ticks();
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
  b->ticks=get_ticks();
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  // acquire(&bcache.lock);
  // b->refcnt--;
  // if (b->refcnt == 0) {
  //   // no one is waiting for it.
    // b->next->prev = b->prev;
    // b->prev->next = b->next;
    // b->next = bcache.head.next;
    // b->prev = &bcache.head;
    // bcache.head.next->prev = b;
    // bcache.head.next = b;
  // }
  
  // release(&bcache.lock);

  int index=Hash(b->blockno);
  acquire(&bcache.Hash_Bucket[index].lock);
  b->refcnt--;
  // if(b->refcnt==0)
  //   b->ticks=get_ticks();
  release(&bcache.Hash_Bucket[index].lock);
}

void
bpin(struct buf *b) {

  int index=Hash(b->blockno);
  acquire(&bcache.Hash_Bucket[index].lock);
  b->refcnt++;
  release(&bcache.Hash_Bucket[index].lock);
}

void
bunpin(struct buf *b) {

  int index=Hash(b->blockno);
  acquire(&bcache.Hash_Bucket[index].lock);
  b->refcnt--;
  release(&bcache.Hash_Bucket[index].lock);
}
