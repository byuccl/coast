#include "FreeRTOS.h"
#include "PollQ.h"
#include "BlockQ.h"
#include "Print.h"
#include "semtest.h"
#include "mevents.h"
#include "QPeek.h"
#include "checkErrors.h"

#ifdef __FOR_SIM
#define vDisplayMessage(...) ((void)(0))
#else
#define vDisplayMessage(...) xil_printf(__VA_ARGS__)
#endif

static unsigned int errCnt = 0;

static void (*doneCallback)(void);


/********************************* Functions **********************************/

void reportError( void ) {
    errCnt += 1;
}
/*-----------------------------------------------------------*/

unsigned int getErrorCount( void ) {
    return errCnt;
}
/*-----------------------------------------------------------*/

void prvCheckOtherTasksAreStillRunning( void ) {

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
		vDisplayMessage( "OK\r\n" );
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

void setDoneFunction(void (*callback)(void)) {
    doneCallback = callback;
}

void goalReached() {
    doneCallback();
}
