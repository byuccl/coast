#ifndef __GLOBAL_H
#define __GLOBAL_H


/********************************** Includes **********************************/
#include "xtime_l.h"


/******************************** Definitions *********************************/
#define US_PER_S    1000000

#ifdef __QEMU_SIM
#include <stdio.h>
#define xil_printf printf
#else
#include "xil_printf.h"
#endif


/********************************* Prototypes *********************************/
void vKillTasksTask( void* pvParameters );


/*********************************** Values ***********************************/
extern XTime tStart;
extern XTime tEnd;


#endif  /* __GLOBAL_H */
