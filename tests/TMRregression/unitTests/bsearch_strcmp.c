/*
 *  Got this program from
 *  http://www.cplusplus.com/reference/cstdlib/bsearch/
 *  it is a combination of both examples
 */

/* bsearch example */
#include <stdio.h>      /* printf */
#include <stdlib.h>     /* qsort, bsearch, NULL */
#include <string.h>     /* strcmp */

int __attribute__((annotate("no_xMR"))) compareints (const void * a, const void * b)
{
  return ( *(int*)a - *(int*)b );
}

int values[] = { 50, 20, 60, 40, 10, 30 };
char strvalues[][20] = {"some","example","strings","here"};

// returns true (1) if in the array, false (0) otherwise
int test1(){
    int * pItem;
    int key = 40;
    qsort (values, 6, sizeof (int), compareints);
    pItem = (int*) bsearch (&key, values, 6, sizeof (int), compareints);
    if (pItem!=NULL) {
        printf ("%d is in the array.\n",*pItem);
        return 1;
    }
    else {
        printf ("%d is not in the array.\n",key);
        return 0;
    }
}

// returns true (1) if in the array, false (0) otherwise
int test2(){
    char * pItem;
    char key[20] = "example";

    /* sort elements in array: */
    qsort (strvalues, 4, 20, (int(*)(const void*,const void*)) strcmp);

    /* search for the key: */
    pItem = (char*) bsearch (key, strvalues, 4, 20, (int(*)(const void*,const void*)) strcmp);

    if (pItem!=NULL) {
        printf ("%s is in the array.\n",pItem);
        return 1;
    }
    else {
        printf ("%s is not in the array.\n",key);
        return 0;
    }
}

int main ()
{
    int x0 = test1();
    int x1 = test2();
    //expect both to be true
    if (x0 && x1) {
        return 0;
    } else {
        printf("Error!\n");
        return -1;
    }
}
