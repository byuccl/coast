/*
    FreeRTOS V6.0.4 - Copyright (C) 2010 Real Time Engineers Ltd.

    ***************************************************************************
    *                                                                         *
    * If you are:                                                             *
    *                                                                         *
    *    + New to FreeRTOS,                                                   *
    *    + Wanting to learn FreeRTOS or multitasking in general quickly       *
    *    + Looking for basic training,                                        *
    *    + Wanting to improve your FreeRTOS skills and productivity           *
    *                                                                         *
    * then take a look at the FreeRTOS eBook                                  *
    *                                                                         *
    *        "Using the FreeRTOS Real Time Kernel - a Practical Guide"        *
    *                  http://www.FreeRTOS.org/Documentation                  *
    *                                                                         *
    * A pdf reference manual is also available.  Both are usually delivered   *
    * to your inbox within 20 minutes to two hours when purchased between 8am *
    * and 8pm GMT (although please allow up to 24 hours in case of            *
    * exceptional circumstances).  Thank you for your support!                *
    *                                                                         *
    ***************************************************************************

    This file is part of the FreeRTOS distribution.

    FreeRTOS is free software; you can redistribute it and/or modify it under
    the terms of the GNU General Public License (version 2) as published by the
    Free Software Foundation AND MODIFIED BY the FreeRTOS exception.
    ***NOTE*** The exception to the GPL is included to allow you to distribute
    a combined work that includes FreeRTOS without being obliged to provide the
    source code for proprietary components outside of the FreeRTOS kernel.
    FreeRTOS is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
    more details. You should have received a copy of the GNU General Public
    License and the FreeRTOS license exception along with FreeRTOS; if not it
    can be viewed here: http://www.freertos.org/a00114.html and also obtained
    by writing to Richard Barry, contact details for whom are available on the
    FreeRTOS WEB site.

    1 tab == 4 spaces!

    http://www.FreeRTOS.org - Documentation, latest information, license and
    contact details.

    http://www.SafeRTOS.com - A version that is certified for use in safety
    critical systems.

    http://www.OpenRTOS.com - Commercial support, development, porting,
    licensing and training services.
*/

#if 0
// Don't need this stuff because we are doing printing differently.
// But keep it around because history.
/**
 * Manages a queue of strings that are waiting to be displayed.  This is used to
 * ensure mutual exclusion of console output.
 *
 * A task wishing to display a message will call vPrintDisplayMessage (), with a
 * pointer to the string as the parameter. The pointer is posted onto the
 * xPrintQueue queue.
 *
 * The task spawned in main. c blocks on xPrintQueue.  When a message becomes
 * available it calls pcPrintGetNextMessage () to obtain a pointer to the next
 * string, then uses the functions defined in the portable layer FileIO. c to
 * display the message.
 *
 * <b>NOTE:</b>
 * Using console IO can disrupt real time performance - depending on the port.
 * Standard C IO routines are not designed for real time applications.  While
 * standard IO is useful for demonstration and debugging an alternative method
 * should be used if you actually require console IO as part of your application.
 *
 * \page PrintC print.c
 * \ingroup DemoFiles
 * <HR>
 */

/*
Changes from V2.0.0

	+ Delay periods are now specified using variables and constants of
	  portTickType rather than unsigned long.
*/
#endif

#include <stdlib.h>

/* Scheduler include files. */
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

/* Demo program include files. */
#include "Print.h"
#include "checkErrors.h"
#include "BlockQ.h"
#include "PollQ.h"
#include "semtest.h"
#include "mevents.h"
#include "QPeek.h"


/**************************** COAST configuration *****************************/
#include "COAST.h"


/******************************** Definitions *********************************/
// run the benchmark forever - used for radiation test
// #define INFINITE_RUN
// print out data counts of tasks
// #define PRINT_TASK_COUNTS


/*********************************** Values ***********************************/
#define BEGIN_PRINTING (0x57A47)
TaskHandle_t vPrintTaskHandle;


/********************************* Prototypes *********************************/
void vPrintingTask( void* pvParameters );
extern void vKillTasksTask( void* pvParameters );
extern void printStatus( void );


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
        vBlockingQueueCountClear();
        vPollingQueueCountClear();
        vSemaphoreCountClear();
        vMultiEventTasksCountClear();
        vQueuePeekCountClear();

        xTaskResumeAll();

        counter++;
    }
}


#if 0
// old stuff
static __NO_xMR xQueueHandle xPrintQueue;

/*-----------------------------------------------------------*/

void vPrintInitialise( void )
{
const unsigned portBASE_TYPE uxQueueSize = 20;

	/* Create the queue on which errors will be reported. */
	xPrintQueue = xQueueCreate( uxQueueSize, ( unsigned portBASE_TYPE ) sizeof( char * ) );
}
/*-----------------------------------------------------------*/

void vPrintDisplayMessage( const char * const * ppcMessageToSend )
{
	#ifdef USE_STDIO
		xQueueSend( xPrintQueue, ( void * ) ppcMessageToSend, ( portTickType ) 0 );
	#else
    	/* Stop warnings. */
		( void ) ppcMessageToSend;
	#endif
}
/*-----------------------------------------------------------*/

const char *pcPrintGetNextMessage( portTickType xPrintRate )
{
char *pcMessage;

	if( xQueueReceive( xPrintQueue, &pcMessage, xPrintRate ) == pdPASS )
	{
		return pcMessage;
	}
	else
	{
		return NULL;
	}
}
#endif
