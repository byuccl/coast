// This was added specifically to make sure TMR works with function pointers
// Original source:
// https://gist.github.com/robstewart57/b11353feb69dc1a6dc30
#include <stdio.h>

int add(int i, int j)
{
   return (i + j);
}

int sub(int i, int j)
{
   return (i - j);
}

void print(int x, int y, int (*func)())
{
        printf("value is : %d\n", (*func)(x, y));
}

int main()
{
    // test with calling function pointers
    int x=100, y=200;
    print(x,y,add);     // expected output: 300
    print(x,y,sub);     // expected output: -100

    // see if we can create arrays of function pointers
    int (* pBitCntFunc[2])(int, int) = {
      add, sub
    };

    return 0;
}
