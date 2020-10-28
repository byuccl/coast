#include <stdlib.h>
#include <stdint.h>

/* Scheduler include files. */
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

/* Xilinx includes. */
#include "xtime_l.h"

/* Demo program include files. */
#include "Print.h"
#include "global.h"
#include "checkErrors.h"
#include "mm.h"


/**************************** COAST configuration *****************************/
#include "COAST.h"


/******************************** Definitions *********************************/
// run the benchmark forever - used for radiation test
// #define INFINITE_RUN
// #define PRINT_TASK_COUNTS


/*********************************** Values ***********************************/
#define BEGIN_PRINTING (0x57A47)
TaskHandle_t vPrintTaskHandle;
uint32_t TMR_ERROR_CNT;


/********************************* Prototypes *********************************/
void printStatus( void );
void vPrintingTask( void* pvParameters );


/********************************* Functions **********************************/
void vStartPrintingTask() __xMR {
    BaseType_t xReturned = xTaskCreate(
		vPrintingTask,                  /* Function */
		"PRINT",                        /* Name */
		configMINIMAL_STACK_SIZE * 3,   /* Stack size */
		( void * ) 0,                   /* Arguments */
		configMAX_PRIORITIES - 1,       /* Priority */
		&vPrintTaskHandle               /* Handle */
	);
    configASSERT(xReturned == pdPASS);
}


/*
 * Tells the printing task to start printing stuff
 */
void vTriggerPrinting() __xMR {
    BaseType_t xReturned = xTaskNotify(
        vPrintTaskHandle,               /* task */
        BEGIN_PRINTING,                 /* value */
        eSetBits                        /* action */
    );
    configASSERT(xReturned == pdPASS);
    return;
}

void printStatus() __xMR {

    XTime_GetTime(&tEnd);

#if ( configGENERATE_RUN_TIME_STATS == 0 )
    uint32_t t = US_PER_S *((float) (tEnd - tStart)) / COUNTS_PER_SECOND;
    uint32_t nErr = getErrorCount();
    xil_printf("C:0 E:%u F:%u T:%uus\r\n", nErr, TMR_ERROR_CNT, t);
#endif

    // start the timer again
    XTime_GetTime(&tStart);         // Breakpoint here
}

/*
 * This task is reponsible for formatting and printing output to the
 *  serial/UART console.
 * Separate task because it takes a lot of stack space to format output.
 * This will be activated by a semaphore being given from the
 *  vDoneCallback function.
 */
void vPrintingTask( void* pvParameters ) __xMR {
    uint32_t uNotifyVal;
    int counter = 0;

    while (pdTRUE) {
        // wait for a notification
        BaseType_t xReturned = xTaskNotifyWait(
            0,                          /* ulBitsToClearOnEntry */
            BEGIN_PRINTING,             /* ulBitsToClearOnExit */
            &uNotifyVal,                /* pulNotificationValue */
            portMAX_DELAY               /* xTicksToWait */
        );

        // because delay indefinitely, should always return true
        configASSERT(xReturned == pdTRUE);
        // also make sure it was the right value
        configASSERT(uNotifyVal == BEGIN_PRINTING);

        vTaskSuspendAll();

        // tally errors
        prvCheckOtherTasksAreStillRunning();
        // print out stuff
        printStatus();
        #ifdef PRINT_TASK_COUNTS
        printTaskCounts();
        #endif

        #ifndef INFINITE_RUN
        if (counter == 1) {

            #if ( configGENERATE_RUN_TIME_STATS == 1 )
            {
                xil_printf("Application finished!\r\n");
                vTaskGetRunTimeStats((char*) statBuffer);
                xil_printf("%s\r\n", statBuffer);
            }
            #endif
            // start kill task
            xTaskCreate(
                vKillTasksTask,                 /* task function */
                "KILL",                         /* name          */
                configMINIMAL_STACK_SIZE,       /* stack size    */
                NULL,                           /* parameters    */
                configMAX_PRIORITIES - 1,       /* priority      */
                NULL                            /* task handle   */
            );
        }
        #endif
        // clear counts
        vMMTaskCountClear();

        xTaskResumeAll();

        counter++;
    }
}
