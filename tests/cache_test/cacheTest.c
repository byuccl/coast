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

//*****************************************************************************
//
// AUTHOR:  Heather Quinn
// CONTACT INFO:  hquinn at lanl dot gov
// LAST EDITED: 12/21/15
//
// cache_test.c
//
// This test is a simple program for instrumenting SRAM memory blocks or caches.
// It tests the foura mostly 0s memory pattern.  A simple sum of
// memory array elements is also done to check for transients in logic.
// Keep in mind that the sum checker is hardcoded.  If the memory arrays change
// size or values, it is necessary to change the sum values, too.
//
// This software is otimized for TI microcontrollers, specifically the
// MSP430F2619.
//
// The output is designed to go out the UART at a speed of 9,600 baud and uses a
// tiny printf to reduce printf footprint.  The tiny printf can be downloaded from
// http://www.43oh.com/forum/viewtopic.php?f=10&t=1732  All of the output is YAML
// parsable.
//
//
//*****************************************************************************
#include <string.h>
#include <stdio.h>

void cache_test(void);
void init_array(int array[]);
int calc_sum(int array[]);

#define 	robust_printing			1
#define		data_array_elements		600
int golden;          //sum constant

int array[data_array_elements];

unsigned long int ind = 0;
int local_errors = 0;
int sum_errors = 0;
int in_block = 0;

void generateGolden(){
   golden = 179700;
}

void init_array(int *array) {
  int i = 0;

  for ( i = 0; i < data_array_elements; i++ ) {
    array[i] = i;
  }
}

int calc_sum(int *array) {
  int i = 0;
  int sum = 0;
  int first_error = 0;
  int numberOfErrors = 0;

  for ( i = 0; i < data_array_elements; i++) {
    sum += array[i];

    if ( array[i] != i ) {
      numberOfErrors++;

      //there is an error, start block of code for printing
      if (!first_error) {
	if (!in_block && robust_printing) {
	  printf(" - i: %lu\r\n", ind);
	  printf("   E: {%i: %i,", i, array[i]);
	  first_error = 1;
	  in_block = 1;
	}
	else if (in_block && robust_printing){
	  printf("   E: {%i: %i,", i, array[i]);
	  first_error = 1;
	}
      }
      else{
	if (robust_printing)
	  printf("%i: %i,", i, array[i]);
      }

      //fix and record error
      array[i] = i;
      local_errors++;
    }

  }

  //finish printing
  if (first_error && robust_printing) {
    printf("}\r\n");
    first_error = 0;
  }

  //handle the less robust printing case
  if (!robust_printing && numberOfErrors > 0) {
    if (!in_block) {
      printf(" - i: %lu\r\n", ind);
      printf("   E: %i\r\n", numberOfErrors);
      in_block = 1;
    }
    else {
      printf("   E: %i\r\n", numberOfErrors);
    }
  }

  //check for a math error
  if (sum != golden) {
    //the difference between robust and not robust printing is trivial for this one, so let it go

    //only count sum errors if there have been no other errors
    if (local_errors == 0) {
      sum_errors++;
      local_errors++;

      if (!in_block) {
	printf(" - i: %lu\r\n", ind);
	printf("   S: {%i: %i}\r\n", golden, sum);
	first_error = 1;
	in_block = 1;
      }
      else if (in_block){
	printf("   S: {%i: %i}\r\n", golden, sum);
	first_error = 1;
      }
    }
  }

  return sum;
}

void cache_test(void) {
  int sum = 0;
  int total_errors = 0;
  int tests_with_errors = 0;

  generateGolden();
  init_array(array);

  // Read Pattern
  //while (1) {

    sum = calc_sum(array);

    //acking every few seconds to make certain the program is still alive/
    if (ind % 1000 == 0 && ind != 0) {
      printf("# %lu, %i, %i, %i\r\n", ind, total_errors, tests_with_errors, sum_errors);
    }

    ind++;
    total_errors += local_errors;
    if (local_errors > 0) {
      tests_with_errors++;
    }
    local_errors = 0;
    in_block = 0;
  //}
}


int main(void) {
  //start test
  cache_test();
  printf("%d\n",local_errors);
}
