/*
 * globalPointers.c
 * 
 * This unit test is designed to show one of the difficulties
 *  encountered when protecting the FreeRTOS kernel.
 * How do global pointers crossing the Sphere of Replication
 *  cause incorrect execution results?
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// COAST configuration
#include "../../COAST.h"
__DEFAULT_NO_xMR


// global value that exists as a pointer
uint32_t* glblPtr;


/****************************** Helper Functions ******************************/

/*
 * Function for printing out the value of a pointer and its clones.
 */
void printPtrVal(uint32_t* ptr) __xMR_FN_CALL {
    printf("%14p: %d\n", ptr, *ptr);
}


/************************** Replicate Function Calls **************************/

/* 
 * Dereferences a pointer and increments the value.
 * By the time any pointer gets here, we don't know
 *  if it was a local or global.
 * This function is called multiple times.
 */
void incPtrValCoarse(uint32_t* ptr)  __xMR_FN_CALL {
    *ptr += 1;
}


void ptrCmp0() __xMR {
    // create a local variable that will be xMR'd
    uint32_t localVar = 0;

    // increment each pointer
    incPtrValCoarse(&localVar);
    incPtrValCoarse(glblPtr);

    // print out what the values are
    printPtrVal(&localVar);
    printPtrVal(glblPtr);

    return;
}


/***************************** Change Signatures ******************************/

void incPtrValFine(uint32_t* ptr) __xMR {
    *ptr += 1;
}

/*
 * This way of accessing a global pointer works fine.
 * However, this is after we implemented store segmenting.  This makes sure all
 *  3 versions of the arithmetic data streams are operating on the same
 *  initial value of the data from the pointer.
 * Maybe that wasn't the right solution.
 */
void ptrCmp1() __xMR {
    // create a local variable that will be xMR'd
    uint32_t localVar = 0;

    // we pass both the local and global into the function, 
    //  parameters replicated
    incPtrValFine(&localVar);
    incPtrValFine(glblPtr);

    // print out what the values are
    printPtrVal(&localVar);
    printPtrVal(glblPtr);

    return;
}

/******************************* Global Struct ********************************/

typedef struct test_s {
    uint32_t x;
    uint32_t* y;
} test_t;

test_t globalStruct;

/*
 * Uses the address of a global struct pointer
 */
void ptrCmp2() __xMR {
    // get pointer to global
    uint32_t* ptr = &(globalStruct.x);
    printPtrVal(ptr);

    // init local struct
    test_t localStruct;
    localStruct.x = 0;
    localStruct.y = NULL;
}


/******************************* Global uint32 ********************************/

uint32_t globalInteger;

/*
 * Uses the address of a global struct pointer
 */
void ptrCmp3() __xMR {
    // get pointer to global
    uint32_t val = globalInteger;
    val += 3;

    uint32_t* localPtr = &globalInteger;
    *localPtr += 1;

    printPtrVal(&val);
    printPtrVal(&globalInteger);
}


/************************************ Main ************************************/

int main() {
    // init the global pointer
    glblPtr = (uint32_t*) malloc(sizeof(uint32_t));
    *glblPtr = 0;

    // ptrCmp0();
    // ptrCmp1();

    memset(&globalStruct, 0, sizeof(test_t));
    // ptrCmp2();

    globalInteger = 42;
    ptrCmp3();

    return 0;
}


/*
 * Expected output (fails in compilation):
 * ERROR: unprotected global "globalInteger" is being read from and written to inside protected functions:
 *  "ptrCmp3" at unitTests/globalPointers.c:128:15,
 * ERROR: unprotected global "glblPtr" is being read from and written to inside protected functions:
 *  "incPtrValFine" at     store i32* %ptr, i32** %ptr.addr, align 8,
 * ERROR: unprotected global "globalStruct" is being read from and written to inside protected functions:
 *  "ptrCmp2" at unitTests/globalPointers.c:106:15,
 */
