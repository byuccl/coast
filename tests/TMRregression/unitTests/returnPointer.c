/*
    returnPointer.c
    Unit test designed to exercise returning pointer types.

    Notes:
    Possible compilation error when run with DWC
*/

#include <stdio.h>

int* something(int* x) {
    if (*x >= 0)
        *x += 1;
    else
        *x -= 1;
    return x;
}

int main() {
    int x = 0;
    int* y = something(&x);
    x = *y;

    printf("x = %d\n", x);

    //expected results: x == 1
    if (x == 1) {
        printf("Success!\n");
        return 0;
    } else {
        printf("Error!\n");
        return -1;
    }

    return 0;
}
