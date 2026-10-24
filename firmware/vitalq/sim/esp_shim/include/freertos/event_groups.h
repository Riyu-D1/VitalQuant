/* event_groups.h — host-sim event groups.
 * Full bit semantics: wait-any / wait-all, clear-on-exit, ISR-safe sets. */
#ifndef INC_EVENT_GROUPS_H
#define INC_EVENT_GROUPS_H

#include <stdint.h>
#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

struct EventGroupDef_t;
typedef struct EventGroupDef_t   *EventGroupHandle_t;
typedef TickType_t                EventBits_t;

EventGroupHandle_t xEventGroupCreate(void);
EventGroupHandle_t xEventGroupCreateStatic(StaticEventGroup_t *pxEventGroupBuffer);

EventBits_t xEventGroupWaitBits(EventGroupHandle_t xEventGroup,
                                const EventBits_t uxBitsToWaitFor,
                                const BaseType_t xClearOnExit,
                                const BaseType_t xWaitForAllBits,
                                TickType_t xTicksToWait);
EventBits_t xEventGroupSetBits(EventGroupHandle_t xEventGroup,
                               const EventBits_t uxBitsToSet);
EventBits_t xEventGroupClearBits(EventGroupHandle_t xEventGroup,
                                 const EventBits_t uxBitsToClear);
EventBits_t xEventGroupSync(EventGroupHandle_t xEventGroup,
                            const EventBits_t uxBitsToSet,
                            const EventBits_t uxBitsToWaitFor,
                            TickType_t xTicksToWait);

/* GetBits is a macro in upstream (clear-nothing read). */
#define xEventGroupGetBits(xEventGroup) \
    xEventGroupClearBits((xEventGroup), (EventBits_t)0)

BaseType_t xEventGroupSetBitsFromISR(EventGroupHandle_t xEventGroup,
                                     const EventBits_t uxBitsToSet,
                                     BaseType_t *pxHigherPriorityTaskWoken);
BaseType_t xEventGroupClearBitsFromISR(EventGroupHandle_t xEventGroup,
                                       const EventBits_t uxBitsToClear);
EventBits_t xEventGroupGetBitsFromISR(EventGroupHandle_t xEventGroup);

void        vEventGroupDelete(EventGroupHandle_t xEventGroup);

#ifdef __cplusplus
}
#endif

#endif /* INC_EVENT_GROUPS_H */
