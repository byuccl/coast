#include <stdio.h>
#include <stdint.h>

#include "COAST.h"
__DEFAULT_NO_xMR


#define US_PER_S (1000 * 1000)

unsigned error;
typedef uint32_t mm_t;

#include "sha_data.inc"

#include "sha256_common_tmr.c"

int main() {
    // uint32_t timer_freq = (unsigned) ( get_timer_freq() & 0xFFFFFFFF);
    
    while (1) {


        for (int i = 0; i < 100; i++) {
			sha_run_test();
			error = checkGolden();
			if (error)
				break;
		}
                
        printf("C:0 E:%d F:0 T:%uus\n", error, 0);
        break;

    }
}
