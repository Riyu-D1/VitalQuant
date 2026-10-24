/* FreeRTOS.h — host-sim kernel root header.
 *
 * Self-contained umbrella: kernel config, project defs, port layer,
 * opaque handle types and static-allocation blobs.  Provides NOTHING
 * from the real IDF/FreeRTOS trees — the whole surface is the sim's.
 */
#ifndef INC_FREERTOS_H
#define INC_FREERTOS_H

#include <stdint.h>
#include <stddef.h>
#include <assert.h>

#include "freertos/FreeRTOSConfig.h"
#include "freertos/projdefs.h"
#include "freertos/portable.h"
/* ESP port pulls in esp_attr.h transitively (IRAM_ATTR etc.) */
#include "esp_attr.h"

#ifdef __cplusplus
extern "C" {
#endif

#define tskKERNEL_VERSION_NUMBER   "V10.4.3-sim"
#define tskKERNEL_VERSION_MAJOR    10
#define tskKERNEL_VERSION_MINOR    4
#define tskKERNEL_VERSION_BUILD    3

#ifndef configASSERT
#define configASSERT(x)            assert(x)
#endif

/* ---- opaque kernel object handles ----------------------------------
 * These match upstream naming so kernel-aware code compiles as-is;
 * the sim defines the structs in freertos_sim.c. */
struct tskTaskControlBlock;
typedef struct tskTaskControlBlock     *TaskHandle_t;
typedef const struct tskTaskControlBlock *ConstTaskHandle_t;

/* ---- static-allocation blobs ----------------------------------------
 * Generous opaque byte stores; the sim mallocs real objects behind
 * pthread primitives internally (static variants still heap-alloc —
 * sim leak policy). */
typedef struct xSTATIC_TCB        { uint32_t uxDummy[64]; } StaticTask_t;
typedef struct xSTATIC_QUEUE      { uint32_t uxDummy[40]; } StaticQueue_t;
typedef struct xSTATIC_SEMAPHORE  { uint32_t uxDummy[24]; } StaticSemaphore_t;
typedef struct xSTATIC_EVENT_GROUP{ uint32_t uxDummy[24]; } StaticEventGroup_t;
typedef struct xSTATIC_TIMER      { uint32_t uxDummy[24]; } StaticTimer_t;
typedef struct xSTATIC_STREAM_BUFFER { uint32_t uxDummy[24]; } StaticStreamBuffer_t;
typedef struct xSTATIC_MESSAGE_BUFFER{ uint32_t uxDummy[24]; } StaticMessageBuffer_t;
typedef struct xSTATIC_LIST_ITEM  { uint32_t uxDummy[10]; } StaticListItem_t;
typedef struct xSTATIC_MINI_LIST_ITEM { uint32_t uxDummy[10]; } StaticMiniListItem_t;
typedef struct xSTATIC_LIST       { uint32_t uxDummy[20]; } StaticList_t;

/* ---- misc upstream structs ----------------------------------------- */
typedef struct xMEMORY_REGION
{
    void    *pvStartAddress;
    size_t   xSizeInBytes;
} MemoryRegion_t;

typedef struct xTASK_PARAMETERS
{
    TaskFunction_t  pvTaskCode;
    const char     *pcName;
    uint32_t        usStackDepth;
    void           *pvParameters;
    UBaseType_t     uxPriority;
    StackType_t    *puxStackBuffer;
    MemoryRegion_t  xRegions[1];
} TaskParameters_t;

typedef struct xTASK_STATUS
{
    TaskHandle_t  xHandle;
    const char   *pcTaskName;
    UBaseType_t   xTaskNumber;
    int           eCurrentState;
    UBaseType_t   uxCurrentPriority;
    UBaseType_t   uxBasePriority;
    uint32_t      ulRunTimeCounter;
    StackType_t  *pxStackBase;
    uint16_t      usStackHighWaterMark;
    uint16_t      usStackRemaining;
} TaskStatus_t;

typedef enum { eSleepSleep = 0, eSleepAbort, eSleepNoTasksWaitingTimeout } eSleepModeStatus;

/* Hook function typedef used by xTaskCallApplicationTaskHook. */
typedef BaseType_t (*TaskHookFunction_t)(void *);

/* Trace/coverage macros — compiled out in the sim. */
#define mtCOVERAGE_TEST_MARKER()
#define mtCOVERAGE_TEST_DELAY()
#define traceMOVED_TASK_TO_READY_STATE(tcb)
#define tracePOST_MOVED_TASK_TO_READY_STATE(tcb)

#ifdef __cplusplus
}
#endif

#endif /* INC_FREERTOS_H */
