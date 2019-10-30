/*
 * simd.c
 *
 * This unit test is to see how the LLVM IR represents SIMD instructions
 * All this does is double all of the values in a matrix
 * have to make sure the flag XCFLAGS="-O3"
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

/***************************** Intrinsic settings *****************************/
#define WITH_INTRINSICS
#ifdef WITH_INTRINSICS
#ifdef __x86_64
#include <emmintrin.h>
#include <immintrin.h>
#elif __arm
#include <arm_neon.h>
#endif
#endif  /* WITH_INTRINSICS */


/**************************** COAST configuration *****************************/
#include "../../COAST.h"
__DEFAULT_NO_xMR

//all values in the matrix will be multiplied by this value
#define SCALAR      2

//this is only used when compiled with flag -countErrors
unsigned int __NO_xMR TMR_ERROR_CNT = 0;

//define the testing type
typedef unsigned int test_t;
// typedef float test_t;


#define ROW_SIZE  2
#define COL_SIZE 8
test_t matrix0[ROW_SIZE][COL_SIZE] = {
    {1, 2, 3, 4, 5, 6, 7, 8},
    {9, 10, 11, 12, 13, 14, 15, 16}
};
test_t golden0[ROW_SIZE][COL_SIZE] = {
    {2, 4, 6, 8, 10, 12, 14, 16},
    {18, 20, 22, 24, 26, 28, 30, 32}
};

// don't inline the matrix multiply call so it can be xMR'd correctly
__attribute__((noinline))
#ifdef WITH_INTRINSICS
#ifdef __x86_64
// hand optimized for x86_64 architecture with the SSE2 extension
void scalarMultiply(test_t scalar) __xMR {
    // const __m128 scalar_vec = _mm_set1_epi32(scalar);
    const __m128 scalar_vec = _mm_set1_ps(scalar);

    __m128i r0 = _mm_load_si128((__m128i *)&matrix0[0][0]);     // load integers
    __m128 m0 = _mm_cvtepi32_ps(r0);                            // convert to float
    m0 = _mm_mul_ps(m0, scalar_vec);                            // multiply by scalar
    __m128i w0 = _mm_cvtps_epi32(m0);                           // convert to int
    _mm_store_si128((__m128i *)&matrix0[0][0], w0);             // store integers

    __m128i r1 = _mm_load_si128((__m128i *)&matrix0[0][4]);
    __m128 m1 = _mm_cvtepi32_ps(r1);
    m1 = _mm_mul_ps(m1, scalar_vec);
    __m128i w1 = _mm_cvtps_epi32(m1);
    _mm_store_si128((__m128i *)&matrix0[0][4], w1);

    __m128i r2 = _mm_load_si128((__m128i *)&matrix0[1][0]);
    __m128 m2 = _mm_cvtepi32_ps(r2);
    m2 = _mm_mul_ps(m2, scalar_vec);
    __m128i w2 = _mm_cvtps_epi32(m2);
    _mm_store_si128((__m128i *)&matrix0[1][0], w2);

    __m128i r3 = _mm_load_si128((__m128i *)&matrix0[1][4]);
    __m128 m3 = _mm_cvtepi32_ps(r3);
    m3 = _mm_mul_ps(m3, scalar_vec);
    __m128i w3 = _mm_cvtps_epi32(m3);
    _mm_store_si128((__m128i *)&matrix0[1][4], w3);
}
#elif __arm
// http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.dui0472j/chr1360928373893.html
// https://developer.arm.com/architectures/instruction-sets/simd-isas/neon/intrinsics
void scalarMultiply(test_t scalar) __xMR {
    uint32x4_t scalar_vec = vdupq_n_u32(scalar);

    uint32x4_t r0 = vld1q_u32(&matrix0[0][0]);      // load integers
    uint32x4_t m0 = vmulq_u32(r0, scalar_vec);      // parallel multiply
    vst1q_u32(&matrix0[0][0], m0);                  // store integers

    uint32x4_t r1 = vld1q_u32(&matrix0[0][4]);
    uint32x4_t m1 = vmulq_u32(r1, scalar_vec);
    vst1q_u32(&matrix0[0][4], m1);

    uint32x4_t r2 = vld1q_u32(&matrix0[1][0]);
    uint32x4_t m2 = vmulq_u32(r2, scalar_vec);
    vst1q_u32(&matrix0[1][0], m2);

    uint32x4_t r3 = vld1q_u32(&matrix0[1][4]);
    uint32x4_t m3 = vmulq_u32(r3, scalar_vec);
    vst1q_u32(&matrix0[1][4], m3);
}
#endif /* __x86_64 */

#else
void scalarMultiply(test_t scalar) __xMR {
    unsigned short i, j;
    for (i = 0; i < ROW_SIZE; i++) {
        for (j = 0; j < COL_SIZE; j++) {
            matrix0[i][j] *= scalar;
        }
    }
}
#endif  /* WITH_INTRINSICS */

int matrixMatch(test_t mat[ROW_SIZE][COL_SIZE], test_t golden[ROW_SIZE][COL_SIZE]) __NO_xMR {
    unsigned short i, j;
    for (i = 0; i < ROW_SIZE; i++) {
        for (j = 0; j < COL_SIZE; j++) {
            if (mat[i][j] != golden[i][j]) {
                return false;
            }
        }
    }
    return true;
}

void printMatrix(test_t mat[ROW_SIZE][COL_SIZE]) {
    unsigned short i, j;
    for (i = 0; i < ROW_SIZE; i++) {
        printf("{ ");
        for (j = 0; j < COL_SIZE; j++) {
            printf("%2d, ", mat[i][j]);
            // printf("%2f, ", mat[i][j]);
        }
        puts("},");
    }
    puts("");
}

int main(){
    scalarMultiply((test_t)SCALAR);

    printMatrix(matrix0);
    printf("TMR errors: %d\n", TMR_ERROR_CNT);

    if (matrixMatch(matrix0, golden0)) {
        printf("Success!\n");
        return 0;
    } else {
        printf("Error!\n");
        return -1;
    }
}
