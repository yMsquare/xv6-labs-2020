#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "spinlock.h"
#include "proc.h"
#include "e1000_dev.h"
#include "net.h"

void net_rx(struct mbuf *m);
#define TX_RING_SIZE 16
static struct tx_desc tx_ring[TX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *tx_mbufs[TX_RING_SIZE];

#define RX_RING_SIZE 16
static struct rx_desc rx_ring[RX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *rx_mbufs[RX_RING_SIZE];

// remember where the e1000's registers live.
static volatile uint32 *regs;

struct spinlock e1000_lock;

// called by pci_init().
// xregs is the memory address at which the
// e1000's registers are mapped.
void
e1000_init(uint32 *xregs)
{
  int i;

  initlock(&e1000_lock, "e1000");

  regs = xregs;

  // Reset the device
  regs[E1000_IMS] = 0; // disable interrupts
  regs[E1000_CTL] |= E1000_CTL_RST;
  regs[E1000_IMS] = 0; // redisable interrupts
  __sync_synchronize();

  // [E1000 14.5] Transmit initialization
  memset(tx_ring, 0, sizeof(tx_ring));
  for (i = 0; i < TX_RING_SIZE; i++) {
    tx_ring[i].status = E1000_TXD_STAT_DD;
    tx_mbufs[i] = 0;
  }
  regs[E1000_TDBAL] = (uint64) tx_ring;
  if(sizeof(tx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_TDLEN] = sizeof(tx_ring);
  regs[E1000_TDH] = regs[E1000_TDT] = 0;
  
  // [E1000 14.4] Receive initialization
  memset(rx_ring, 0, sizeof(rx_ring));
  for (i = 0; i < RX_RING_SIZE; i++) {
    rx_mbufs[i] = mbufalloc(0);
    if (!rx_mbufs[i])
      panic("e1000");
    rx_ring[i].addr = (uint64) rx_mbufs[i]->head;
  }
  regs[E1000_RDBAL] = (uint64) rx_ring;
  if(sizeof(rx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_RDH] = 0;
  regs[E1000_RDT] = RX_RING_SIZE - 1;
  regs[E1000_RDLEN] = sizeof(rx_ring);

  // filter by qemu's MAC address, 52:54:00:12:34:56
  regs[E1000_RA] = 0x12005452;
  regs[E1000_RA+1] = 0x5634 | (1<<31);
  // multicast table
  for (int i = 0; i < 4096/32; i++)
    regs[E1000_MTA + i] = 0;

  // transmitter control bits.
  regs[E1000_TCTL] = E1000_TCTL_EN |  // enable
    E1000_TCTL_PSP |                  // pad short packets
    (0x10 << E1000_TCTL_CT_SHIFT) |   // collision stuff
    (0x40 << E1000_TCTL_COLD_SHIFT);
  regs[E1000_TIPG] = 10 | (8<<10) | (6<<20); // inter-pkt gap

  // receiver control bits.
  regs[E1000_RCTL] = E1000_RCTL_EN | // enable receiver
    E1000_RCTL_BAM |                 // enable broadcast
    E1000_RCTL_SZ_2048 |             // 2048-byte rx buffers
    E1000_RCTL_SECRC;                // strip CRC
  
  // ask e1000 for receive interrupts.
  regs[E1000_RDTR] = 0; // interrupt after every received packet (no timer)
  regs[E1000_RADV] = 0; // interrupt after every packet (no timer)
  regs[E1000_IMS] = (1 << 7); // RXDW -- Receiver Descriptor Write Back
}

int
e1000_transmit(struct mbuf *m)
{
  // printf("e1000_transmit!\n");
  //
  // Your code here.
  //
  // the mbuf contains an ethernet frame; program it into
  // the TX descriptor ring so that the e1000 sends it. Stash
  // a pointer so that it can be freed after sending.
  //
  acquire(&e1000_lock); // 加锁，保护共享数据结构
  int td_index = regs[E1000_TDT];
  // 暂存这个传入的mbuf
  struct tx_desc * td_ptr;
  // 读取 TDT 寄存器,获得tx_descriptor的尾部指针

  td_ptr = &tx_ring[td_index];
  // printf("td_ptr : %d, %p\n", regs[E1000_TDT],td_ptr);
  if(!td_ptr){
    // printf("null td_ptr!\n");
    return -1;

  }  
  if(!(td_ptr->status & E1000_TXD_STAT_DD)){
    // printf("not finished yet");
    return -1;
  }
  if(tx_mbufs[td_index]){
    mbuffree(tx_mbufs[td_index]);
    tx_mbufs[td_index] = 0;
    // printf("freed mbuf\n");
  }
  // 把一个descriptor的addr写成这个mbuf
  td_ptr->addr = (uint64)m->head;
  td_ptr->length = m->len;
  td_ptr->cmd = 0b10001001;
  // printf("data: %s,\n", td_ptr->addr);
  // printf("len :%d \n", td_ptr->length);

  tx_mbufs[td_index] = m;

  regs[E1000_TDT]  = (regs[E1000_TDT]  + 1) % TX_RING_SIZE;
  // printf("change regs to %d\n", regs[E1000_TDT]);
  release(&e1000_lock); // 解锁
  return 0;
}

static void
e1000_recv(void)
{
  // printf("\ne1000_recv!\n");
  //
  // Your code here.
  //
  // Check for packets that have arrived from the e1000
  // Create and deliver an mbuf for each packet (using net_rx()).
  //
  struct rx_desc * rd_ptr;
  int next_rindex;
  while(1){
    int next_rindex = (regs[E1000_RDT] + 1) % RX_RING_SIZE;   
    rd_ptr = &rx_ring[next_rindex];
    if (!(rd_ptr->status & E1000_RXD_STAT_DD)) {
      // printf("not finished yet\n");
      break;
    }
    struct mbuf * m = rx_mbufs[next_rindex];
    if(!m){
      // printf("not enought mbufs\n");
      break;
    }
    m->len = rd_ptr->length;
    net_rx(m);
    // printf("sent to net_rx\n");

    struct mbuf * new_mbuf = mbufalloc(0);
    if(!new_mbuf){
      printf("allocate new mbuf failed\n");
      panic("mbuf");
    }

    rd_ptr->addr = (uint64)new_mbuf->head;
    rd_ptr->length = 0;
    rd_ptr->status = 0;

    rx_mbufs[next_rindex] = new_mbuf;
    regs[E1000_RDT] = next_rindex;
  }
}

void
e1000_intr(void)
{
  // tell the e1000 we've seen this interrupt;
  // without this the e1000 won't raise any
  // further interrupts.
  regs[E1000_ICR] = 0xffffffff;

  e1000_recv();
}
