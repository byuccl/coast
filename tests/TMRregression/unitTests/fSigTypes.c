// This checks if dataflowProtection works correctly with
//  functions of 4 different types of signatures:
// 1) void, arguments > 0
// 2) non-void, arguments > 0
// 3) void, arguments == 0
// 4) non-void, arguments == 0
// specifically in the way that these functions are called

#include <stdio.h>


void inc(int i);
int add(int i, int j);
void incx(void);
int test();

//test function
void print(int z) {
    printf("value is : %d\n", z);
}

int main() {
    int a, b, c, d;
    float e;
    a = 1;
    b = 2;
    inc(b);
    c = add(a, b);
    print(c);
    //check
    if (c != 3) {
        printf("Error!\n");
        return -1;
    }

    incx();
    d = test();
    print(d);
    //check
    if (d != 1) {
        printf("Error!\n");
        return -1;
    }

    e = (float)add(b, c);
    printf("value is : %f\n", e);
    //check
    if (e != 5.0) {
        printf("Error!\n");
        return -1;
    }

    return 0;
}
