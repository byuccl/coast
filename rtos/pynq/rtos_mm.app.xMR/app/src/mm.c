/* C stdlib includes */
#include <stdint.h>
#include <stdlib.h>

/* Xilinx includes */
#include "xil_printf.h"

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.h"

/* For the application */
#include "mm.h"
#include "checkErrors.h"
#include "global.h"


/**************************** COAST configuration *****************************/
#include "COAST.h"

/* We need to create more than one copy of the results matrix. */
MALLOC_WRAPPER_REGISTER(pvPortMalloc);
/* Also make sure that we free all the copies. */
void GENERIC_COAST_WRAPPER(vPortFree)(void *pv);


/******************************** Definitions *********************************/
#define NUM_MM_TASKS (2)
#define MM_STACK_SIZE (configMINIMAL_STACK_SIZE)
#define MM_TASK_PRIORITY (tskIDLE_PRIORITY + 1)

// Which size matrix are we going to use?
// matrix and golden definitions included in the following file
#if defined(MATRIX_SIZE_L1_CACHE)
#include "mm_L1.inc"
#elif defined(MATRIX_SIZE_L2_CACHE)
#warning Make sure the FreeRTOS heap size has been increased
#include "mm_L2.inc"
#else
// default size, same as was used in the radtest2019 benchmark for L1 size
#include "mm.inc"
#endif

// Original benchmark on pynq ran 1000 times
// Lower the number here to decrease run-time for more testing
// And we spread the work across all the tasks
#define GOAL_VAL (100 / NUM_MM_TASKS)

// data type of the arrays
typedef uint32_t mm_t;
// define array type
// https://stackoverflow.com/a/1052837/12940429
typedef mm_t (*mm_array_ptr_t)[side];
// parameters for the matrix tasks
typedef struct MM_TASK_PARAMS {
    mm_array_ptr_t m1;          /* first matrix */
    mm_array_ptr_t m2;          /* second matrix */
    mm_array_ptr_t results;     /* results matrix */
    uint32_t golden;            /* golden hash value */
    short* psCheckVariable;     /* counter for the task */
} mm_task_param_t;


/********************************* Prototypes *********************************/
void vMMTask( void* pvParameters );
void matrix_multiply(mm_t f_matrix[][side],
        mm_t s_matrix[][side], mm_t r_matrix[][side]);
int checkGolden(mm_t results_matrix[][side], uint32_t xor_golden);


/*********************************** Values ***********************************/
// parameters
static mm_task_param_t allMMParams[NUM_MM_TASKS];
// task handles
static TaskHandle_t __NO_xMR mmTaskHandles[NUM_MM_TASKS];
// counters to make sure tasks are running
static short sLastMMCount[NUM_MM_TASKS];
static short sCurMMCount[NUM_MM_TASKS];


/********************************* Functions **********************************/

// function which does the multiplication
void matrix_multiply(mm_t f_matrix[side][side],
        mm_t s_matrix[side][side], mm_t r_matrix[side][side]) __xMR
{
	int i = 0;
	int j = 0;
	int k = 0;
	unsigned long sum = 0;

	for ( i = 0 ; i < side ; i++ ) {
		for ( j = 0 ; j < side ; j++ ) {
			for ( k = 0 ; k < side ; k++ ) {
				sum = sum + f_matrix[i][k]*s_matrix[k][j];
			}

			r_matrix[i][j] = sum;
			sum = 0;
		}
	}
}

// compute XOR of matrix and see if it matches
__attribute__((noinline))
int checkGolden(mm_t results_matrix[side][side], uint32_t xor_golden) __xMR {
	unsigned int xor = 0;
	unsigned int i, j;

	for(i=0; i<side; i++)
		for (j = 0; j < side; j++)
			xor ^= results_matrix[i][j];

	return (xor != xor_golden);
}


void vStartMMTasks( void ) __xMR {
    // set up parameters
    mm_task_param_t* p0 = &allMMParams[0];
    p0->m1 = first_matrix;
    p0->m2 = second_matrix;
    // allocate space for the results matrix
    uint32_t matrixSizeBytes = sizeof(mm_t) * (side * side);
    // p0->results = (mm_array_ptr_t)pvPortMalloc(matrixSizeBytes);
    p0->results = (mm_array_ptr_t)
            MALLOC_WRAPPER_CALL(pvPortMalloc, (matrixSizeBytes) );
    p0->golden = xor_golden;
    // set the count variable
    p0->psCheckVariable = &( sCurMMCount[0] );

    // create task
    BaseType_t xReturned = xTaskCreate(
        vMMTask,                        /* function */
        "MM0",                          /* name */
        MM_STACK_SIZE,                  /* stack size */
        ( void* ) 0,                    /* parameters */
        MM_TASK_PRIORITY,               /* priority */
        &mmTaskHandles[0]               /* handle */
    );
    configASSERT(xReturned == pdPASS);

    // the rest of the tasks
    int taskNum;
    for (taskNum = 1; taskNum < NUM_MM_TASKS; taskNum++) {
        mm_task_param_t* p = &allMMParams[taskNum];
        p->m1 = first_matrix;
        p->m2 = second_matrix;
        p->results = (mm_array_ptr_t)
                MALLOC_WRAPPER_CALL(pvPortMalloc, (matrixSizeBytes) );
        p->golden = xor_golden;
        p->psCheckVariable = &( sCurMMCount[taskNum] );

        // TODO: names
        xReturned = xTaskCreate(
                vMMTask, "MM1", MM_STACK_SIZE,
                (void*) taskNum, MM_TASK_PRIORITY,
                &mmTaskHandles[taskNum]);
        configASSERT(xReturned == pdPASS);
    }

    // init count arrays
    memset(sLastMMCount, 0, sizeof(short) * NUM_MM_TASKS);
    memset(sCurMMCount, 0, sizeof(short) * NUM_MM_TASKS);
}


// delete all the MxM tasks
void vEndMMTasks( void ) __xMR {
    int i;
    for (i = 0; i < NUM_MM_TASKS; i+=1) {
        // task handles
        vTaskDelete(mmTaskHandles[i]);
        // results matrices
        GENERIC_COAST_WRAPPER(vPortFree)(allMMParams[i].results);
    }
}


void vMMTask( void* pvParameters ) __xMR {
    int status;
    int doReportFlag = 0;

    // unpack parameters
    uint32_t paramIdx = (uint32_t)pvParameters;
    mm_task_param_t* params = (mm_task_param_t*) &allMMParams[paramIdx];

    // only one of the tasks should report
    if (paramIdx == 0) {
        doReportFlag = 1;
    }

    while (1) {
        // run test
        matrix_multiply(params->m1, params->m2, params->results);

        // check golden
        status = checkGolden(params->results, params->golden);

        if (status) {
            // report error
            reportError();
        } else {
            // report success
            (*params->psCheckVariable) += 1;
        }

        // yield here, let another task take a turn
        // they're all the same priority, so the scheduler should play nice
        taskYIELD();

        // report done
        if ( doReportFlag && ((*params->psCheckVariable) >= GOAL_VAL) ) {
            goalReached();
        }
    }
}


/*
 * Functions to do with keeping track of each running task
 */
uint32_t xAreMMTasksStillRunning( void ) __xMR {
    uint32_t xReturn = pdPASS;
    uint32_t xTaskNum;

    for (xTaskNum = 0; xTaskNum < NUM_MM_TASKS; xTaskNum++) {
        // if they're the same, that's an error
        if (sCurMMCount[xTaskNum] == sLastMMCount[xTaskNum]) {
            xReturn = pdFALSE;
        }
        // update the count
        sLastMMCount[xTaskNum] = sCurMMCount[xTaskNum];
    }

    return xReturn;
}

void vMMTaskCountClear( void ) __xMR {
    uint32_t xTaskNum;

    for (xTaskNum = 0; xTaskNum < NUM_MM_TASKS; xTaskNum++) {
        sCurMMCount[xTaskNum] = 0;
        sLastMMCount[xTaskNum] = 0;
    }

    return;
}

void vMMTaskCountPrint( void ) __xMR {
    uint32_t xTaskNum;

    for (xTaskNum = 0; xTaskNum < NUM_MM_TASKS; xTaskNum++) {
        xil_printf("MM%d: %d\r\n", xTaskNum, sCurMMCount[xTaskNum]);
    }

    return;
}
