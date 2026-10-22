/* timers.h — host-sim software timers.
 * All timers are serviced by ONE shared daemon thread driving callbacks
 * off the vboard virtual clock (not per-timer threads). */
#ifndef INC_TIMERS_H
#define INC_TIMERS_H

#include <stdint.h>
#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

struct tmrTimerControl;
typedef struct tmrTimerControl      *TimerHandle_t;
typedef void (*TimerCallbackFunction_t)(TimerHandle_t xTimer);
typedef void (*PendedFunction_t)(void *pvParameter1, uint32_t ulParameter2);

TimerHandle_t xTimerCreate(const char *pcTimerName,
                           const TickType_t xTimerPeriodInTicks,
                           const UBaseType_t uxAutoReload,
                           void *pvTimerID,
                           TimerCallbackFunction_t pxCallbackFunction);
TimerHandle_t xTimerCreateStatic(const char *pcTimerName,
                                 const TickType_t xTimerPeriodInTicks,
                                 const UBaseType_t uxAutoReload,
                                 void *pvTimerID,
                                 TimerCallbackFunction_t pxCallbackFunction,
                                 StaticTimer_t *pxTimerBuffer);

BaseType_t xTimerStart(TimerHandle_t xTimer, TickType_t xTicksToWait);
BaseType_t xTimerStop(TimerHandle_t xTimer, TickType_t xTicksToWait);
BaseType_t xTimerReset(TimerHandle_t xTimer, TickType_t xTicksToWait);
BaseType_t xTimerDelete(TimerHandle_t xTimer, TickType_t xTicksToWait);
BaseType_t xTimerChangePeriod(TimerHandle_t xTimer,
                              const TickType_t xNewPeriod,
                              TickType_t xTicksToWait);

BaseType_t xTimerStartFromISR(TimerHandle_t xTimer,
                              BaseType_t *pxHigherPriorityTaskWoken);
BaseType_t xTimerStopFromISR(TimerHandle_t xTimer,
                             BaseType_t *pxHigherPriorityTaskWoken);
BaseType_t xTimerResetFromISR(TimerHandle_t xTimer,
                              BaseType_t *pxHigherPriorityTaskWoken);
BaseType_t xTimerChangePeriodFromISR(TimerHandle_t xTimer,
                                     const TickType_t xNewPeriod,
                                     BaseType_t *pxHigherPriorityTaskWoken);

BaseType_t xTimerIsTimerActive(TimerHandle_t xTimer);
void      *pvTimerGetTimerID(const TimerHandle_t xTimer);
void       vTimerSetTimerID(TimerHandle_t xTimer, void *pvNewID);
TickType_t xTimerGetPeriod(TimerHandle_t xTimer);
TickType_t xTimerGetExpiryTime(TimerHandle_t xTimer);
const char *pcTimerGetName(TimerHandle_t xTimer);
BaseType_t xTimerGetReloadMode(TimerHandle_t xTimer);
UBaseType_t uxTimerGetReloadMode(TimerHandle_t xTimer);
void       vTimerSetReloadMode(TimerHandle_t xTimer,
                               const UBaseType_t uxAutoReload);

BaseType_t xTimerPendFunctionCall(PendedFunction_t xFunctionToPend,
                                  void *pvParameter1,
                                  uint32_t ulParameter2,
                                  TickType_t xTicksToWait);
BaseType_t xTimerPendFunctionCallFromISR(PendedFunction_t xFunctionToPend,
                                         void *pvParameter1,
                                         uint32_t ulParameter2,
                                         BaseType_t *pxHigherPriorityTaskWoken);

#ifdef __cplusplus
}
#endif

#endif /* INC_TIMERS_H */
