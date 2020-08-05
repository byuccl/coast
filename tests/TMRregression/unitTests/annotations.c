/*
 * annotations.c
 * This file created to test how COAST treats local variables that have been
 *  annotated to be xMR'd.
 * The most important requirement is that it actually syncs on the values.
 * We add in some dynamically allocated structs as examples of things we
 *  wouldn't want to xMR
 * Also test if we can turn on/off the protection of specific function arguments.
 */

#include <stdio.h>
#include <stdlib.h>
#include "../../COAST.h"
__DEFAULT_NO_xMR

//struct for holding data
typedef struct _data_struct {
    int d0;
    int d1;
} data_t;


// arbitrary math operations
int doMath(data_t* d, int a) {
    int __xMR x, y;
    int __xMR result;
    x = d->d0 * a;
    y = d->d1 / a;
    result = ((x + y) >> 2) | 0x01;
    return result;
}

int moreMath(int a, int b) __xMR {
    int __NO_xMR p = 2;
    int q = 4;
    return (a * p) + (b << q);
}

void halfProtected(int* a, int* __NO_xMR b) __xMR {
    int* local_a = a;
    int* local_b = b;
}

void halfNotProtected(int* a, int* __xMR b) {
    int* local_a = a;
    int* local_b = b;
}

void callHalfFunctions() __xMR {
    int a = 2;
    int b = 3;
    halfProtected(&a, &b);
    halfNotProtected(&a, &b);
}


int main() {
    int __xMR result;
    int status = 0;

    data_t* myData = (data_t*) malloc(sizeof(data_t));
    myData->d0 = 21;
    myData->d1 = 47;
    //expected result: 17

    result = doMath(myData, 2);
    printf("Result = %d\n", result);
    if (result != 17) {
        printf("Error!\n");
        status |= -1;
    }

    //expected result: 52
    result = moreMath(2, 3);
    printf("Result = %d\n", result);
    if (result != 52) {
        printf("Error!\n");
        status |= -1;
    }

    // this doesn't do anything
    callHalfFunctions();

    return status;
}
