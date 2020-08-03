/*
 * mallocTest.c
 *
 * This unit test is to make sure malloc works with TMR
 * malloc() wrappers may need special treatment
 *  must include `-replicateFnCalls=alloc_struct ` after -TMR,
 *  or include the annotations as shown below
 *
 * Also, to make sure that all pointers are free'd, wrap up
 *  `free` with the directive.
 */

/********************************** Includes **********************************/
#include <stdio.h>
#include <stdlib.h>


/**************************** COAST configuration *****************************/
#include "COAST.h"

void GENERIC_COAST_WRAPPER(free)(void* ptr);


/******************************** Definitions *********************************/
#define ARRAY_SIZE  4
#define ELEMENT_SIZE    10

typedef struct {
    float a;
    float b;
} inner_struct;

typedef struct {
    int x;
    int y;
    int array[ARRAY_SIZE];
    inner_struct z;
} outer_struct;


/********************************* Functions **********************************/

__xMR_FN_CALL
outer_struct* alloc_struct(void) {
    outer_struct* st = (outer_struct*) malloc(sizeof(outer_struct));
    return st;
}

int main() {
    // don't even need to do anything with the struct, just create it and destroy it
    outer_struct* st = alloc_struct();
    GENERIC_COAST_WRAPPER(free)(st);

    printf("Finished\n");
    // This unit test considered to have succeeded
    //  if there are no memory leaks (double free corruption, etc)
    return 0;
}
