#include <stdio.h>

#include "platform.h"

#define US_PER_S (1000 * 1000)

int main() {
    uint32_t timer_freq = (unsigned) ( get_timer_freq() & 0xFFFFFFFF);
    printf("DBG: Hello World!\n");
    
    while (1) {
       	uint64_t t1 = get_timer_value();
        int i;
        for (i = 0; i < 100000000; i++);
        uint64_t t2 = get_timer_value();        
        
        uint32_t t = US_PER_S * (t2 - t1) / (float)timer_freq;
        
        
        printf("C:0 DD:0 T:%uus\n", t);

    }
}
