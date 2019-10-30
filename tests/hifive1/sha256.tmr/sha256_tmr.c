#include <stdio.h>
#include <stdint.h>

#include "COAST.h"
__DEFAULT_NO_xMR

#define SIMULATED
#ifdef SIMULATED
#define N 2
#else
#define N 100
#endif

#include "platform.h"

#define US_PER_S (1000 * 1000)

unsigned error;
typedef uint32_t mm_t;
unsigned TMR_ERROR_CNT;

#include "sha_data.inc"

#include "../../sha256_common/sha256_common_tmr.c"

int main() {
    uint32_t timer_freq = (unsigned) ( get_timer_freq() & 0xFFFFFFFF);

    while (1) {
       	uint64_t t1 = get_timer_value();

        for (int i = 0; i < N; i++) {
			sha_run_test();
			error = checkGolden();
			if (error)
				break;
		}
        uint64_t t2 = get_timer_value();

        uint32_t t = US_PER_S * (t2 - t1) / (float)timer_freq;

        printf("C:0 E:%u F:%u T:%uus\n", error, TMR_ERROR_CNT, t);

    }
}
