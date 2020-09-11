/*
 * FreeRTOS application that tries to use the kernel functions as much as possible.
 * Adapted from the FreeRTOS Demo application source code.
 */

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"
/* Xilinx includes. */
#include "xparameters.h"
#include "xtime_l.h"

/* C standard library */
#include <stdlib.h>

/* For the application */
#include "PollQ.h"
#include "BlockQ.h"
#include "Print.h"
#include "semtest.h"
#include "mevents.h"
#include "QPeek.h"
#include "checkErrors.h"

/* COAST configuration */
#include "COAST.h"
__DEFAULT_NO_xMR


/******************************** Definitions *********************************/
#define TIMER_ID    1
#define US_PER_S    1000000
#ifdef __QEMU_SIM
#include <stdio.h>
#define xil_printf printf
#endif

// priorities
#define mainSEMAPHORE_TASK_PRIORITY	( tskIDLE_PRIORITY + 1 )
#define mainQUEUE_POLL_PRIORITY		( tskIDLE_PRIORITY + 3 )
#define mainQUEUE_BLOCK_PRIORITY	( tskIDLE_PRIORITY + 2 )


/*********************************** Values ***********************************/
XTime tStart, tEnd;
uint32_t TMR_ERROR_CNT;


/********************************* Prototypes *********************************/
extern void prvCheckOtherTasksAreStillRunning( void );
void vDoneCallback( void );
void vKillTasksTask( void* pvParameters );


/********************************** Handles ***********************************/
/* Configure profiling */
#if ( configGENERATE_RUN_TIME_STATS == 1 )
static char statBuffer[3072];
#endif


/********************************* Functions **********************************/

int main(void) {

#ifndef __FOR_SIM
    xil_printf("\r\n --- Beginning FreeRTOS kernel application --- \r\n");
#endif

    /* start all the things */
    vStartPolledQueueTasks( mainQUEUE_POLL_PRIORITY );
    vStartBlockingQueueTasks( mainQUEUE_BLOCK_PRIORITY );
    vStartSemaphoreTasks( mainSEMAPHORE_TASK_PRIORITY );
    vStartMultiEventTasks();
    vStartQueuePeekTasks();
    vStartPrintingTask();

    setDoneFunction(&vDoneCallback);

#if ( configGENERATE_RUN_TIME_STATS == 0 )
    XTime_GetTime(&tStart);         // Breakpoint here
    // start timing now to make it work with the fault injector
#endif
    /* this starts tasks and timers running */
    vTaskStartScheduler();

    for( ;; );
}
/*-----------------------------------------------------------*/

void printStatus(void) {

#if ( configGENERATE_RUN_TIME_STATS == 1 )
    prvCheckOtherTasksAreStillRunning();
#endif

    XTime_GetTime(&tEnd);

#if ( configGENERATE_RUN_TIME_STATS == 0 )
    uint32_t t = US_PER_S *((float) (tEnd - tStart)) / COUNTS_PER_SECOND;
    uint32_t nErr = getErrorCount();
    xil_printf("C:0 E:%u F:0 T:%uus\r\n", nErr, t);
#endif

    // start the timer again
    XTime_GetTime(&tStart);         // Breakpoint here
}
/*-----------------------------------------------------------*/

void vDoneCallback() {

    vTriggerPrinting();
    return;
}
/*-----------------------------------------------------------*/

/*
 * This task should have the highest priority.
 */
void vKillTasksTask(void* pvParameters) {
    // Just to make sure
    vTaskSuspendAll();

    vEndPolledQueueTasks();
    vEndBlockedQueueTasks();
    vEndSempahoreTasks();
    vEndEventTasks();
    vEndQueuePeekTasks();

    xTaskResumeAll();

    // hang here forever
    for( ;; ) ;
}
