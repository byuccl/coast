/*
 * This is a program intended to help test the functionality
 * of an LLVM pass that does control flow checking
 * this should produce a nightmare of a control flow graph
 * I don't normally use goto statements, but I am trying to break things
 */

#include <stdlib.h>
#include <stdio.h>

int size = 20;

//To meet the FIJI standard
int golden;
__attribute__((noinline))
void generateGolden(){
    ;
}

void fillArray(int array[]){
  //printf("Starting to fill array\n");
  for(int i = 0; i < size; i++){
    array[i] = rand() % 100;
  }
  //printf("Finished filling array\n");
  return;
}

int main() {
  //printf("Starting program\n");
  generateGolden();
  int array[size];
  //printf("Initialized array\n");
  int total = 0;
  //printf("Initialized total counter\n");
  srand(42);
  //printf("Called srand\n");
  //create a randomly generated array of size 20
  fillArray(array);
  //printf("The array has been filled \n");
  int timesThroughWhile = 10;
  int i = 0;
 LOOP:for(; i < size; i++){
    switch(i){
    case 0:
      total += rand() % 10;
      break;
    case 5:
      total += 127;
      break;
    case 17:
      printf("total so far: %d\n", total);
      break;
    case 25:
      total += 25;
    case 37:
      goto WHILE;
    default:
      total -= 10;
    }
  WHILE:while(timesThroughWhile > 0){
      total -= 1;
      timesThroughWhile--;
      goto LOOP;
    }
  }

  printf("Total = %d\n", total);
  //printf("%d\n", zeroCount);
  return 0;
}
