/* queue.h — host-sim queue API.
 * Queues are mutex+condvar protected circular buffers; FromISR variants
 * are real concurrent calls (never block, wake waiting receivers). */
#ifndef INC_QUEUE_H
#define INC_QUEUE_H

#include <stdint.h>
#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

struct QueueDefinition;
typedef struct QueueDefinition   *QueueHandle_t;
typedef struct QueueDefinition   *QueueSetHandle_t;
typedef struct QueueDefinition   *QueueSetMemberHandle_t;

/* Send-position selectors (match upstream values). */
#define queueSEND_TO_BACK           ((BaseType_t)0)
#define queueSEND_TO_FRONT          ((BaseType_t)1)
#define queueOVERWRITE              ((BaseType_t)2)

/* Queue-family return codes. */
#define queueQUEUE_TYPE_BASE        ((uint8_t)0u)
#define errQUEUE_EMPTY              ((BaseType_t)0)
#define errQUEUE_FULL               ((BaseType_t)0)
#define errQUEUE_BLOCKED            (-4)
#define errQUEUE_YIELD              (-5)
#define queuePASS                   pdPASS

QueueHandle_t xQueueCreate(const UBaseType_t uxQueueLength,
                           const UBaseType_t uxItemSize);
QueueHandle_t xQueueCreateStatic(const UBaseType_t uxQueueLength,
                                 const UBaseType_t uxItemSize,
                                 uint8_t *pucQueueStorage,
                                 StaticQueue_t *pxStaticQueue);

/* xQueueSend family are macros in upstream; preserve that here. */
#define xQueueSend(q, pv, wait) \
    xQueueGenericSend((q), (pv), (wait), queueSEND_TO_BACK)
#define xQueueSendToBack(q, pv, wait) \
    xQueueGenericSend((q), (pv), (wait), queueSEND_TO_BACK)
#define xQueueSendToFront(q, pv, wait) \
    xQueueGenericSend((q), (pv), (wait), queueSEND_TO_FRONT)
#define xQueueOverwrite(q, pv) \
    xQueueGenericSend((q), (pv), (TickType_t)0, queueOVERWRITE)

#define xQueueSendToBackFromISR(q, pv, pxw) \
    xQueueGenericSendFromISR((q), (pv), (pxw), queueSEND_TO_BACK)
#define xQueueSendToFrontFromISR(q, pv, pxw) \
    xQueueGenericSendFromISR((q), (pv), (pxw), queueSEND_TO_FRONT)
#define xQueueSendFromISR(q, pv, pxw) \
    xQueueGenericSendFromISR((q), (pv), (pxw), queueSEND_TO_BACK)
#define xQueueOverwriteFromISR(q, pv, pxw) \
    xQueueGenericSendFromISR((q), (pv), (pxw), queueOVERWRITE)

BaseType_t xQueueGenericSend(QueueHandle_t xQueue,
                             const void *pvItemToQueue,
                             TickType_t xTicksToWait,
                             const BaseType_t xCopyPosition);
BaseType_t xQueueGenericSendFromISR(QueueHandle_t xQueue,
                                    const void *pvItemToQueue,
                                    BaseType_t *pxHigherPriorityTaskWoken,
                                    const BaseType_t xCopyPosition);

BaseType_t xQueueReceive(QueueHandle_t xQueue, void *pvBuffer,
                         TickType_t xTicksToWait);
BaseType_t xQueueReceiveFromISR(QueueHandle_t xQueue, void *pvBuffer,
                                BaseType_t *pxHigherPriorityTaskWoken);
BaseType_t xQueuePeek(QueueHandle_t xQueue, void *pvBuffer,
                      TickType_t xTicksToWait);
BaseType_t xQueuePeekFromISR(QueueHandle_t xQueue, void *pvBuffer);

/* Queue-family primitives that upstream semphr.h wraps — exposed for
 * code that uses them directly. */
QueueHandle_t xQueueCreateMutex(const uint8_t ucQueueType);
QueueHandle_t xQueueCreateMutexStatic(const uint8_t ucQueueType,
                                      StaticQueue_t *pxStaticQueue);
QueueHandle_t xQueueCreateRecursiveMutex(void);
QueueHandle_t xQueueCreateCountingSemaphore(const UBaseType_t uxMaxCount,
                                            const UBaseType_t uxInitialCount);
QueueHandle_t xQueueCreateCountingSemaphoreStatic(
    const UBaseType_t uxMaxCount, const UBaseType_t uxInitialCount,
    StaticQueue_t *pxStaticQueue);
BaseType_t xQueueSemaphoreTake(QueueHandle_t xQueue, TickType_t xTicksToWait);
BaseType_t xQueueTakeMutexRecursive(QueueHandle_t xMutex,
                                    TickType_t xTicksToWait);
BaseType_t xQueueGiveMutexRecursive(QueueHandle_t xMutex);
BaseType_t xQueueGiveFromISR(QueueHandle_t xQueue,
                             BaseType_t *pxHigherPriorityTaskWoken);
TaskHandle_t xQueueGetMutexHolder(QueueHandle_t xSemaphore);
TaskHandle_t xQueueGetMutexHolderFromISR(QueueHandle_t xSemaphore);

BaseType_t xQueueReset(QueueHandle_t xQueue);
UBaseType_t uxQueueMessagesWaiting(const QueueHandle_t xQueue);
UBaseType_t uxQueueMessagesWaitingFromISR(const QueueHandle_t xQueue);
UBaseType_t uxQueueSpacesAvailable(const QueueHandle_t xQueue);
BaseType_t  xQueueIsQueueEmptyFromISR(const QueueHandle_t xQueue);
BaseType_t  xQueueIsQueueFullFromISR(const QueueHandle_t xQueue);
void        vQueueDelete(QueueHandle_t xQueue);

#ifdef __cplusplus
}
#endif

#endif /* INC_QUEUE_H */
