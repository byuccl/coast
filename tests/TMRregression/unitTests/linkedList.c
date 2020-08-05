/*
 * linkedList.c
 * 
 * This unit test is designed to show one of the difficulties
 *  encountered when protecting the FreeRTOS kernel.
 * Linked list items should be kept inside or outside the
 *  Sphere or Replication (SoR), because crossing that boundary
 *  with pointers can cause wonderfully interesting problems.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// COAST configuration
#include "../../COAST.h"
__DEFAULT_NO_xMR


// we define a type that represents a linked list.
typedef struct node_s node_t;

// each node has a value, and a previous and next pointer
struct node_s {
    uint32_t val;
    node_t* prev;
    node_t* next;
};


/****************************** Verbose printing ******************************/
typedef struct buf_s {
    char buf[256];
    int idx;
} buf_t;


#define PRINTBUF(b, i, fmt, ...) \
    b[i].idx += sprintf(b[i].buf+b[i].idx, fmt, __VA_ARGS__)
#define SIZE 7
void printListVerbose(node_t* list) __xMR_FN_CALL {
    // allocate some buffers
    buf_t b[SIZE];
    int i;
    for (i = 0; i < SIZE; i+=1) {
        memset(&b[i], 0, sizeof(buf_t));
    }
    // filler string
    char* filler = "───────────────────────────────";
    int fillLen = 36;

    // fill with nicely formatted data
    while (list != NULL) {
        // usually prints 12 characters
        PRINTBUF(b, 1, "│ %10p │  ", list);
        PRINTBUF(b, 3, "│ %10p │⇢ ", list->next);
        PRINTBUF(b, 4, "│ %10p │⇠ ", list->prev);
        PRINTBUF(b, 5, "│ %10d │  ", list->val);
        PRINTBUF(b, 0, "┌%*.*s┐  ", fillLen, fillLen, filler);
        PRINTBUF(b, 2, "├%*.*s┤  ", fillLen, fillLen, filler);
        PRINTBUF(b, 6, "└%*.*s┘  ", fillLen, fillLen, filler);
        list = list->next;
    }

    // print it out
    for (i = 0; i < SIZE; i+=1) {
        printf("%s\n", b[i].buf);
    }
    printf("\n");
}

/******************************************************************************/


// inserts a new node at a spot in the list
void listInsertAfter(node_t* listSpot, node_t* node) __xMR_FN_CALL {
    node_t* temp;

    // save current next pointer
    temp = listSpot->next;
    
    // give node the right pointers
    node->next = temp;
    node->prev = listSpot;

    // update the ones around it
    listSpot->next = node;
    if (temp) {
        temp->prev = node;
    }

    return;
}


// this version will be TMR'd
void listInsertAfter3(node_t* listSpot, node_t* node) __xMR {
    node_t* temp;

    // save current next pointer
    temp = listSpot->next;
    
    // give node the right pointers
    node->next = temp;
    node->prev = listSpot;

    // update the ones around it
    listSpot->next = node;
    if (temp) {
        temp->prev = node;
    }

    return;
}


// remove this item from the list
void listDeleteNode(node_t* node) __xMR_FN_CALL {
    if (!node) {
        printf("Error, node is NULL!\n");
        return;
    }
    // remove all references
    node->next->prev = node->prev;
    node->prev->next = node->next;
    
    // because the pointer is being orphaned, we're going 
    //  to free the memory
    // free(node);
    // leave out for functionality sake
    // yes, I know there will be memory leaks

    return;
}


// helper function to create a new node, unconnected
node_t* createNode() __xMR_FN_CALL {
    // allocate the memory
    node_t* n = (node_t*) malloc(sizeof(node_t));

    // set the links to NULL
    n->prev = NULL;
    n->next = NULL;

    return n;
}


// textual representation of a list
void printList(node_t* list) __xMR_FN_CALL {
    while (list != NULL) {
        printf("%d, ", list->val);
        list = list->next;
    }
    printf("\n");
}


void normalUsage(void) {
    int i;
    int size = 4;

    // create a base
    node_t* list = createNode();
    list->val = 0;

    // let's add some nodes
    for (i = size; i > 1; i-=1) {
        node_t* next = createNode();
        next->val = i-1;
        listInsertAfter(list, next);
    }

    printListVerbose(list);
    listDeleteNode(list->next);
    listDeleteNode(list->next);
    printListVerbose(list);

    /*
     * expected output:
     * 0, 1, 2, 3, 4, 
     * 0, 3, 4, 
     */
}


void tmrUsage(void) __xMR {
    int i;
    int size = 4;

    // create a base
    node_t* list = createNode();
    list->val = 0;

    // let's add some nodes
    for (i = size; i > 1; i-=1) {
        node_t* next = createNode();
        next->val = i-1;
        listInsertAfter(list, next);
    }

    // call these for each of the clones
    printListVerbose(list);
    listDeleteNode(list->next);
    listDeleteNode(list->next);
    printListVerbose(list);
    /*
     * expected output:
     * 0, 1, 2, 3, 4, 
     * 0, 1, 2, 3, 4, 
     * 0, 1, 2, 3, 4, 
     * 0, 3, 4, 
     * 0, 3, 4, 
     * 0, 3, 4, 
     */
}


// this has to be a global
static node_t* specialNode;

void crossBoundaryAdd() __xMR {
    // create a list
    node_t* list = createNode();
    list->val = 0;
    node_t* last = createNode();
    last->val = 42;

    // add a global
    listInsertAfter3(list, last);
    printListVerbose(list);
    listInsertAfter3(list, specialNode);
    printf("address of specialNode: %p\n\n", specialNode);
    printListVerbose(list);
    listDeleteNode(list->next);
    printListVerbose(list);
}


/*
 * This will have 3 copies of a list created in the SoR,
 *  but try to add and then remove a value from outside the Sor.
 */
void crossBoundaryUsage(void) {
    specialNode = createNode();
    specialNode->val = 1234;
    crossBoundaryAdd();
}


int main() {
    
    printf("\nNormal usage:\n");
    normalUsage();

    printf("\nTMR usage:\n");
    tmrUsage();

    printf("\nCross boundary usage:\n");
    crossBoundaryUsage();
    printf("\n");

    return 0;
}


/*
 * Expected output (fails in compilation):
 * ERROR: unprotected global "specialNode" is being read from and written to inside protected functions:
 *  "listInsertAfter3" at     store %struct.node_s* %node, %struct.node_s** %node.addr, align 8,
 */
