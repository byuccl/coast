//Pulled these constants from the support includes for the AES test

#include <stdio.h>

// ECBGFSbox128.h
const unsigned char zeroStartArray[] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x03, 0x36, 0x76, 0x3e, 0x96, 0x6d, 0x92, 0x59, 0x5a, 0x56, 0x7c, 0xc9, 0xce, 0x53, 0x7f, 0x5e,
	0xf3, 0x44, 0x81, 0xec, 0x3c, 0xc6, 0x27, 0xba, 0xcd, 0x5d, 0xc3, 0xfb, 0x08, 0xf2, 0x73, 0xe6,
};

// ECBKeySbox128.h
const unsigned char zeroEndArray[] = {
	0x10, 0xa5, 0x88, 0x69, 0xd7, 0x4b, 0xe5, 0xa3, 0x74, 0xcf, 0x86, 0x7c, 0xfb, 0x47, 0x38, 0x59,
	0x6d, 0x25, 0x1e, 0x69, 0x44, 0xb0, 0x51, 0xe0, 0x4e, 0xaa, 0x6f, 0xb4, 0xdb, 0xf7, 0x84, 0x65,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
const unsigned int count = 64;

int main() {
	// do something to make sure the arrays don't disappear
	int acc1 = 0, acc2 = 0;

	for (int i = 0; i < count; i++) {
		acc2 += zeroEndArray[i];
		acc1 += zeroStartArray[i];
	}

	printf("Calculated: %d, %d\n", acc1, acc2);
	printf("  Expected: %d, %d\n", 4240, 4220);

	//check
	if ( (acc1 == 4240) && (acc2 == 4220) ) {
		return 0;
	} else {
		return -1;
	}
}
