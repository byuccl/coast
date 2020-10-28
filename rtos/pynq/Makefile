# Access the rest of the Makefile system
LEVEL = ../../tests

###############################
# COAST options
###############################

# rtos_kUser.xMR
KUSER_SKIP_LIBS		:= pvPortMalloc,vPortRestoreTaskContext,printf,Xil_Assert
KUSER_REPL_FNS		:= xTaskGetCurrentTaskHandle,xQueueGenericCreate,xQueueCreateMutex,xQueueCreateCountingSemaphore,xQueueCreateSet,xEventGroupCreate
KUSER_NO_xMR_FN		:= pvPortMalloc,vPortFree,FreeRTOS_Tick_Handler,vPortEnterCritical,vPortExitCritical,Xil_Assert
KUSER_NO_xMR_GLBL	:= ulPortYieldRequired,ulPortTaskHasFPUContext,ulCriticalNesting,xMinimumEverFreeBytesRemaining,xBlockAllocatedBit,xFreeBytesRemaining,pxEnd,xStart,XExc_VectorTable,Xil_AssertStatus,Xil_AssertCallbackRoutine
KUSER_YES_xMR_FN	:= vPortEnterCritical,vPortExitCritical,vTaskSuspend,vTaskDelete,prvIdleTask,xTaskResumeFromISR,xTaskGenericNotifyFromISR,vTaskNotifyGiveFromISR,vDoneCallback,xTaskIncrementTick,vTaskSuspendAll,xTaskResumeAll,xQueueReceive,xEventGroupWaitBits,xQueueSemaphoreTake,xTaskNotifyWait,xTaskGenericNotify,vTaskSwitchContext,prvTimerTask,xTimerCreateTimerTask,xTaskCreate,xTaskRemoveFromEventList,prvAddCurrentTaskToDelayedList,vTaskStartScheduler,prvUnlockQueue,vTaskRemoveFromUnorderedEventList,xTaskPriorityDisinherit,xQueueGenericSend
KUSER_YES_xMR_GLBL	:=
KUSER_REPL_RET_FN   := xTimerCreate,pcTaskGetName
KUSER_CLN_AFT_CALL  := XTime_GetTime

# rtos_kUser.app.xMR
KUSER_APP_NO_xMR_FN 	:= pvPortMalloc,vPortFree,FreeRTOS_Tick_Handler,vPortEnterCritical,vPortExitCritical,Xil_Assert,vApplicationStackOverflowHook,xEventGroupSetBits,vTaskRemoveFromUnorderedEventList,xTaskRemoveFromEventList,xQueueGenericCreate,xQueueGenericSend,xTaskPriorityDisinherit,prvNotifyQueueSetContainer,prvAddCurrentTaskToDelayedList,prvUnlockQueue,xQueueSemaphoreTake,xTaskPriorityInherit,vTaskPriorityDisinheritAfterTimeout,xQueueGenericSendFromISR,xQueueReceive,vTaskDelete,prvSampleTimeNow,xTimerCreateTimerTask,XScuGic_Enable,XScuGic_SetPriorityTriggerType,XScuTimer_CfgInitialize,XScuGic_CfgInitialize,XScuGic_Stop,XScuGic_Connect,vTaskDelay,vTaskSuspend,vTaskResume,xQueuePeek,uxQueueMessagesWaiting
KUSER_APP_NO_xMR_GLBL 	:= xQueueRegistry,ulCriticalNesting,ulPortYieldRequired,ulPortInterruptNesting,pxCurrentTCB,uxSchedulerSuspended,xYieldPending,uxTopReadyPriority,pxReadyTasksLists,xTimer,xTickCount,pxDelayedTaskList,pxOverflowDelayedTaskList,xNumOfOverflows,xNextTaskUnblockTime,uxPendedTicks,xPendingReadyList,pxEnd,xStart,xMinimumEverFreeBytesRemaining,xFreeBytesRemaining,xBlockAllocatedBit,uxCurrentNumberOfTasks,xSchedulerRunning,xSuspendedTaskList,xTimerQueue,xDelayedTaskList1,xDelayedTaskList2,xTasksWaitingTermination,uxTaskNumber,uxDeletedTasksWaitingCleanUp,xActiveTimerList1,xActiveTimerList2,pxCurrentTimerList,pxOverflowTimerList,CpuId,Xil_AssertWait,Xil_AssertStatus,UndefinedExceptionAddr,XExc_VectorTable,Xil_AssertCallbackRoutine

# rtos_mm.xMR
MM_NO_xMR_FN	:= pvPortMalloc,vPortFree,FreeRTOS_Tick_Handler,Xil_Assert
MM_YES_xMR_FN	:= vPortEnterCritical,vPortExitCritical,vTaskDelete,prvIdleTask,vDoneCallback,xTaskIncrementTick,vTaskSuspendAll,xTaskResumeAll,xQueueReceive,xEventGroupWaitBits,xTaskNotifyWait,xTaskGenericNotify,vTaskSwitchContext,prvTimerTask,xTimerCreateTimerTask,xTaskCreate,xTaskRemoveFromEventList,prvAddCurrentTaskToDelayedList,vTaskStartScheduler,prvUnlockQueue,vTaskRemoveFromUnorderedEventList,xTaskPriorityDisinherit,xQueueGenericSend,xTaskPriorityInherit,vTaskPriorityDisinheritAfterTimeout,xQueueSemaphoreTake
MM_NO_xMR_GLBL	:= ulPortYieldRequired,ulPortTaskHasFPUContext,ulCriticalNesting,xMinimumEverFreeBytesRemaining,xBlockAllocatedBit,xFreeBytesRemaining,pxEnd,xStart,XExc_VectorTable,Xil_AssertStatus,Xil_AssertCallbackRoutine,UndefinedExceptionAddr
MM_CLN_AFT_CALL := XTime_GetTime

# rtos_mm.app.xMR
MM_APP_NO_xMR_FN 	:= pvPortMalloc,vPortFree,FreeRTOS_Tick_Handler,vPortEnterCritical,vPortExitCritical,Xil_Assert,vApplicationStackOverflowHook,xEventGroupSetBits,vTaskRemoveFromUnorderedEventList,xTaskRemoveFromEventList,xQueueGenericCreate,xQueueGenericSend,xTaskPriorityDisinherit,prvNotifyQueueSetContainer,prvAddCurrentTaskToDelayedList,prvUnlockQueue,xQueueSemaphoreTake,xTaskPriorityInherit,vTaskPriorityDisinheritAfterTimeout,xQueueGenericSendFromISR,xQueueReceive,vTaskDelete,prvSampleTimeNow,xTimerCreateTimerTask,XScuGic_Enable,XScuGic_SetPriorityTriggerType,XScuTimer_CfgInitialize,XScuGic_CfgInitialize,XScuGic_Stop,XScuGic_Connect,xTaskGenericNotify,xTaskNotifyWait
MM_APP_NO_xMR_GLBL 	:= xQueueRegistry,ulCriticalNesting,ulPortYieldRequired,ulPortInterruptNesting,pxCurrentTCB,uxSchedulerSuspended,xYieldPending,uxTopReadyPriority,pxReadyTasksLists,xTimer,xTickCount,pxDelayedTaskList,pxOverflowDelayedTaskList,xNumOfOverflows,xNextTaskUnblockTime,uxPendedTicks,xPendingReadyList,pxEnd,xStart,xMinimumEverFreeBytesRemaining,xFreeBytesRemaining,xBlockAllocatedBit,uxCurrentNumberOfTasks,xSchedulerRunning,xSuspendedTaskList,xTimerQueue,xDelayedTaskList1,xDelayedTaskList2,xTasksWaitingTermination,uxTaskNumber,uxDeletedTasksWaitingCleanUp,xActiveTimerList1,xActiveTimerList2,pxCurrentTimerList,pxOverflowTimerList,CpuId,Xil_AssertWait,Xil_AssertStatus,UndefinedExceptionAddr,XExc_VectorTable,Xil_AssertCallbackRoutine
MM_APP_CLN_AFT_CALL := XTime_GetTime

# common for xMR versions
OPT_PASSES_COMMON   := -TMR -countErrors
EXTRA_COAST_FLAGS   ?=
# uncomment this to emit inline information
# INLINE_REMARKS      := 1

# other flags
OPT_PASSES ?=
OPT_FLAGS =
# COAST_VERBOSE := 1
# PROTECT_STACK := 1
QEMU_PROFILE ?=

# project information
TARGET 		?= rtos_kUser
KERNEL_DIRS := $(sort $(dir $(wildcard $(TARGET)/kernel/*/src/)))
USER_CFLAGS := -g3
USER_DEFS	:=
SRC_DIRS 	:= $(TARGET)/app/src
INC_DIRS	:= $(LEVEL) $(TARGET)/app/include $(TARGET)/kernel/include $(KERNEL_DIRS)
PROJ_LIBS 	:=
# LIB_DIR		:= $(BSP_DIR)
BUILD_DIR	:= build/$(TARGET)

# if running with an emulator (QEMU), make sure this is defined
BUILD_FOR_SIMULATOR := 1
ifneq ($(BUILD_FOR_SIMULATOR),)
USER_DEFS	+= __FOR_SIM=1 __QEMU_SIM=1
USER_CFLAGS += -g3
endif

# get the compiler and optimizer to emit information about which functions are inlined
ifneq ($(INLINE_REMARKS),)
OPT_FLAGS   += -pass-remarks=inline
USER_CFLAGS += -Rpass=inline
endif

BOARD = pynqrtos

# automatically add flags to `opt` based on the benchmark name
ifeq ($(TARGET),rtos_kUser.xMR)
OPT_PASSES += $(OPT_PASSES_COMMON) $(EXTRA_COAST_FLAGS)
OPT_PASSES += -skipLibCalls=$(KUSER_SKIP_LIBS) -replicateFnCalls=$(KUSER_REPL_FNS)
OPT_PASSES += -ignoreFns=$(KUSER_NO_xMR_FN) -cloneFns=$(KUSER_YES_xMR_FN)
OPT_PASSES += -ignoreGlbls=$(KUSER_NO_xMR_GLBL) -cloneReturn=$(KUSER_REPL_RET_FN)
OPT_PASSES += -cloneAfterCall=$(KUSER_CLN_AFT_CALL)

else ifeq ($(TARGET),rtos_kUser.app.xMR)
OPT_PASSES += $(OPT_PASSES_COMMON) $(EXTRA_COAST_FLAGS)
OPT_PASSES += -ignoreFns=$(KUSER_APP_NO_xMR_FN) -ignoreGlbls=$(KUSER_APP_NO_xMR_GLBL)

else ifeq ($(TARGET),rtos_mm.xMR)
OPT_PASSES += $(OPT_PASSES_COMMON) $(EXTRA_COAST_FLAGS)
OPT_PASSES += -ignoreFns=$(MM_NO_xMR_FN) -cloneFns=$(MM_YES_xMR_FN)
OPT_PASSES += -ignoreGlbls=$(MM_NO_xMR_GLBL) -cloneAfterCall=$(MM_CLN_AFT_CALL)

else ifeq ($(TARGET),rtos_mm.app.xMR)
OPT_PASSES += $(OPT_PASSES_COMMON) $(EXTRA_COAST_FLAGS)
OPT_PASSES += -ignoreFns=$(MM_APP_NO_xMR_FN) -ignoreGlbls=$(MM_APP_NO_xMR_GLBL)
OPT_PASSES += -cloneAfterCall=$(MM_APP_CLN_AFT_CALL)

endif

# some stuff for MxM if we want to choose a different size for the matrices
ifneq ($(MATRIX_SIZE_L1_CACHE),)
USER_DEFS += MATRIX_SIZE_L1_CACHE=1
else ifneq ($(MATRIX_SIZE_L2_CACHE),)
USER_DEFS += MATRIX_SIZE_L2_CACHE=1
endif

# add verbosity flags
ifneq ($(OPT_PASSES),)
 ifneq ($(COAST_VERBOSE),)
  OPT_PASSES += -verbose
 endif
#  ifneq ($(PROTECT_STACK),)
#   OPT_PASSES += -protectStack
#   USER_DEFS  += PROTECT_STACK=1
#  endif
endif

include $(LEVEL)/makefiles/Makefile.common
