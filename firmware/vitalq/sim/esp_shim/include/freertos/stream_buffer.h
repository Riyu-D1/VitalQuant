/* stream_buffer.h — host-sim stream buffers (byte streams).
 * Implemented for real (small): mutex+condvar ring buffer.  Firmware
 * does not use it today; correctness kept anyway. */
#ifndef INC_STREAM_BUFFER_H
#define INC_STREAM_BUFFER_H

#include <stdint.h>
#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

struct StreamBufferDef_t;
typedef struct StreamBufferDef_t   *StreamBufferHandle_t;

#define sbFLAGS_MESSAGE_BUFFER_LENGTH_BYTES   ((size_t)sizeof(size_t))

StreamBufferHandle_t xStreamBufferCreate(size_t xBufferSizeBytes,
                                         size_t xTriggerLevelBytes);
StreamBufferHandle_t xStreamBufferCreateStatic(size_t xBufferSizeBytes,
                                               size_t xTriggerLevelBytes,
                                               uint8_t *pucStreamBufferStorageArea,
                                               StaticStreamBuffer_t *pxStaticStreamBuffer);
void   vStreamBufferDelete(StreamBufferHandle_t xStreamBuffer);

size_t xStreamBufferSend(StreamBufferHandle_t xStreamBuffer,
                         const void *pvTxData, size_t xDataLengthBytes,
                         TickType_t xTicksToWait);
size_t xStreamBufferSendFromISR(StreamBufferHandle_t xStreamBuffer,
                                const void *pvTxData, size_t xDataLengthBytes,
                                BaseType_t *pxHigherPriorityTaskWoken);
size_t xStreamBufferReceive(StreamBufferHandle_t xStreamBuffer,
                            void *pvRxData, size_t xBufferLengthBytes,
                            TickType_t xTicksToWait);
size_t xStreamBufferReceiveFromISR(StreamBufferHandle_t xStreamBuffer,
                                   void *pvRxData, size_t xBufferLengthBytes,
                                   BaseType_t *pxHigherPriorityTaskWoken);

BaseType_t xStreamBufferReset(StreamBufferHandle_t xStreamBuffer);
BaseType_t xStreamBufferSetTriggerLevel(StreamBufferHandle_t xStreamBuffer,
                                        size_t xTriggerLevel);
size_t xStreamBufferBytesAvailable(StreamBufferHandle_t xStreamBuffer);
size_t xStreamBufferSpacesAvailable(StreamBufferHandle_t xStreamBuffer);
BaseType_t xStreamBufferIsEmpty(StreamBufferHandle_t xStreamBuffer);
BaseType_t xStreamBufferIsFull(StreamBufferHandle_t xStreamBuffer);
BaseType_t xStreamBufferSendCompleted(StreamBufferHandle_t xStreamBuffer);
BaseType_t xStreamBufferReceiveCompleted(StreamBufferHandle_t xStreamBuffer);

#ifdef __cplusplus
}
#endif

#endif /* INC_STREAM_BUFFER_H */
