/* task.h — host-sim task API.
 * Tasks are real detached pthreads running the firmware entry points
 * concurrently.  Delays/ticks derive from vboard_now_us(). */
#ifndef INC_TASK_H
#define INC_TASK_H

#include <stdint.h>
#include <stddef.h>

#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

#define tskIDLE_PRIORITY            ((UBaseType_t)0u)
#define tskNO_AFFINITY              ((BaseType_t)0x7FFFFFFF)
#define tskINVALID_INDEX            ((BaseType_t)-1)
#define taskSCHEDULER_SUSPENDED     ((BaseType_t)0)
#define taskSCHEDULER_NOT_STARTED   ((BaseType_t)1)
#define taskSCHEDULER_RUNNING       ((BaseType_t)2)

typedef enum
{
    eRunning = 0,
    eReady,
    eBlocked,
    eSuspended,
    eDeleted,
    eInvalid
} eTaskState;

typedef enum
{
    eNoAction = 0,
    eSetBits,
    eIncrement,
    eSetValueWithOverwrite,
    eSetValueWithoutOverwrite
} eNotifyAction;

typedef struct xTIME_OUT
{
    TickType_t xOverflowCount;
    TickType_t xTimeOnEntering;
} TimeOut_t;

/* ---- task lifecycle ------------------------------------------------ */
BaseType_t xTaskCreate(TaskFunction_t pxTaskCode,
                       const char *pcName,
                       const uint32_t usStackDepth,
                       void *pvParameters,
                       UBaseType_t uxPriority,
                       TaskHandle_t *pxCreatedTask);
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t pxTaskCode,
                                   const char *pcName,
                                   const uint32_t usStackDepth,
                                   void *pvParameters,
                                   UBaseType_t uxPriority,
                                   TaskHandle_t *pxCreatedTask,
                                   const BaseType_t xCoreID);
TaskHandle_t xTaskCreateStatic(TaskFunction_t pxTaskCode,
                               const char *pcName,
                               const uint32_t ulStackDepth,
                               void *pvParameters,
                               UBaseType_t uxPriority,
                               StackType_t *puxStackBuffer,
                               StaticTask_t *pxTaskBuffer);
TaskHandle_t xTaskCreateStaticPinnedToCore(TaskFunction_t pxTaskCode,
                                           const char *pcName,
                                           const uint32_t ulStackDepth,
                                           void *pvParameters,
                                           UBaseType_t uxPriority,
                                           StackType_t *puxStackBuffer,
                                           StaticTask_t *pxTaskBuffer,
                                           const BaseType_t xCoreID);
void       vTaskDelete(TaskHandle_t xTaskToDelete);

/* ---- delays --------------------------------------------------------- */
void       vTaskDelay(const TickType_t xTicksToDelay);
void       vTaskDelayUntil(TickType_t *pxPreviousWakeTime,
                           const TickType_t xTimeIncrement);
BaseType_t xTaskAbortDelay(TaskHandle_t xTask);

/* ---- identity / state ------------------------------------------------ */
TaskHandle_t xTaskGetCurrentTaskHandle(void);
TaskHandle_t xTaskGetHandle(const char *pcNameToQuery);
char        *pcTaskGetName(TaskHandle_t xTaskToQuery);
#define     pcTaskGetTaskName(h)  pcTaskGetName(h)
UBaseType_t  uxTaskPriorityGet(const TaskHandle_t xTask);
UBaseType_t  uxTaskPriorityGetFromISR(const TaskHandle_t xTask);
void         vTaskPrioritySet(TaskHandle_t xTask, UBaseType_t uxNewPriority);
void         vTaskSuspend(TaskHandle_t xTaskToSuspend);
void         vTaskResume(TaskHandle_t xTaskToResume);
BaseType_t   xTaskResumeFromISR(TaskHandle_t xTaskToResume);
eTaskState   eTaskGetState(TaskHandle_t xTask);
#define      eTaskStateGet(h)  eTaskGetState(h)
UBaseType_t  uxTaskGetStackHighWaterMark(TaskHandle_t xTask);
UBaseType_t  uxTaskGetNumberOfTasks(void);
BaseType_t   xTaskGetSchedulerState(void);
TaskHandle_t xTaskGetIdleTaskHandle(void);

/* ---- ticks ------------------------------------------------------------ */
TickType_t   xTaskGetTickCount(void);
TickType_t   xTaskGetTickCountFromISR(void);

/* ---- timeouts ---------------------------------------------------------- */
void         vTaskSetTimeOutState(TimeOut_t *pxTimeOut);
BaseType_t   xTaskCheckForTimeOut(TimeOut_t *pxTimeOut,
                                  TickType_t *pxTicksToWait);

/* ---- scheduler control --------------------------------------------------- */
void         vTaskStartScheduler(void);
void         vTaskEndScheduler(void);
void         vTaskSuspendAll(void);
BaseType_t   xTaskResumeAll(void);
#define      taskSCHEDULER_RUN  taskSCHEDULER_RUNNING

/* ---- task notifications ---------------------------------------------------- */
BaseType_t xTaskNotify(TaskHandle_t xTaskToNotify, uint32_t ulValue,
                       eNotifyAction eAction);
BaseType_t xTaskNotifyAndQuery(TaskHandle_t xTaskToNotify, uint32_t ulValue,
                               eNotifyAction eAction,
                               uint32_t *pulPreviousNotifyValue);
BaseType_t xTaskNotifyFromISR(TaskHandle_t xTaskToNotify, uint32_t ulValue,
                              eNotifyAction eAction,
                              BaseType_t *pxHigherPriorityTaskWoken);
BaseType_t xTaskNotifyAndQueryFromISR(TaskHandle_t xTaskToNotify,
                                      uint32_t ulValue,
                                      eNotifyAction eAction,
                                      uint32_t *pulPreviousNotificationValue,
                                      BaseType_t *pxHigherPriorityTaskWoken);
void       vTaskNotifyGiveFromISR(TaskHandle_t xTaskToNotify,
                                  BaseType_t *pxHigherPriorityTaskWoken);
/* xTaskNotifyGive is a macro in upstream (→ xTaskNotify eIncrement). */
#define    xTaskNotifyGive(h)  xTaskNotify((h), 0, eIncrement)
BaseType_t xTaskNotifyWait(uint32_t ulBitsToClearOnEntry,
                           uint32_t ulBitsToClearOnExit,
                           uint32_t *pulNotificationValue,
                           TickType_t xTicksToWait);
uint32_t   ulTaskNotifyTake(BaseType_t xClearCountOnExit,
                            TickType_t xTicksToWait);
BaseType_t xTaskNotifyStateClear(TaskHandle_t xTask);
uint32_t   ulTaskNotifyValueClear(TaskHandle_t xTask,
                                  uint32_t ulBitsToClear);

/* ---- thread-local storage ------------------------------------------------ */
void  vTaskSetThreadLocalStoragePointer(TaskHandle_t xTaskToSet,
                                        BaseType_t xIndex, void *pvValue);
void *pvTaskGetThreadLocalStoragePointer(TaskHandle_t xTaskToQuery,
                                         BaseType_t xIndex);
void  vTaskSetApplicationTaskTag(TaskHandle_t xTask, TaskHookFunction_t pxHookFunction);
TaskHookFunction_t xTaskGetApplicationTaskTag(TaskHandle_t xTask);
void  vTaskCoreAffinitySet(TaskHandle_t xTask, UBaseType_t uxCoreAffinityMask);
UBaseType_t xTaskGetAffinity(TaskHandle_t xTask);

#ifdef __cplusplus
}
#endif

#endif /* INC_TASK_H */
