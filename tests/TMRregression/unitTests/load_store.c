/*
 * load_store.c
 * This benchmark designed to create LLVM IR that shows how storing
 *  the first copy before loading the second can create errors. Run
 *  with -DWC.
 * Don't compile with any optimizations, or it will compile to only
 *  print the first string.
 */

#include <stdio.h>
#include <stdint.h>

#include "../../COAST.h"
__DEFAULT_NO_xMR

// #define EXECUTE_CORRECT
#ifdef EXECUTE_CORRECT
#define FUNCTION_TAG
#else
#define FUNCTION_TAG __xMR
#endif

struct myStruct
{
    uint32_t x;
    uint32_t y;
};


void touchStruct(struct myStruct* ms) FUNCTION_TAG {
    (ms->x)++;

    if ( (ms->x) == 1) {
        printf("ms == 1\r\n");
    } else {
        printf("ms == %d\r\n", ms->x);
    }

    return;
}


int main() {
    struct myStruct ms;
    ms.x = 0;
    ms.y = 0;
    touchStruct(&ms);
}
