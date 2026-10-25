/* semphr.h — host-sim semaphore/mutex API.
 * Binary, counting, mutex and recursive-mutex flavours over the shared
 * queue-family object; FromISR gives are real concurrent calls. */
#ifndef INC_SEMPHR_H
#define INC_SEMPHR_H

#include <stdint.h>
#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef QueueHandle_t SemaphoreHandle_t;

#define semBINARY_SEMAPHORE_QUEUE_LENGTH    ((uint8_t)1u)
#define semSEMAPHORE_QUEUE_ITEM_LENGTH      ((uint8_t)0u)
#define semGIVE_BLOCK_TIME                  ((TickType_t)0u)

/* ---- creates --------------------------------------------------------- */
SemaphoreHandle_t xSemaphoreCreateMutex(void);
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *pxMutexBuffer);
SemaphoreHandle_t xSemaphoreCreateBinary(void);
SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t *pxSemaphoreBuffer);
SemaphoreHandle_t xSemaphoreCreateCounting(const UBaseType_t uxMaxCount,
                                           const UBaseType_t uxInitialCount);
SemaphoreHandle_t xSemaphoreCreateCountingStatic(const UBaseType_t uxMaxCount,
                                                 const UBaseType_t uxInitialCount,
                                                 StaticSemaphore_t *pxSemaphoreBuffer);
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void);
SemaphoreHandle_t xSemaphoreCreateRecursiveMutexStatic(StaticSemaphore_t *pxSemaphoreBuffer);

#define vSemaphoreCreateBinary(xSem) \
    do { (xSem) = xSemaphoreCreateBinary(); } while (0)

/* ---- take / give ------------------------------------------------------ */
BaseType_t xSemaphoreTake(SemaphoreHandle_t xSemaphore,
                          TickType_t xTicksToWait);
BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t xMutex,
                                   TickType_t xTicksToWait);
BaseType_t xSemaphoreGive(SemaphoreHandle_t xSemaphore);
BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t xMutex);
BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t xSemaphore,
                                 BaseType_t *pxHigherPriorityTaskWoken);
BaseType_t xSemaphoreTakeFromISR(SemaphoreHandle_t xSemaphore,
                                 BaseType_t *pxHigherPriorityTaskWoken);

/* ---- misc -------------------------------------------------------------- */
void        vSemaphoreDelete(SemaphoreHandle_t xSemaphore);
UBaseType_t uxSemaphoreGetCount(SemaphoreHandle_t xSemaphore);
TaskHandle_t xSemaphoreGetMutexHolder(SemaphoreHandle_t xMutex);
TaskHandle_t xSemaphoreGetMutexHolderFromISR(SemaphoreHandle_t xMutex);

#ifdef __cplusplus
}
#endif

#endif /* INC_SEMPHR_H */
