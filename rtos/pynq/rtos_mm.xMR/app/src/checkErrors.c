#include "FreeRTOS.h"
#include "Print.h"
#include "checkErrors.h"

#include "xil_printf.h"

#include "mm.h"


/**************************** COAST configuration *****************************/
#include "COAST.h"


// TODO: fix this
#ifdef __FOR_SIM
#define vDisplayMessage(...) ((void)(0))
#else
#define vDisplayMessage(...) xil_printf(__VA_ARGS__)
#endif


/*********************************** Values ***********************************/
static unsigned int errCnt = 0;

static __NO_xMR void (*doneCallback)(void);


/********************************* Functions **********************************/

void reportError( void ) __xMR {
    errCnt += 1;
}
/*-----------------------------------------------------------*/

unsigned int getErrorCount( void ) __xMR {
    return errCnt;
}
/*-----------------------------------------------------------*/

// makes sure the tasks ran correctly
void prvCheckOtherTasksAreStillRunning( void ) __xMR {

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
