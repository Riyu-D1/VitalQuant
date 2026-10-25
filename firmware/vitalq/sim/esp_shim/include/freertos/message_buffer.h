/* message_buffer.h — host-sim message buffers.
 * Length-prefixed records over the stream-buffer ring. */
#ifndef INC_MESSAGE_BUFFER_H
#define INC_MESSAGE_BUFFER_H

#include <stdint.h>
#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/stream_buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef StreamBufferHandle_t MessageBufferHandle_t;

#define xMessageBufferCreate(sz) \
    ((MessageBufferHandle_t)xStreamBufferCreate((sz), (size_t)1))
#define xMessageBufferCreateStatic(sz, storage, staticbuf) \
    ((MessageBufferHandle_t)xStreamBufferCreateStatic((sz), (size_t)1, \
                                                     (storage), (staticbuf)))

size_t xMessageBufferSend(MessageBufferHandle_t xMessageBuffer,
                          const void *pvTxData, size_t xDataLengthBytes,
                          TickType_t xTicksToWait);
size_t xMessageBufferSendFromISR(MessageBufferHandle_t xMessageBuffer,
                                 const void *pvTxData,
                                 size_t xDataLengthBytes,
                                 BaseType_t *pxHigherPriorityTaskWoken);
size_t xMessageBufferReceive(MessageBufferHandle_t xMessageBuffer,
                             void *pvRxData, size_t xBufferLengthBytes,
                             TickType_t xTicksToWait);
size_t xMessageBufferReceiveFromISR(MessageBufferHandle_t xMessageBuffer,
                                    void *pvRxData, size_t xBufferLengthBytes,
                                    BaseType_t *pxHigherPriorityTaskWoken);
size_t xMessageBufferNextLengthBytes(MessageBufferHandle_t xMessageBuffer);
size_t xMessageBufferSpaceAvailable(MessageBufferHandle_t xMessageBuffer);
#define xMessageBufferSpacesAvailable(mb) xMessageBufferSpaceAvailable(mb)
BaseType_t xMessageBufferReset(MessageBufferHandle_t xMessageBuffer);
BaseType_t xMessageBufferIsEmpty(MessageBufferHandle_t xMessageBuffer);
BaseType_t xMessageBufferIsFull(MessageBufferHandle_t xMessageBuffer);
#define vMessageBufferDelete(mb)  vStreamBufferDelete((mb))

#ifdef __cplusplus
}
#endif

#endif /* INC_MESSAGE_BUFFER_H */
