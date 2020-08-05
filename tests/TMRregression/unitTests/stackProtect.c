/*
 * stackProtect.c
 * This unit test created to test new COAST ability to protect stack
 *  from corruption of return address and previous stack pointer value.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// using this file to figure out the correct signatures for the
//  builtin functions
// #define FIGURE_OUT_FUNCTION_INFO
#ifdef FIGURE_OUT_FUNCTION_INFO

// stringification macros
// https://stackoverflow.com/a/47346160/12940429
#define __STRINGIFY(x) #x
#define STRINGIFY(x) __STRINGIFY(x)

// silence annoying compiler warnings about length specifiers on printing pointers
#ifdef __x86_64__
#define PTR_FMT lX
#else
#define PTR_FMT X
#endif

// globals
uintptr_t glblRet = 0;
uintptr_t glblFrmPtr = 0;
#endif


int call1(int b) {
    return b + 1;
}


int call0(int a) {
    return call1(a);
}


int main() {
    #ifdef FIGURE_OUT_FUNCTION_INFO
    uintptr_t ret = (uintptr_t)__builtin_return_address(0);
    uintptr_t fp  = (uintptr_t)__builtin_frame_address(0);
    glblRet = ret;
    glblFrmPtr = fp;

    printf("ret: 0x%08" STRINGIFY(PTR_FMT) ", fp: 0x%08" STRINGIFY(PTR_FMT) "\n",
            ret, fp);
    #endif

    int res = call0(2);
    // expected result: res = 3
    if (res != 3) {
        printf("Error! %d\n", res);
        return 3;
    } else {
        printf("Success!\n");
    }

    return 0;
}

void FAULT_DETECTED_DWC() {
    printf("corrupted return address\r\n");
    abort();
}
