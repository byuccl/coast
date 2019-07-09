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

int32_t TMR_ERROR_CNT;
unsigned error;

typedef uint32_t mm_t;

#include "mm.inc"

#include "../../mm_common/mm_common_tmr.c"


int main()
{
	init_platform();

	XTime tStart, tEnd;

	printf("Hello there!\n");
	while (1) {
		XTime_GetTime(&tStart);

		for (int i = 0; i < N; i++) {
			mm_run_test();
			error = checkGolden();
			if (TMR_ERROR_CNT || error)
				break;
		}
		XTime_GetTime(&tEnd);

		float t = ((float) (tEnd - tStart)) / COUNTS_PER_SECOND;
		printf("C:%d E:%d F:%d T:%fs\n\r", CORE, error, TMR_ERROR_CNT, t);
	}
    return 0;
}
