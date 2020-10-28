/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.h"

/* Xilinx includes. */
#include "xparameters.h"
#include "xtime_l.h"

/* C standard library */
#include <stdlib.h>

/* For the application */
#include "global.h"
#include "checkErrors.h"
#include "mm.h"
#include "Print.h"


/**************************** COAST configuration *****************************/
#include "COAST.h"


/******************************** Definitions *********************************/


/*********************************** Values ***********************************/
XTime tStart, tEnd;


/********************************* Prototypes *********************************/
void vDoneCallback( void );


/********************************** Handles ***********************************/
/* Configure profiling */
#if ( configGENERATE_RUN_TIME_STATS == 1 )
static char statBuffer[3072];
#endif


/********************************* Functions **********************************/

int main( void ) __xMR {

    /* start all the tasks */
    vStartPrintingTask();
    vStartMMTasks();

    setDoneFunction(&vDoneCallback);

    #if ( configGENERATE_RUN_TIME_STATS == 0 )
    // start timing now to make it work with the fault injector
    XTime_GetTime(&tStart);         // Breakpoint here
    #endif

    /* this starts tasks and timers running */
    vTaskStartScheduler();

    // should never get here
    for( ;; );
}


void vDoneCallback() __xMR {

    vTriggerPrinting();
    return;
}


/*
 * Removes all the other tasks.
 * This task should have the highest priority.
 */
void vKillTasksTask(void* pvParameters) __xMR {
    // Just to make sure
    vTaskSuspendAll();

    vEndMMTasks();

    xTaskResumeAll();

    // hang here forever
    for( ;; ) ;
}

/*
 * Testing notes:
 * python3 supervisor.py -f ~/coast/rtos/pynq/build/rtos_mm/rtos_mm.elf -p 3750 -t 10 -s cache -q
 */
