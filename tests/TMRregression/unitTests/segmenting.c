/*
 * segmenting.c
 * Are the basic blocks segmented correctly? This test particularly includes reordering
 *  of function calls that have been marked to be replicated
 * run with -replicateFnCalls=simpleMath
 */

#include <stdio.h>
#include <stdlib.h>
#include "../../COAST.h"


int simpleMath(int x, int y) {
    return x + y;
}


int main() {
    int a = 10;
    int b = 20;

    int result = simpleMath(a, b);

    if (result != 30) {
        printf("Error! %d\n", result);
    } else {
        printf("Success!\n");
    }
}