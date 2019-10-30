/*
 * verifyOptions.c
 * 
 * Unit test to see if COAST can detect when replication rules
 * are being violated.
 */

#include <stdio.h>
#include "../../COAST.h"
__DEFAULT_NO_xMR


int __xMR myGlobal = 0;


void incGlbl(void) __xMR {
    myGlobal++;
}

void decGlbl(void) __NO_xMR {
    myGlobal--;
}

__COAST_IGNORE_GLOBAL(myGlobal)
void mulGlbl(void) {
    myGlobal *= 2;
}


int main() {

    printf("%d, ", myGlobal);
    incGlbl();
    printf("%d, ", myGlobal);
    decGlbl();
    printf("%d\n", myGlobal);

    return 0;
}
