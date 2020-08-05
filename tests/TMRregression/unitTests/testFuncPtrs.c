/*
 * testFuncPtrs.c
 *
 * This was added specifically to make sure TMR works with function pointers
 * Original source:
 * https://gist.github.com/robstewart57/b11353feb69dc1a6dc30
 *
 * Also add a global function pointer to make sure it isn't cloned.
 * Test to see if COAST treats ISR function pointers correctly.
 */


/********************************** Includes **********************************/
#include <stdio.h>

/**************************** Function Prototypes *****************************/
void fakeISRfunc(int x);

/*********************************** Values ***********************************/
// global pointer to fake ISR function
void (*fakeISRptr) (int x) = fakeISRfunc;


/********************************* Functions **********************************/

void fakeISRfunc(int x) {
    x += 1;
    return;
}


// Some math functions
int add(int i, int j)
{
   return (i + j);
}

int sub(int i, int j)
{
   return (i - j);
}


// Print results of math function from ptr call
int print(int x, int y, int (*func)())
{
    int val = (*func)(x, y);
    // printf("value is : %d\n", val);
    // check values
    if ( (val == 300) || (val == -100) ) {
        return 0;
    } else {
        return val;
    }
}


int main()
{
    int returnVal = 0;
    int x=100, y=200;

    // test with calling function pointers
    returnVal |= print(x, y, add);     // expected output: 300
    returnVal |= print(x, y, sub);     // expected output: -100
    // NOTE: COAST replicates the calls themselves

    // see if we can create arrays of function pointers
    int (* pBitCntFunc[2])(int, int) = {
        add,
        sub
    };

    // test global function pointer
    fakeISRptr(0);

    // Check values
    if (returnVal) {
        printf("Error: %d\n", returnVal);
    } else {
        printf("Success!\n");
    }

    return returnVal;
}
