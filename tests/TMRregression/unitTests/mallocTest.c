// This unit test is to make sure malloc works with TMR
// malloc() wrappers may need special treatment
//  must include `-replicateFnCalls=alloc_struct ` after -TMR,
//  or include the annotations as shown below
#include <stdio.h>
#include <stdlib.h>

#define ARRAY_SIZE  4
#define ELEMENT_SIZE    10

typedef struct{
    float a;
    float b;
} inner_struct;

typedef struct{
    int x;
    int y;
    int array[ARRAY_SIZE];
    inner_struct z;
}outer_struct;

outer_struct* __attribute__((annotate("xMR_call"))) alloc_struct(){
    outer_struct* st = (outer_struct*) malloc(sizeof(outer_struct));
    return st;
}

int main(){
    //don't even need to do anything with the struct, just create it and destroy it
    outer_struct* st = alloc_struct();
    free(st);
    printf("Finished\n");
}
