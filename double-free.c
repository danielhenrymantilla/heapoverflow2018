#include <stdlib.h>
#include <stdio.h>

int main (){
 char *a = malloc(8);
 char *b = malloc(8);

 free(a);
 free(b);
 free(a);

 fprintf(stderr, "1st malloc %p\n", malloc(8));
 fprintf(stderr, "2nd malloc %p\n", malloc(8));
 fprintf(stderr, "3rd malloc %p\n", malloc(8));
}
