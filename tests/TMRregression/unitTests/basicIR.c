/*
 * basicIR.c
 * This benchmark designed to create basic LLVM IR that is easy to check if
 *  all of the replication rules are being followed correctly
 */

#include <stdio.h>

int globalArr[] = {0, 0};

int main() {
    //load
    int* xp = &globalArr[0];
    xp+=1;
    int x = *xp;

    //ops
    x = ((x + 5) * 3) >> 1;

    //store
    globalArr[0] = x;

    //expected output: 7
    printf("Result: %d\n", globalArr[0]);

    if (globalArr[0] != 7) {
        printf("Error!\n");
        return -1;
    } else {
        return 0;
    }
}
