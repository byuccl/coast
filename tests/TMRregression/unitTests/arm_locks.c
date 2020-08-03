/*
 * arm_locks.c
 *
 * This is to test the synchronization primitives from the ARM ISA.
 * TODO: add more primitives
 *
 * __swp
 * "swap data between registers and memory"
 * https://developer.arm.com/documentation/dui0472/m/compiler-specific-features/--swp-intrinsic
 */

#ifndef __arm
#error This unit test only works with ARM targets
#endif


/********************************** Includes **********************************/
#include <stdio.h>
#include <arm_acle.h>


/**************************** COAST configuration *****************************/
#include "../../COAST.h"
__DEFAULT_NO_xMR


/********************************* Functions **********************************/
// wrap intrinsic
void swap(unsigned int* a, unsigned int* b) __xMR {
    *a = __swp(*a, (unsigned int*)b);
}


int main() {
    unsigned int x = 0x55;
    unsigned int y = 0xAA;
    swap(&x, &y);

    printf("x = 0x%02X, y = 0x%02X\r\n", x, y);
}
