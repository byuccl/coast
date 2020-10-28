#include "FreeRTOS.h"
#include "Print.h"
#include "checkErrors.h"

#include "xil_printf.h"

#include "mm.h"

// TODO: fix this
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

// makes sure the tasks ran correctly
void prvCheckOtherTasksAreStillRunning( void ) {

    static short sErrorHasOccurred = pdFALSE;

	if ( xAreMMTasksStillRunning() != pdTRUE ) {
		sErrorHasOccurred = pdTRUE;
		errCnt += 1;
	}
}
/*-----------------------------------------------------------*/

void printTaskCounts( void ) {
	vMMTaskCountPrint();
}

void setDoneFunction(void (*callback)(void)) {
    doneCallback = callback;
}

void goalReached() {
    doneCallback();
}
