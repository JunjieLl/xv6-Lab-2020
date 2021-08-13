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

#define LOCKNAMEMAXLEN 20
struct {
  struct spinlock lock;
  struct run *freelist;
  char lockName[LOCKNAMEMAXLEN];
} kmem[NCPU];

void
kinit()
{
  for(int i=0;i<NCPU;++i){
    snprintf(kmem[i].lockName, LOCKNAMEMAXLEN,"kmem%d",i);
    initlock(&kmem[i].lock,kmem[i].lockName);
  }
  //initlock(&kmem.lock, "kmem");
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
  push_off();
  int cpuId=cpuid();

  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem[cpuId].lock);
  r->next = kmem[cpuId].freelist;
  kmem[cpuId].freelist = r;
  release(&kmem[cpuId].lock);

  pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  push_off();
  int cpuId=cpuid();

  struct run *r;

  //acquire(&kmem.lock);
  // r = kmem.freelist;
  // if(r)
  //   kmem.freelist = r->next;
  //release(&kmem.lock);
  acquire(&kmem[cpuId].lock);
  r=kmem[cpuId].freelist;
  if(r){
    kmem[cpuId].freelist=r->next;
    release(&kmem[cpuId].lock);
  }
  else{
    release(&kmem[cpuId].lock);
    for(int otherCPU=0;otherCPU<NCPU;++otherCPU){
      if(otherCPU!=cpuId){
        acquire(&kmem[otherCPU].lock);
        r=kmem[otherCPU].freelist;
        if(r){
          kmem[otherCPU].freelist=r->next;
          release(&kmem[otherCPU].lock);
          break;
        }
        release(&kmem[otherCPU].lock);
      }
    }
  }

  pop_off();

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk

  return (void*)r;
}
