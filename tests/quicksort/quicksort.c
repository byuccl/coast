/*
Copyright (c) 2015, Los Alamos National Security, LLC
All rights reserved.

Copyright 2015. Los Alamos National Security, LLC. This software was
produced under U.S. Government contract DE-AC52-06NA25396 for Los
Alamos National Laboratory (LANL), which is operated by Los Alamos
National Security, LLC for the U.S. Department of Energy. The
U.S. Government has rights to use, reproduce, and distribute this
software.  NEITHER THE GOVERNMENT NOR LOS ALAMOS NATIONAL SECURITY,
LLC MAKES ANY WARRANTY, EXPRESS OR IMPLIED, OR ASSUMES ANY LIABILITY
FOR THE USE OF THIS SOFTWARE.  If software is modified to produce
derivative works, such modified software should be clearly marked, so
as not to confuse it with the version available from LANL.

Additionally, redistribution and use in source and binary forms, with
or without modification, are permitted provided that the following
conditions are met:

• Redistributions of source code must retain the above copyright
         notice, this list of conditions and the following disclaimer.

• Redistributions in binary form must reproduce the above copyright
         notice, this list of conditions and the following disclaimer
         in the documentation and/or other materials provided with the
         distribution.

• Neither the name of Los Alamos National Security, LLC, Los Alamos
         National Laboratory, LANL, the U.S. Government, nor the names
         of its contributors may be used to endorse or promote
         products derived from this software without specific prior
         written permission.

THIS SOFTWARE IS PROVIDED BY LOS ALAMOS NATIONAL SECURITY, LLC AND
CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING,
BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL LOS
ALAMOS NATIONAL SECURITY, LLC OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.*/

/*
 *****************************************************************************
//
// AUTHOR:  Heather Quinn
// CONTACT INFO:  hquinn at lanl dot gov
// LAST EDITED: 12/21/15
//
// tiva_qsort.c
//
// This test is a simple program for testing quicksort.  The data are randomly
// generated and placed in an array.  The inputs change every few seconds in a
// repeatable pattern.  The test is designed to test whether sorting many numbers
// cause more errors than sorting an already sorted array.  To that end, the
// test sorts an array two times in a row in the forward direction, followed by two
// times in the reverse direction.  The qsort code that we used can be found here:
//
// http://rosettacode.org/wiki/Sorting_algorithms/Quicksort#C
//
// The user will need to create the reverse sort on their own.
//
// This software is otimized for microcontrollers.  In particular, it was designed
// for the Texas Instruments MSP430F2619.

// The output is designed to go out the UART at a speed of 9,600 baud and uses a tiny
// print to reduce the printf footprint.  The tiny printf can be downloaded from
// http://www.43oh.com/forum/viewtopic.php?f=10&t=1732  All of the output is YAML
// parsable.
//
 *****************************************************************************/

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define		array_elements				580
#define		robust_printing				1
#define		change_rate				100

unsigned long int ind = 0;
int local_errors = 0;
int in_block = 0;
int seed_value = -1;
int array[array_elements];
int golden_array[array_elements];
int golden_array_rev[array_elements];

void init_array() {
  int i = 0;

  //seed the random number generator
  //the input arrays are reset on error to the same values, so
  //the seed value is not always new.  The seed value also
  //changes with the change rate so that new values are used
  //every few seconds during the test.
  if (seed_value == -1) {
    srand(ind);
    seed_value = ind;
  }
  else {
    srand(seed_value);
  }

  //fill the matrices
  for ( i = 0; i < array_elements; i++ ){
    int val = rand();
    array[i] = val;
    golden_array[i] = val;
    golden_array_rev[i] = val;
  }
}


//*****************************************************************************
//
// Quick sort code from http://rosettacode.org/wiki/Sorting_algorithms/Quicksort#C
//
//*****************************************************************************

void quick_sort (int *A, int len) {
	if (len < 2) return;

    int pivot = A[len / 2];

    int i, j;
    for (i = 0, j = len - 1; ; i++, j--) {
      while (A[i] < pivot) i++;
      while (A[j] > pivot) j--;

      if (i >= j) break;

      int temp = A[i];
      A[i]     = A[j];
      A[j]     = temp;
    }

    quick_sort(A, i);
    quick_sort(A + i, len - i);
}

void quick_sort_rev (int *a, int n) {
  //TODO: user enters code
}

int checker(int golden_array[], int dut_array[], int sub_test) {
  int first_error = 0;
  int num_of_errors = 0;
  int i = 0;

  for(i=0; i<array_elements; i++) {
    if (golden_array[i] != dut_array[i]) {
      //an error is found, print the results
      if (!first_error) {
	if (!in_block && robust_printing) {
	  printf(" - i: %lu, %i\r\n", ind, sub_test);
	  printf("   E: {%i: [%x, %x],", i, golden_array[i], dut_array[i]);
	  first_error = 1;
	  in_block = 1;
	}
	else if (in_block && robust_printing){
	  printf("   E: {%i: [%x, %x],", i, golden_array[i], dut_array[i]);
	  first_error = 1;
	}
      }
      else {
	if (robust_printing)
	  printf("%i: [%x, %x],", i, golden_array[i], dut_array[i]);

      }
      num_of_errors++;
    }
  }

  //more printing
  if (first_error) {
    printf("}\r\n");
    first_error = 0;
  }

  //non-robust printing
  if (!robust_printing && (num_of_errors > 0)) {
    if (!in_block) {
      printf(" - i: %lu, %i\r\n", ind, sub_test);
      printf("   E: %i\r\n", num_of_errors);
      in_block = 1;
    }
    else {
      printf("   E: %i\r\n", num_of_errors);
    }
  }

  return num_of_errors;
}

void qsort_test() {
  //initialize variables
  int total_errors = 0;
  int n = sizeof array / sizeof array[0];
  int i = 0;

  //init arrays
  init_array();

  //compute the goldens for the forward and reverse sorts.
  quick_sort(golden_array, n);
  quick_sort_rev(golden_array_rev, n);

  while (1) {
    for (i = 0; i < 4; i++) {
      //the first two sorts are forward
      if (i < 2) {
	quick_sort(array, n);
	local_errors = checker(golden_array, array, i);
      }
      else {
	//the last two sorts are reverse
	quick_sort_rev(array, n);
	local_errors = checker(golden_array_rev, array, i);
      }

      //if there is an erro, fix the input arrays
      //and recompute the two goldens.
      if (local_errors > 0) {
	init_array();
	quick_sort(golden_array, n);
	quick_sort_rev(golden_array_rev, n);
      }

      total_errors += local_errors;
      local_errors = 0;
      in_block = 0;

    }

    //ack and change input arrays
    if (ind % change_rate == 0) {
      printf("# %lu, %i\r\n", ind, total_errors);
      seed_value = -1;

      //init arrays with new values
      init_array();

      //compute the two new golden arrays
      quick_sort(golden_array, n);
      quick_sort_rev(golden_array_rev, n);
    }

    //reset vars and such
    ind++;
  }

}

int main() {


  //print the YAML header
  printf("\r\n---\r\n");
  printf("hw: msp430f2619\r\n");
  printf("test: QSort\r\n");
  printf("mit: none\r\n");
  printf("printing: %i\r\n", robust_printing);
  printf("input change rate: %i\r\n", change_rate);
  printf("Array size: %i\r\n", array_elements);
  printf("ver: 0.1\r\n");
  printf("fac: LANSCE Nov 2015\r\n");
  printf("d:\r\n");

  //start the test
  qsort_test();

  return 0;

}
