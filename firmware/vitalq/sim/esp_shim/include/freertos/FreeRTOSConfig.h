/* FreeRTOSConfig.h — host-sim kernel configuration.
 * Mirrors the IDF settings the firmware was built against (sdkconfig.h). */
#ifndef INC_FREERTOS_CONFIG_H
#define INC_FREERTOS_CONFIG_H

#define configTICK_RATE_HZ                      1000
#define configUSE_PREEMPTION                    1
#define configUSE_TIME_SLICING                  1
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1
#define configMAX_PRIORITIES                    25
#define configMINIMAL_STACK_SIZE                768
#define configMAX_TASK_NAME_LEN                 16
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_COUNTING_SEMAPHORES           1
#define configUSE_TASK_NOTIFICATIONS            1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES   1
#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               3
#define configTIMER_QUEUE_LENGTH                10
#define configTIMER_TASK_STACK_DEPTH            3072
#define configSUPPORT_STATIC_ALLOCATION         1
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configUSE_TRACE_FACILITY                1
#define configUSE_STATS_FORMATTING_FUNCTIONS    1
#define configGENERATE_RUN_TIME_STATS           0
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configUSE_NEWLIB_REENTRANT              0
#define configUSE_CO_ROUTINES                   0
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS 4
#define configSTACK_DEPTH_TYPE                  uint32_t
#define configUSE_APPLICATION_TASK_TAG          1
#define configUSE_QUEUE_SETS                    1
#define configUSE_TASK_FPU_SUPPORT              1

#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_xResumeFromISR                  1
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_uxTaskGetStackHighWaterMark     1
#define INCLUDE_xTaskGetIdleTaskHandle          1
#define INCLUDE_eTaskGetState                   1
#define INCLUDE_xTimerPendFunctionCall          1
#define INCLUDE_xTaskAbortDelay                 1
#define INCLUDE_xTaskGetHandle                  1
#define INCLUDE_xSemaphoreGetMutexHolder        1
#define INCLUDE_xTaskResumeFromISR              1
#define INCLUDE_pcTaskGetTaskName               1

#endif /* INC_FREERTOS_CONFIG_H */
