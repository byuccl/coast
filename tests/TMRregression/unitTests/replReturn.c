/*
 * replReturn.c
 *
 * This unit test is intended to test COAST support for returning
 *  multiple values from a function call.
 * Invocation should include
 *  `-cloneReturn=returnTest -replicateFnCalls=malloc -cloneFns=testWrapper`
 *
 * Don't run with -noMemReplication because that defeats the point of the unit test.
 */

#include <stdio.h>
#include <stdlib.h>

// COAST configuration
#include "../../COAST.h"
// __DEFAULT_NO_xMR

PRINTF_WRAPPER_REGISTER(printf);

// TODO: make a test that already returns an aggregate type


int* returnTest(size_t sz) {
    int* myPointer = (int*) malloc(sizeof(int) * sz);
    return myPointer;
}

int testWrapper() {
    // decide size
    size_t sz = 4;

    // create a pointer
    int* newPointer = returnTest(sz);
    PRINTF_WRAPPER_CALL(printf, "%p\n", newPointer);

    // use pointer
    newPointer[1] = 42;

    return !(newPointer[1] == 42);
}


int main() __NO_xMR {
    int status = testWrapper();

    if (status) {
        printf("Failure!\n");
        return -1;
    } else {
        printf("Success!\n");
        return 0;
    }
}
