/*
 * funcPtrStruct.c
 *
 * This unit test is designed to ensure that COAST properly detects function
 *  pointers being used as elements in a struct
 * Essentially, detect when a function is used as an input to an assignment
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "../../COAST.h"


                        //////// type definitions ////////
//function signature we will be using
typedef void (*myFuncSignature) (void *CallBackRef, uint32_t StatusEvent);

//this struct will have a function pointer in it
typedef struct _test_struct {
    int x;
    int y;
    myFuncSignature StatusHandler;  /* Event handler function */
} test_struct;

//this struct will just have data
typedef struct _data_struct {
    int a;
    int b;
} data_struct;

                        //////// function definitions ////////
// this function will be assigned as a value
static void StubHandler(void *CallBackRef, uint32_t StatusEvent) {
    // cast
    data_struct* dst = (data_struct*)CallBackRef;
    // print values
    printf("%d %d %d\n", dst->a, dst->b, StatusEvent);
}

// this function allocates a struct
test_struct* __xMR_FN_CALL alloc_struct(){
    test_struct* st = (test_struct*) malloc(sizeof(test_struct));
    return st;
}

int main(){
    // set up the structs
    test_struct* st = alloc_struct();
    data_struct dst = {1, 2};

    st->x = 100;
    st->y = 150;
    st->StatusHandler = StubHandler;

    // print some data about the structs
    printf("%d %d\n", st->x, st->y);
    st->StatusHandler(&dst, 3);
    // COAST will replicate these calls even though they are indirect.
    // If you want it only called once, have to create an intermediate wrapper function.

    // cleanup
    free(st);
    printf("Finished\n");
}
