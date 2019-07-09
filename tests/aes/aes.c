#include <stdio.h>
#include <stdlib.h>

#include <string.h>
#include "TI_aes_128.h"
#include "ECBGFSbox128.h"
#include "ECBKeySbox128.h"
#include "ECBVarKey128.h"
#include "ECBVarTxt128.h"

int local_errors = 0;

int golden;
__attribute__((noinline))
void generateGolden(){
	;
}

void check_arrays(unsigned char *array1, unsigned char *array2, int lim, char pre) {
	int i = 0;

	for (i = 0; i < lim; i++) {
		if (array1[i] != array2[i]) {
			local_errors++;
		}
	}
}

void aes_test() {
	int i = 0;
	int j = 0;
	int k = 0;
	unsigned int count = 0;
	unsigned int ret = 0;
	int total_errors = 0;
	int first_error = 0;

	unsigned char key[] = { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
	unsigned char key2[] = { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
	unsigned char gold_cypher[] = {0x08, 0xa4, 0xe2, 0xef, 0xec, 0x8a, 0x8e, 0x33, 0x12, 0xca, 0x74, 0x60, 0xb9, 0x04, 0x0b, 0xbf };
	unsigned char gold_plain[] = {0x58, 0xc8, 0xe0, 0x0b, 0x26, 0x31, 0x68, 0x6d, 0x54, 0xea, 0xb8, 0x4b, 0x91, 0xf0, 0xac, 0xa1 };
	unsigned char input[] = {0x58, 0xc8, 0xe0, 0x0b, 0x26, 0x31, 0x68, 0x6d, 0x54, 0xea, 0xb8, 0x4b, 0x91, 0xf0, 0xac, 0xa1 };

	for (k = 0; k < 4; k++) {
		if (k == 0) {
			count = ECBGFSbox128_count;
		}
		else if (k == 1) {
			count = ECBKeySbox128_count;
		}
		else if (k == 2) {
			count = ECBVarKey128_count;
		}
		else {
			count = ECBVarTxt128_count;
		}

		for (j = 0; j < count; j++){
			//read data arrays out of flash
			for (i = 0; i < 16; i++) {
				if (k == 0) {
					key[i] = ECBGFSbox128[i + j*80];
					key2[i] = ECBGFSbox128[i + 16 + j*80];
					gold_cypher[i] = ECBGFSbox128[i + 32 + j*80];
					gold_plain[i] = ECBGFSbox128[i + 48 + j*80];
					input[i] = ECBGFSbox128[i + 64 + j*80];
				}
				if (k == 1) {
					key[i] = ECBKeySbox128[i + j*80];
					key2[i] = ECBKeySbox128[i + 16 + j*80];
					gold_cypher[i] = ECBKeySbox128[i + 32 + j*80];
					gold_plain[i] = ECBKeySbox128[i + 48 + j*80];
					input[i] = ECBKeySbox128[i + 64 + j*80];
				}
				if (k == 2) {
					key[i] = ECBVarKey128[i + j*80];
					key2[i] = ECBVarKey128[i + 16 + j*80];
					gold_cypher[i] = ECBVarKey128[i + 32 + j*80];
					gold_plain[i] = ECBVarKey128[i + 48 + j*80];
					input[i] = ECBVarKey128[i + 64 + j*80];
				}
				if (k == 3) {
					key[i] = ECBVarTxt128[i + j*80];
					key2[i] = ECBVarTxt128[i + 16 + j*80];
					gold_cypher[i] = ECBVarTxt128[i + 32 + j*80];
					gold_plain[i] = ECBVarTxt128[i + 48 + j*80];
					input[i] = ECBVarTxt128[i + 64 + j*80];
				}

			}

			check_arrays(input, gold_plain, sizeof(input), 'S');

			aes_enc_dec(input,key,0);

			check_arrays(input, gold_cypher, sizeof(input), 'E');

			aes_enc_dec(input,key2,1);

			check_arrays(input, gold_plain, sizeof(input), 'D');
		}
	}
}

int main( void ) {

	
	//run test
	generateGolden();
	aes_test();


	#ifndef USING_MSP
	printf("Number of errors: %d\n",local_errors);
	#endif
	return local_errors;
}
