/*
 *  Unit test designed to exercise returning struct types.
 *  Can change the data types to see what happens.
 *
 *  Notes:
 *  if the data types can fit within a normal wordsize of the target system,
 *  such as how 2 ints can fit into a 64-bit register, then it's not a problem.
 *  float returns a vector type, which is interesting.
 */

#include <stdio.h>

#define TEST_DATA_TYPE double
typedef TEST_DATA_TYPE testdata_t;

typedef struct _testStruct {
    testdata_t x;
    testdata_t y;
} testStruct_t;

testStruct_t newStruct() {
    return (testStruct_t) {1, 2};
}

int structCompare(testStruct_t d0, testStruct_t d1) {
    return (d0.x == d1.x) && (d0.y == d1.y);
}

int main() {
    int returnVal = 0;

    testStruct_t d0 = {1, 2};
    testStruct_t d1 = newStruct();
    if (structCompare(d0, d1)) {
        printf("Equal!\n");
    } else {
        printf("Not equal!\n");
        returnVal = -1;
    }

    return returnVal;
}
