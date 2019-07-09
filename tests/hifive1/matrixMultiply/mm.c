// standard headers
#include <stdio.h>
#include <stdint.h>

// COAST configuration
#include "COAST.h"
__DEFAULT_NO_xMR

// required for timing functions
#include "platform.h"

// timing conversion
#define US_PER_S (1000 * 1000)

// global variables
unsigned error;
typedef uint32_t mm_t;

// include matrices and the matrix operation functions
#include "mm.inc"
#include "../../mm_common/mm_common.c"


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

        printf("C:0 E:%d F:0 T:%uus\n", error, t);

    }
}
