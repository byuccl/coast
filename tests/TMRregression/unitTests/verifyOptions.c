/*
 * verifyOptions.c
 *
 * Unit test to see if COAST can detect when replication rules
 * are being violated.
 *
 * Also if COAST is treating "used" variables correctly.
 */

#include <stdio.h>
#include "../../COAST.h"
__DEFAULT_NO_xMR


// some things that are never used
int __attribute__((used)) unusedInt = 4;
char* __COAST_VOLATILE unusedString = "Hello there!";
char __attribute__((used)) unusedChar = 'c';


// globals used in and out of scope
int __xMR protectedGlobal = 0;
int normalGlobal = 0;


void incGlbl(void) __xMR {
    protectedGlobal++;
    normalGlobal++;
}

void decGlbl(void) __NO_xMR {
    protectedGlobal--;
    normalGlobal--;
}

void mulGlbl(void) {
    protectedGlobal *= 2;
    normalGlobal *= 2;
}


__COAST_IGNORE_GLOBAL(protectedGlobal)
int main() {

    printf("%d, ", protectedGlobal);
    incGlbl();
    printf("%d, ", protectedGlobal);
    decGlbl();
    printf("%d\n", protectedGlobal);

    return 0;
}
