/*
Modified by Matthew Bohman (BYU 2017)
https://github.com/lanl/benchmark_codes

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

//#define USING_MSP
#define RANDVALUES

#ifdef USING_MSP
#include "supportFiles/fiji.h"
#else
#include <stdio.h>
#include <stdlib.h>
#include "../COAST.h"
#endif

// __DEFAULT_NO_xMR

#define	side 9

int first_matrix[side][side];
int second_matrix[side][side];
unsigned __xMR results_matrix[side][side];
unsigned __NO_xMR golden[side][side];

int seed_value = 42;

void __xMR initialize() {
	int i = 0;
	int j = 0;

	//int z;
	//int qq = __builtin_annotation(z,"this_is_srand");
	srand(seed_value);

	//fill the matrices
	for ( i = 0; i < side; i++ ){
		for (j = 0; j < side; j++) {

			#ifdef RANDVALUES
			first_matrix[i][j] = rand();
			second_matrix[i][j] = rand();
			#else
			first_matrix[i][j] = i*j;
			second_matrix[i][j] = i*j;
			#endif
		}
	}
}

void matrix_multiply(int f_matrix[][side], int s_matrix[][side], unsigned r_matrix[][side]) {
	int i = 0;
	int j = 0;
	int k = 0;
	unsigned long sum = 0;

	//MM
	for ( i = 0 ; i < side ; i++ ) {
		for ( j = 0 ; j < side ; j++ ) {
			for ( k = 0 ; k < side ; k++ ) {
				sum = sum + f_matrix[i][k]*s_matrix[k][j];
			}

			r_matrix[i][j] = sum;
			sum = 0;
		}
	}
}

__attribute__((noinline))
int checkGolden() {
	int __xMR num_of_errors = 0;
	int i = 0;
	int j = 0;

	for(i=0; i<side; i++) {
		for (j = 0; j < side; j++) {
			if (golden[i][j] != results_matrix[i][j]) {
				num_of_errors++;
			}
		}
	}

	return num_of_errors;
}

__attribute__((noinline))
void test() {
	matrix_multiply(first_matrix, second_matrix, results_matrix);
}

__attribute__((noinline))
void generateGolden(){
	matrix_multiply(first_matrix, second_matrix, golden);
}

int main() {
	int __NO_xMR numErrors;

	#ifdef USING_MSP
	WDTCTL = WDTPW | WDTHOLD;                 // Stop WDT
	PM5CTL0 = PM5CTL0 & ~LOCKLPM5;            // GPIO power-on default high-impedance mode disabled to use prior settings
	#endif

	initialize();
	generateGolden();

	test();

	numErrors = checkGolden();

	#ifndef USING_MSP
	printf("Number of errors: %d\n",numErrors);
	#endif
	return numErrors;
}
