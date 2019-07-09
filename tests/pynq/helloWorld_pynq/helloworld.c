#include <stdio.h>
#include <stdint.h>

#include "platform.h"
#include "xil_printf.h"

#include "xtime_l.h"

#ifndef CORE
#define CORE 0
#endif 


int main()
{
	init_platform();

	XTime tStart, tEnd;

	while (1) {
		XTime_GetTime(&tStart);
		printf("DBG: Hello World\n");
		for (int i = 0; i < 100000000; i++);
		XTime_GetTime(&tEnd);

		float t = ((float) (tEnd - tStart)) / COUNTS_PER_SECOND;

		printf("C:%d E:0 F:0 T:%fs\n\r", CORE, t);
	}
    return 0;
}
