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

int hash(int blockno)
{
  return blockno % NBUCKET;
}
struct
{
  struct spinlock lock[NBUCKET];
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head[NBUCKET];
} bcache;

void binit(void)
{
  struct buf *b;

  for (int i = 0; i < NBUCKET; ++i)
  {
    initlock(&bcache.lock[i], "bcache");
    bcache.head[i].next = &bcache.head[i];
    bcache.head[i].prev = &bcache.head[i];
  }

  for (b = bcache.buf; b < bcache.buf + NBUF; ++b)
  {
    b->next = bcache.head[0].next;
    b->prev = &bcache.head[0];
    initsleeplock(&b->lock, "buffer");
    bcache.head[0].next->prev = b;
    bcache.head[0].next = b;
  }
  // initlock(&bcache.lock, "bcache");

  // // Create linked list of buffers
  // bcache.head.prev = &bcache.head;
  // bcache.head.next = &bcache.head;
  // for(b = bcache.buf; b < bcache.buf+NBUF; b++){
  //   b->next = bcache.head.next;
  //   b->prev = &bcache.head;
  //   initsleeplock(&b->lock, "buffer");
  //   bcache.head.next->prev = b;
  //   bcache.head.next = b;
  //}
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf *
bget(uint dev, uint blockno)
{
  struct buf *b;
  int index = hash(blockno);

  acquire(&bcache.lock[index]);
  for (b = bcache.head[index].next; b != &bcache.head[index]; b = b->next)
  {
    if (b->blockno == blockno && b->dev == dev)
    {
      ++b->refcnt;
      release(&bcache.lock[index]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  for (int j = hash(blockno + 1); j != index; j=(j+1)%NBUCKET)
  {
    acquire(&bcache.lock[j]);
    for (b = bcache.head[j].next; b != &bcache.head[j]; b = b->next)
    {
      if (0 == b->refcnt)
      {
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;
        //移动到对应的bucket
        b->next->prev = b->prev;
        b->prev->next = b->next;
        release(&bcache.lock[j]);
        b->next = bcache.head[index].next;
        b->prev = &bcache.head[index];
        bcache.head[index].next->prev = b;
        bcache.head[index].next = b;
        release(&bcache.lock[index]);
        acquiresleep(&b->lock);
        return b;
      }
    }
    release(&bcache.lock[j]);
  }
  panic("bget: no enough buffers");

  // acquire(&bcache.lock);

  // // Is the block already cached?
  // for(b = bcache.head.next; b != &bcache.head; b = b->next){
  //   if(b->dev == dev && b->blockno == blockno){
  //     b->refcnt++;
  //     release(&bcache.lock);
  //     acquiresleep(&b->lock);
  //     return b;
  //   }
  // }

  // // Not cached.
  // // Recycle the least recently used (LRU) unused buffer.
  // for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
  //   if(b->refcnt == 0) {
  //     b->dev = dev;
  //     b->blockno = blockno;
  //     b->valid = 0;
  //     b->refcnt = 1;
  //     release(&bcache.lock);
  //     acquiresleep(&b->lock);
  //     return b;
  //   }
  // }
  // panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf *
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if (!b->valid)
  {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void bwrite(struct buf *b)
{
  if (!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void brelse(struct buf *b)
{
  if (!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int index = hash(b->blockno);
  acquire(&bcache.lock[index]);
  --b->refcnt;
  if (0 == b->refcnt)
  {
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head[index].next;
    b->prev = &bcache.head[index];
    bcache.head[index].next->prev = b;
    bcache.head[index].next = b;
  }
  release(&bcache.lock[index]);
  // acquire(&bcache.lock);
  // b->refcnt--;
  // if (b->refcnt == 0) {
  //   // no one is waiting for it.
  //   b->next->prev = b->prev;
  //   b->prev->next = b->next;
  //   b->next = bcache.head.next;
  //   b->prev = &bcache.head;
  //   bcache.head.next->prev = b;
  //   bcache.head.next = b;
  // }

  // release(&bcache.lock);
}

void bpin(struct buf *b)
{
  int index = hash(b->blockno);
  acquire(&bcache.lock[index]);
  b->refcnt++;
  release(&bcache.lock[index]);
}

void bunpin(struct buf *b)
{
  int index = hash(b->blockno);
  acquire(&bcache.lock[index]);
  b->refcnt--;
  release(&bcache.lock[index]);
}
