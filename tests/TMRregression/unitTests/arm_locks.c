/*
 * arm_locks.c
 *
 * This is to test the synchronization primitives from the ARM ISA.
 */

#include <stdio.h>
#include <arm_acle.h>


/**************************** COAST configuration *****************************/
#include "../../COAST.h"
__DEFAULT_NO_xMR


#ifdef __arm
void swap(unsigned int* a, unsigned int* b) __xMR {
    *a = __swp(*a, (unsigned int*)b);
}
#endif


int main() {
    unsigned int x = 0x55;
    unsigned int y = 0xAA;
    swap(&x, &y);

    printf("x = 0x%02X, y = 0x%02X\r\n", x, y);
}
