/*
 * atomics.c
 *
 * Test atomic operations and the effect of COAST SoR crossings on them.
 * requires -std=c11
 */

#include <stdio.h>
#include <stdatomic.h>

#include "../../COAST.h"
__DEFAULT_NO_xMR


void incAtomic(atomic_uint* at) __xMR {
    atomic_fetch_add(at, 1);
}

int main() {
    atomic_uint counter;
    atomic_init(&counter, 1);

    incAtomic(&counter);

    printf("counter = %d\n", counter);
}
