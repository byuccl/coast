/**
 * This file contains structure ojects that are allocated in heap, and hence 
 * you don't need to promote all of them
 */

#include <stdio.h>
#include <stdlib.h>

const double oneOverSeven = 0.142857;

struct SimpleStruct { int M; double X; };
struct CyclicStruct { int M; struct CyclicStruct *left, *right; };
struct NestedStruct { int M; struct SimpleStruct S; struct CyclicStruct C; };
struct NestedArray  { int M; double X[5]; struct SimpleStruct S[3]; };

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

struct CyclicStruct*
allocCyclic(int Mval)
{
  struct CyclicStruct *N;
  N = (struct CyclicStruct *) malloc(sizeof(struct CyclicStruct));
  N->M = Mval;
  N->left = N->right = NULL;
  return N;
}

void
testCyclic()
{
  struct CyclicStruct *N, Nleft, Nright;
  N = allocCyclic(20);
  Nleft.M  = 30;
  Nright.M = 40;
  N->left  = &Nleft;
  N->right = &Nright;
  printf("testCyclic: %d %d %d\n",
         N->M, N->left->M, N->right->M);
  free(N);
}

void
testNestedStruct()
{
  struct NestedStruct N;

  struct CyclicStruct *Sleft, *Sright;
  Sleft  = allocCyclic(0);
  Sright = allocCyclic(0);

  initSimple(&N.S, 50, oneOverSeven);
  N.C.left = Sleft;
  N.C.right = Sright;

  printf("testNestedStruct: %d %lf %p %p\n",
         N.S.M, N.S.X, N.C.left->left, N.C.right->right);

  free(N.C.left);
  free(N.C.right);
}

void
testNestedArray()
{
  struct NestedArray N;
  int i, j;

  N.M = 60;

  for (i=0; i < 5; ++i)
    N.X[i] = oneOverSeven * i;

  for (j=0; j < 3; ++j)
    initSimple(&N.S[j], N.M + 5 + j, oneOverSeven * j);

  printf("testNestedArray: %d %lf %d %lf\n",
         N.M, N.X[4], N.S[0].M, N.S[2].X);
}

void
testCompare()
{
  struct SimpleStruct *S;
  S = (struct SimpleStruct *) malloc(sizeof(struct SimpleStruct));
  initSimple(S, 88, 99.0);
  if (S == NULL) {
    fprintf(stderr, "testCompare(): malloc() failed!\n");
    abort();
  }
  S->M = 70;
  printf("testCompare: %d\n", S->M);
  free(S);
}

int
main(int argc, char** argv)
{
  testSimple();
  testCyclic();
  testNestedStruct();
  testNestedArray();
  testCompare();
  return 0;
}
