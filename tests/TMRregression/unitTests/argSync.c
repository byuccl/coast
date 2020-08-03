/*
 * In processCallSync(), assert for number of users of original in function call
 * is it OK to have more than 2 uses if it's an argument?
 */

#include <stdio.h>
#include <stdlib.h>

#include "../../COAST.h"
__DEFAULT_NO_xMR

// borrowed from FFT benchmark in MiBench
// run with -O3, but not inlined
unsigned __attribute((noinline)) NumberOfBitsNeeded ( unsigned PowerOfTwo ) __xMR
{
    unsigned i;

    if ( PowerOfTwo < 2 )
    {
        fprintf (
            stderr,
            ">>> Error in fftmisc.c: argument %d to NumberOfBitsNeeded is too small.\n",
            PowerOfTwo );

        exit(1);
    }

    for ( i=0; ; i++ )
    {
        if ( PowerOfTwo & (1 << i) )
            return i;
    }
}

int runTest(int a) __xMR {
    int x = NumberOfBitsNeeded(a);
    return x;
}


int main() {
    int x = runTest(32);
    // expected value: 5

    if (x == 5) {
        printf("Success!\n");
    } else {
        printf("Error: %d\n", x);
        return x;
    }

    return 0;
}
