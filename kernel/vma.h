#define VMA_SIZE 16
struct VMA *alloc_vma();


// VMA
struct VMA{
  uint64 addr;  // start addr of vma
  int   length;
  int   prot;
  int   flags;
  int   fd;
  struct file * f;
  int   offset;
  struct VMA * next;
  int   in_use;
  uint64 addr_end;
};


// vmalist 
typedef struct {  
  struct spinlock lock;
  struct VMA vmas[VMA_SIZE];
} VMAlist;
