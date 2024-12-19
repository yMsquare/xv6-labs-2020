#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fcntl.h"
#include "vma.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
extern VMAlist vmalist;

void vma_init()
{
  initlock(&vmalist.lock, "vma_list");
  printf("init lock\n");
}

void acquireVMA()
{
  acquire(&vmalist.lock);
}

void releaseVMA()
{
  release(&vmalist.lock);
}

// 找到一个可用的vma
struct VMA *
alloc_vma()
{
  uint64 max_addr = TRAPFRAME;
  int target = -1;
  acquire(&vmalist.lock);
  for(int i = 0; i < VMA_SIZE; i++){
    if(vmalist.vmas[i].in_use){
      continue;
    } else {
      vmalist.vmas[i].in_use = 1;
      target = i;
      break;
    }
  }
  release(&vmalist.lock);
  if(target >= VMA_SIZE){
    panic("vma size");
  }
  // printf("--allocated vma : %p,target : %d\n",&vmalist.vmas[target],target);
  // for (struct VMA *cur = &vmalist.vmas[target]; cur; cur = cur->next) {
    // printf("Existing VMA: %p, addr: %p, length: %d\n", cur, cur->addr,
          //  cur->length);
// }
  return &vmalist.vmas[target];
}

// dealloc
int dealloc_vma(struct VMA * vma){
    acquireVMA();
    vma->in_use = 0;
    releaseVMA();
    return 0;
}

struct VMA *find_vma(uint64 addr) {
  struct VMA *vma = myproc()->vma;
  while (vma != 0) {
    if(vma->in_use && vma->addr < addr < vma->addr + vma->length){
      // printf("find_vma : need to find addr :%p\n",addr);
      // printf("vma addr : %p, vma size : %d, vma end: %p\n", vma->addr,vma->length,vma->addr + vma->length);
      break;
      // found
    }
    vma = vma->next;
  }
  printf("--find_vma : found %p\n",vma);
  return vma;
}

int mmap_write(struct file *f, uint64 addr, uint64 n ) 
{
  int r;
  int i = 0;
  int max = ((MAXOPBLOCKS -1 -1 -2)/2) * BSIZE;
  printf("Starting mmap_write: addr = %p, n = %d\n", addr, n);

while (i < n) {
  printf("Iteration start: i = %d, remaining = %d\n", i, n - i);

  int n1 = n - i > max ? max : n - i;
  printf("Calculated chunk size: n1 = %d\n", n1);

  begin_op();
  ilock(f->ip);

  if (f->ip == 0) {
    printf("Error: invalid inode pointer.\n");
    iunlock(f->ip);
    end_op();
    return -1;
  }

  r = writei(f->ip, 1, addr + i, i, n1);
  printf("writei result: r = %d\n", r);

  iunlock(f->ip);
  end_op();

  if (r != n1) {
    printf("Error: writei returned %d, expected %d.\n", r, n1);
    break;
  }

  i += r;
}

printf("mmap_write completed: total written = %d, expected = %d\n", i, n);
return i == n ? n : -1;
}
