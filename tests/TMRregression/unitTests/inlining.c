/*
 * inlining.c
 * Unit test to see what COAST can do with function inlining, and the problems
 *  it can cause when combined with Replication scope and memory aliasing.
 * Compile with -O2
 *
 * To try waiting to inline until after COAST has run, do
 *  XCFLAGS=-O2 -Xclang -disable-llvm-passes -Rpass=inline
 *  OPT_PASSES=-O2 -disable-inlining -pass-remarks=inline -DWC -verbose -inline
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "../../COAST.h"
__DEFAULT_NO_xMR


// simulates system registers
static uint32_t fakeOutput[8];
static uint32_t idx = 0;

void globalWrite(uint32_t x) {
    fakeOutput[idx++] = x;
}


uint32_t __attribute__((noinline))
replicateThis(uint32_t a, uint32_t b) __xMR {
    uint32_t y = (a + b) << 1;  //random math
    globalWrite(y);
    return y;
}

uint32_t leaveThisAlone(uint32_t c, uint32_t d) __NO_xMR {
    uint32_t z = (c - d) ^ (uint32_t)0x0F;
    globalWrite(z);
    return z;
}


int main() {
    uint32_t y = replicateThis(2, 3);   //expect 10
    uint32_t z = leaveThisAlone(4, 1);  //expect 12

    printf("%d, %d\n", y, z);

    if ( (y != 10) || (z != 12) ) {
        printf("error!\n");
        return -1;
    }

    //read output so doesn't get optimized out
    for (uint32_t i = 0; i < 8; i+=1) {
        printf("%d, ", fakeOutput[i]);
    }
    printf("\n");

    return 0;
}
