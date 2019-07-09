#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "platform.h"
#include "xil_printf.h"

#include "xtime_l.h"

#ifndef CORE
#define CORE 0
#endif

#include "../../COAST.h"
__DEFAULT_NO_xMR

#define SIMULATED
#ifdef SIMULATED
#define N 2
#else
#define N 1000
#endif

unsigned error;

typedef uint32_t mm_t;

#include "mm.inc"

#include "../../mm_common/mm_common.c"

int main()
{
	init_platform();

	XTime tStart, tEnd;

	// For some reason, printing doesn't work without this here
	printf("Hello there!\n");
	while (1) {
		XTime_GetTime(&tStart);

		for (int i = 0; i < N; i++) {
			mm_run_test();
			error = checkGolden();
			if (error)
				break;
		}
		XTime_GetTime(&tEnd);

		float t = ((float) (tEnd - tStart)) / COUNTS_PER_SECOND;
		printf("C:%d E:%d F:0 T:%fs\n\r", CORE, error, t);
	}
    return 0;
}
