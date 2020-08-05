/*
 * fibonacci.c
 *
 * Implementation of the fibonacci sequence using recursion.
 * This should allow for some interesting tests.
 */

#include <stdio.h>
#include <stdlib.h>

// COAST configuration
#include "../../COAST.h"

__DEFAULT_NO_xMR

PRINTF_WRAPPER_REGISTER(printf);


int fib(int n) __xMR {
    if (n <= 1) {
        return n;
    } else {
        return fib(n-1) + fib(n-2);
    }
}

int testWrapper() __xMR {
    // decide size
    size_t sz = 10;

    // calculate
    int res = fib(sz);

    PRINTF_WRAPPER_CALL(printf, "%d\n", res);

    return !(res == 55);
}


int main() {
    int status = testWrapper();

    if (status) {
        printf("Failure!\n");
        return -1;
    } else {
        printf("Success!\n");
        return 0;
    }
}
