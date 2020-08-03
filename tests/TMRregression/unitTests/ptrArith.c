/*
 * ptrArith.c
 *
 * pointer arithmetic. Is it safe?
 */

#include <stdio.h>
#include <string.h>

#include "../../COAST.h"
__DEFAULT_NO_xMR


/***************************** utility functions ******************************/
void print1dArray(int a[], int size) {
    for (int i = 0; i < size; i+=1) {
        printf("%2d ", a[i]);
    }
    puts("");
}

void print2dArray(int* a[], int rows, int cols) {
    for (int i = 0; i < rows; i+=1) {
        for (int j = 0; j < cols; j+=1) {
            printf("%2d @ %p\n", a[i][j], &a[i][j]);
        }
    }
}

/******************************** mutate array ********************************/
// do some pointer arithmetic
void mutateArray(int* ar) __xMR {
    int* a7 = &ar[7];
    // ar[7] = 2;
    *ar = 1;            //index 0
    *(ar + 1) = 2;      //index 1
    ar += 3;
    *ar += 4;           //index 3
    ar[2] -= 1;         //index 5
    *(ar + 3) = 7;      //index 6
    *a7 |= (ar[0] >> ar[1]) - ar[2];           //index 7
    // *a7 ^= 1;
}

/********************************** xor swap **********************************/
void xorSwap(int* x, int* y) __xMR {
    if (x != y) {
        *x ^= *y;
        *y ^= *x;
        *x ^= *y;
    }
}

/******************************** pointer math ********************************/
void doPtrMath(int* ptr0, int* ptr1) __xMR {
    ptr0[0] = ptr0[2] ^ ptr1[7];    //yields 2
    ptr0++;
    ptr0[1] += ptr1[4];             //yields 7
}

/***************************** pointer increment ******************************/
void ptrInc(int* p) __xMR {
    (*p)++;
}

/****************************** double pointers *******************************/
void ptr2d(int** p) __xMR {
    *(*(p + 1)) = 42;
    p++;
    *(*p - 1) <<= 2;
}

/****************************** double crossing *******************************/
void fakeLibFunc(int* p) {
    (*p) &= 0xFF;
}

void doubleCross(int* p) __xMR {
    fakeLibFunc(p);
    (*p) += 1;
}

/****************************** storing pointers ******************************/
void doNothing(int* p) {
    int* q = p + 1;
}

void storePtr(int** pp, int* p) __xMR {
    p = *pp;
    p += 1;      // this makes it a GEP instead
    doNothing(p);
    *p = 7;
}

/***************************** function pointers ******************************/
//typedef function pointer
typedef void (*MyFnType)(int* arg0);

void intMath(int* arg0) {
    *arg0 += 2;
}

void callFnPtr(int* x, MyFnType fnPtr) __xMR {
    (*fnPtr)(x);
}


/************************************ main ************************************/
#define ASIZE 8
int main() {
    int status = 0;

    ///////////////////////////// mutate array /////////////////////////////
    int a1[ASIZE];
    int a2[ASIZE] = {1, 2, 0, 4, 0, -1, 7, 5};   //golden
    memset(a1, 0, ASIZE * sizeof(int));

    mutateArray(a1);

    print1dArray(a1, ASIZE);

    if (memcmp(a1, a2, ASIZE * sizeof(int))) {
        printf(" !! Error !!\n");
        status += 1;
    }

    /////////////////////////////// xor swap ///////////////////////////////
    int x = 0x55;
    int y = 0xFA;
    xorSwap(&x, &y);
    printf(" 0x%02X, 0x%02X\n", x, y);

    ///////////////////////////// pointer math /////////////////////////////
    int array0[ASIZE] = {1, 2, 3, 4, 5, 6, 7, 8};
    int array1[ASIZE] = {8, 7, 6, 5, 4, 3, 2, 1};
    doPtrMath(array0, array1);
    //expected array0 = {2, 2, 7}

    print1dArray(array0, ASIZE);

    ////////////////////////// pointer increment ///////////////////////////
    int incThis = 2;
    ptrInc(&incThis);
    printf(" %d\n", incThis);
    if (incThis != 3) {
        puts("Error!");
        status += 1;
    }

    /////////////////////////// double pointers ////////////////////////////
    int a3[2] = {1, 2};
    int a4[2] = {3, 4};
    int* square[2] = {a3, a4};
    print2dArray(square, 2, 2);

    int** sp = &square[0];
    printf("%p, %p\n", sp, *sp);
    ptr2d(&square[0]);

    //expects { {1, 8}, {42, 4} }
    print2dArray(square, 2, 2);

    /////////////////////////// double crossing ////////////////////////////
    int val = 0xAAAA;
    doubleCross(&val);
    //expected 0x00AB
    printf(" 0x%04X\n", val);
    if (val != 0x00AB) {
        status += 1;
    }

    /////////////////////////// storing pointers ///////////////////////////
    int sp0[4] = {4, 8, 16, 32};
    int* spp0 = &sp0[0];
    storePtr(&spp0, &sp0[3]);
    print1dArray(sp0, 4);

    ////////////////////////// function pointers ///////////////////////////
    int fnX = 3;
    callFnPtr(&fnX, &intMath);
    printf("%2d\n", fnX);
    //expected 5
    // TODO: call gets replicated
    // if (fnX != 5) {
    //     status += 1;
    // }

    ///////////////////////////// Status Check /////////////////////////////
    if (status) {
        printf("Error: %d\n", status);
    } else {
        printf("Success!\n");
    }

    return status;
}
