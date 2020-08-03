/*
 * protectedLib.c
 *
 * This unit test was created to show what can happen when a "library" style
 *  function is used inside and outside the scope of replication, espcecially
 *  when it also accesses a protected global variable.
 *
 * Is it possible to make all the calls to this function have the same signature?
 * Then it would be easier to call in- and outside the scope of replication.
 *
 * Without changing the COAST code, this would only be possible with 2 separate
 *  compilation units, and the first one run with -noMain flag
 *
 * We have succeeded:
 * Run with the command line parameter -protectedLibFn=sharedFunc
 */

#include <stdint.h>
#include <stdio.h>

#include "COAST.h"


// protected global
static uint32_t __xMR protectedUInt = 0;


/*
 * This function called from in- and out-side the scope of replication.
 * It modifies a global variable.
 */
void sharedFunc(uint32_t* ptr, uint32_t val) {
    protectedUInt += val;
    *ptr = protectedUInt;
}


int protectedWrapper() __xMR {
    int status = 0;
    // can't protect this because modified by reference
    // might be able to do it with cloneAfterCall
    uint32_t __NO_xMR a = 2;
    uint32_t b = 3;

    sharedFunc(&a, b);
    // expected result: a = proctedUInt+3 = 5+3 = 8

    status = (a != 8);
    return status;
}


int main() __NO_xMR {
    int status = 0;
    uint32_t a = 4;
    uint32_t b = 5;

    sharedFunc(&a, b);
    // expected output: a = 5
    status |= (a != 5);

    // call the protected one 2nd
    status |= protectedWrapper();

    if (status) {
        printf("Error: %d\n", status);
        return status;
    } else {
        printf("Success!\n");
    }

    return 0;
}