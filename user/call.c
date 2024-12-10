#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int g(int x) {
  return x+3;
}

int f(int x) {
  return g(x);
}

void main(void) {
  printf("%d %d\n", f(8)+1, 13);
  unsigned int i = 0x00646c72;
	printf("H%x Wo%s", 57616, &i); //  HE110 World , 57616 = 0xE110, i = 00646c72 = rld
  //   0110 0100 0110 1100 0111 0010， (64)(6c)(72) in hex 
  // little-endian : 0111 0010 0110 1100 0110 0100 (72)(6c)(64) in hex : 'r' 'l' 'd'
  printf("x=%d y=%d", 3); // y = 1
   // %: 00100101 0x25, d : 01100100 0x64 , 1:00110001 
  exit(0);
}
