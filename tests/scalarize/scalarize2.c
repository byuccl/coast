/**
 * This file contains structure ojects that are allocated in heap, and hence 
 * you don't need to promote all of them
 */

#include <stdio.h>
#include <stdlib.h>

const double oneOverSeven = 0.142857;

struct SimpleStruct { int M; double X; };
struct NestedStruct { int M; struct SimpleStruct S; };

void
initSimple(struct SimpleStruct* S, int Mval, double Xval)
{
  S->M = Mval;
  S->X = Xval;
}

void
testSimple()
{
  struct SimpleStruct S;
  initSimple(&S, 10, oneOverSeven);
  printf("testSimple: %d\n", S.M);
}


void
testNestedStruct()
{
  struct NestedStruct N;

  //struct CyclicStruct *Sleft, *Sright;
  //Sleft  = allocCyclic(0);
  //Sright = allocCyclic(0);

  initSimple(&N.S, 50, oneOverSeven);
//  N.C.left = Sleft;
//  N.C.right = Sright;

  printf("testNestedStruct: %d %lf\n",
         N.S.M, N.S.X);//N.C.left->left, N.C.right->right);

//  free(N.C.left);
//  free(N.C.right);
}


int
main(int argc, char** argv)
{
  testSimple();
  testNestedStruct();
  return 0;
}
