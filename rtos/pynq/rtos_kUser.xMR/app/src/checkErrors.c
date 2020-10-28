#include "FreeRTOS.h"
#include "PollQ.h"
#include "BlockQ.h"
#include "Print.h"
#include "semtest.h"
#include "mevents.h"
#include "QPeek.h"
#include "checkErrors.h"

/**************************** COAST configuration *****************************/
#include "COAST.h"


#ifdef __FOR_SIM
#define vDisplayMessage(...) ((void)(0))
#else
#define vDisplayMessage(...) xil_printf(__VA_ARGS__)
#endif

static unsigned int errCnt = 0;

static __NO_xMR void (*doneCallback)(void);


/********************************* Functions **********************************/

void reportError( void ) __xMR {
    errCnt += 1;
}
/*-----------------------------------------------------------*/

#ifdef ERR_DBG_PRINT
#include <stdarg.h>

void debugPrint(const char* fmt, ...) {
    va_list argptr;
    va_start(argptr, fmt);
    xil_printf("error in ");
    xil_printf(fmt, argptr);
    xil_printf("\r\n");
    va_end(argptr);
}
/*-----------------------------------------------------------*/
#endif

unsigned int getErrorCount( void ) __xMR {
    unsigned int lastErrCnt = errCnt;
    errCnt = 0;
    return lastErrCnt;
}
/*-----------------------------------------------------------*/

void prvCheckOtherTasksAreStillRunning( void ) __xMR {

    static short sErrorHasOccurred = pdFALSE;

    if( xAreBlockingQueuesStillRunning() != pdTRUE )
	{
		vDisplayMessage( "Blocking queues count unchanged!\r\n" );
		sErrorHasOccurred = pdTRUE;
        errCnt += 1;
	}

	if( xArePollingQueuesStillRunning() != pdTRUE )
	{
		vDisplayMessage( "Polling queue count unchanged!\r\n" );
		sErrorHasOccurred = pdTRUE;
        errCnt += 1;
	}

	if( xAreSemaphoreTasksStillRunning() != pdTRUE )
	{
		vDisplayMessage( "Semaphore take count unchanged!\r\n" );
		sErrorHasOccurred = pdTRUE;
        errCnt += 1;
	}

	if( xAreMultiEventTasksStillRunning() != pdTRUE )
	{
		vDisplayMessage( "Error in multi events tasks!\r\n" );
		sErrorHasOccurred = pdTRUE;
        errCnt += 1;
	}

	if( xAreQueuePeekTasksStillRunning() != pdTRUE )
	{
		vDisplayMessage( "Error in queue peek test task!\r\n" );
		sErrorHasOccurred = pdTRUE;
        errCnt += 1;
	}

    if( sErrorHasOccurred == pdFALSE )
	{
		// vDisplayMessage( "OK\r\n" );
	}
}
/*-----------------------------------------------------------*/

void printTaskCounts( void ) {
    vBlockingQueueCountPrint();
    vPollingQueueCountPrint();
    vSemaphoreCountPrint();
	vMultiEventTasksCountPrint();
	vQueuePeekCountPrint();
}

void setDoneFunction(void (*callback)(void)) __xMR {
    doneCallback = callback;
}

// avoid replicating calls to function pointers
void callCallbackOnce() __NO_xMR __SKIP_FN_CALL {
    doneCallback();
}

void goalReached() __xMR {
    callCallbackOnce();
}
