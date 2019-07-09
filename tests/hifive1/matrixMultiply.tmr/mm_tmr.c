#include <stdio.h>
#include <stdint.h>

#include "COAST.h"
__DEFAULT_NO_xMR

int32_t TMR_ERROR_CNT;

#include "platform.h"

#define US_PER_S (1000 * 1000)

unsigned error;
typedef uint32_t mm_t;

#include "mm.inc"

#include "../../mm_common/mm_common_tmr.c"

int main() {
    uint32_t timer_freq = (unsigned) ( get_timer_freq() & 0xFFFFFFFF);
    uint32_t t;
    uint64_t t1, t2;

    while (1) {
       	t1 = get_timer_value();
        for (int i = 0; i < 2; i++) {
			mm_run_test();
			error = checkGolden();
			if (error)
				break;
		}
        t2 = get_timer_value();

        t = US_PER_S * (t2 - t1) / (float)timer_freq;

        printf("C:0 E:%d F:%d T:%uus\n", error, TMR_ERROR_CNT, t);

    }
}
