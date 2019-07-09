// This unit test is to see how the LLVM IR represents SIMD instructions
// All this does is double all of the values in a matrix
// have to make sure the XCFLAGS="-O3"

#include <stdio.h>
#include <stdlib.h>

#define SCALAR      2

// #define ARRAY_SIZE  4
// unsigned int matrix[ARRAY_SIZE][ARRAY_SIZE] = {
//     {1, 2, 3, 4},
//     {5, 6, 7, 8},
//     {9, 10, 11, 12},
//     {13, 14, 15, 16}
// };

// #define ARRAY_SIZE  5
// unsigned int matrix[ARRAY_SIZE][ARRAY_SIZE] = {
//     {1, 2, 3, 4, 5},
//     {6, 7, 8, 9, 10},
//     {11, 12, 13, 14, 15},
//     {16, 17, 18, 19, 20},
//     {21, 22, 23, 24, 25}
// };

#define ARRAY_SIZE  2
#define ARRAY_SIZE2 8
unsigned int matrix[ARRAY_SIZE][ARRAY_SIZE2] = {
    {1, 2, 3, 4, 5, 6, 7, 8},
    {9, 10, 11, 12, 13, 14, 15, 16}
};

int main(){
    unsigned short i, j;
    for(i = 0; i < ARRAY_SIZE; i++){
        for(j = 0; j < ARRAY_SIZE2; j++){
            matrix[i][j] *= SCALAR;
        }
    }
    printf("%d\n", matrix[0][0]);
    //expected result: SCALAR * 1
    printf("thing: %d\n", 4);
}
