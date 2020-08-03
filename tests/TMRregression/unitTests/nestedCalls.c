/*
 * nestedCalls.c
 * 
 * This unit test was created to explore issues with COAST that arise
 *  when there is a bitcast *and* GEP inside the same call instruction.
 * This seems to affect correctly changing the parameters of the 
 *  cloned function call in cases like memset, when it is a function
 *  whose call is replicated.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include "../../COAST.h"


/******************************** Definitions *********************************/

// define a struct with a block inside of it
typedef struct block_s {
    int a;
    int b;
    int block[16];
} block_t;

block_t globalBlock;

// define a struct with aliasing pointer and value types
// borrowed from FreeRTOS list.h
typedef uint32_t UBaseType_t;
struct xLIST_ITEM
{
	UBaseType_t xItemValue;
	struct xLIST_ITEM * pxNext;
	struct xLIST_ITEM * pxPrevious;
	void * pvOwner;
	void * pvContainer;
};
typedef struct xLIST_ITEM ListItem_t;

struct xMINI_LIST_ITEM
{
	UBaseType_t xItemValue;
	struct xLIST_ITEM * pxNext;
	struct xLIST_ITEM * pxPrevious;
};
typedef struct xMINI_LIST_ITEM MiniListItem_t;

typedef struct xLIST {
    UBaseType_t uxNumberOfItems;
    ListItem_t * pxIndex;
    MiniListItem_t xListEnd;
} List_t;

static List_t pxReadyTasksLists[ 4 ];


/********************************* Functions **********************************/

__attribute__((noinline))
void memset_test() {
    block_t myBlock;
    
    memset(myBlock.block, 0, sizeof(myBlock.block));
    memset(globalBlock.block, 0, sizeof(myBlock.block));

    // do something with it so it doesn't get optimized out
    myBlock.a = 42;

    // This will be checked later
    globalBlock.block[2] = myBlock.a + globalBlock.block[0];

    return;
}


void vListInitialise( List_t * const pxList )
{
	pxList->pxIndex = ( ListItem_t * ) &( pxList->xListEnd );

	pxList->xListEnd.xItemValue = ( UBaseType_t ) 0xffffffffUL;

	pxList->xListEnd.pxNext = ( ListItem_t * ) &( pxList->xListEnd );
	pxList->xListEnd.pxPrevious = ( ListItem_t * ) &( pxList->xListEnd );

	pxList->uxNumberOfItems = ( UBaseType_t ) 0U;
}

__attribute__((noinline))
void vListPrintInfo( List_t * const pxList, UBaseType_t uxPriority) __xMR_FN_CALL 
{
    printf("List priority %d: \n", uxPriority);
    printf("  uxNumberOfItems: %d\n", pxList->uxNumberOfItems);
    printf("  pxIndex @%p\n", pxList->pxIndex);
    printf("  xListEnd @%p\n", &pxList->xListEnd);
    printf("    xItemValue = %u\n", pxList->xListEnd.xItemValue);
}

/* Compiling this with -O2 causes the compiler to unroll the loop and inline
 * the initialization function.  Then each store will be all one LLVM IR
 * instruction, with a GEP inside of a bitcast, the thing that is tricky
 * to clone correctly. */
__attribute__((noinline))
void struct_test() {
    UBaseType_t uxPriority;
    for( uxPriority = 0U; uxPriority < 4; uxPriority++ )
	{
		vListInitialise( &( pxReadyTasksLists[ uxPriority ] ) );
	}

    // print them out
    // for ( uxPriority = 0U; uxPriority < 4; uxPriority++ ) {
    //     vListPrintInfo(&pxReadyTasksLists[uxPriority], uxPriority);
    // }
    int q = 57;
    pxReadyTasksLists[0].pxIndex->xItemValue = q;
}


int main() {
    int status = 0;

    memset_test();
    // expect globalBlock.block[2] = 42
    if (globalBlock.block[2] != 42) {
        status += 1;
    }

    struct_test();

    // expect pxReadyTasksLists[0].pxIndex->xItemValue = 57
    if (pxReadyTasksLists[0].pxIndex->xItemValue != 57) {
        status += 1;
    }

    if (status) {
        printf("Error: %d\n", status);
    } else {
        printf("Success!\n");
    }

    return status;
}
