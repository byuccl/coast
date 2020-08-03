/*
 * cloneAfterCall.c
 * This benchmark was created to test replicating arguments to functions
 *  which are modified by the function, and are replicated afterwards,
 *  because we can only call the function once.
 * An example of this would be `scanf`.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "COAST.h"


// more difficult test that exercises parameter-specific annotations
#define SCANF_TEST


// This function has similar behavior to scanf, but has only 1 argument
//  to worry about
__xMR_ALL_AFTER_CALL
void simpleVoidFunc(uint32_t* modPtr) {
    *modPtr = 42;
}


int simpleTest() {
    uint32_t modifyMe = 0;
    simpleVoidFunc(&modifyMe);

    if (modifyMe != 42) {
        printf("Wrong modify value %d!\n", modifyMe);
        return 1;
    }

    return 0;
}


#ifdef SCANF_TEST
// specifically clone-after-call arguments 2, 3, and 4 (0 indexed)
int __xMR_AFTER_CALL(sscanf, 2_3_4)(const char * s, const char * format, ...);

int scanfTest() {
    int ret = 0;
    int expected = 3;

    // name, age, grade
    const char* inputStr = "Bob 16 3.7";

    char nameBuf[16];
    uint32_t age;
    float grade;

    // read input
    ret = __xMR_AFTER_CALL(sscanf, 2_3_4)(inputStr, "%s %d %f", nameBuf, &age, &grade);

    if (ret != expected) {
        printf("Error, return value %d\n", ret);
        return ret;
    }

    printf("%s (%d): %f\n", nameBuf, age, grade);
    return 0;
}
#endif


int main() {
    int ret = 0;

    // simple test
    ret |= simpleTest();

    #ifdef SCANF_TEST
    // more complex test
    ret |= scanfTest();
    #endif

    // validate
    if (ret) {
        printf("Error: %d\n", ret);
    } else {
        printf("Success!\n");
    }

    return ret;
}


// custom error handler
void FAULT_DETECTED_DWC() {
    printf("Error, fault detected!\n");
    exit(1);
}
