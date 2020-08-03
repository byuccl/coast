/*
 * atomics.c
 *
 * Test atomic operations and the effect of COAST SoR crossings on them.
 * requires -std=c11
 *
 * Does not work with -noMemReplication flag, because it tries to sync on
 *  the atomics, but the values are after each atomic call.
 */

#include <stdio.h>
#include <stdatomic.h>

#include "../../COAST.h"
__DEFAULT_NO_xMR


void incAtomic(atomic_uint* at) __xMR {
    atomic_fetch_add(at, 1);
}

int main() {
    // initialize an atomic counter to have value = 1
    atomic_uint counter;
    atomic_init(&counter, 1);

    // add 1 to the number
    incAtomic(&counter);

    // normal, expect result to be = 2
    // with DWC, expect counter = 3
    // with TMR, expect counter = 4

    printf("counter = %d\n", counter);

    return 0;
}
