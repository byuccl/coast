/*
 * Code to check and report errors.
 */

#ifndef __CHECK_ERRORS_H
#define __CHECK_ERRORS_H


/******************************** Definitions *********************************/
// verbose printing of when tasks are being killed
// #define VERBOSE_KILL_TASKS

#ifdef __QEMU_SIM
#include <stdio.h>
#define xil_printf printf
#else
#include "xil_printf.h"
#endif


/**************************** Function Prototypes *****************************/

void prvCheckOtherTasksAreStillRunning( void );

void reportError( void );

unsigned int getErrorCount( void );

#ifndef __FOR_SIM
// only enable debug printing when not simulating
#define ERR_DBG_PRINT
#endif

#ifdef ERR_DBG_PRINT    /**********************************************/

#include "xil_printf.h"
void debugPrint(const char* fmt, ...);
#define verboseError(...)   debugPrint(__VA_ARGS__); reportError()

#else
#define verboseError(...)   reportError()
#endif                  /**********************************************/

void printTaskCounts( void );

void setDoneFunction(void (*callback)(void));

void goalReached();


#endif  /* __CHECK_ERRORS_H */
