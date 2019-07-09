#include <stdio.h>
#include <stdlib.h>

struct SimpleStruct { int M; double X; };

/* Although this is a trivial case of scalar replacement, it is
 * instructive to study the unoptimized code.  Run "make trivial.llvm.bc",
 * disassemble the resulting file, and study it carefully to understand the
 * LLVM code that is generated.
 */
int
main(int argc, char** argv)
{
  struct SimpleStruct S;
  int i = 100;
  S.M = 10;
  S.X = 0.142857;
  printf("testSimple: %d, %d, %f\n", i, S.M, S.X);
  return 0;
}
