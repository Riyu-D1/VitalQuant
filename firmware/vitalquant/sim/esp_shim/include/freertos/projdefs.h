/* projdefs.h — host-sim FreeRTOS shim.
 * Project definitions: basic return codes, task function type,
 * ms→tick conversion.  Self-contained; no real IDF/FreeRTOS headers. */
#ifndef INC_PROJDEFS_H
#define INC_PROJDEFS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Task entry point prototype. */
typedef void (*TaskFunction_t)(void *);

/* Return codes used across the API. */
#ifndef pdTRUE
#define pdTRUE                          ((BaseType_t)1)
#endif
#ifndef pdFALSE
#define pdFALSE                         ((BaseType_t)0)
#endif
#ifndef pdPASS
#define pdPASS                          pdTRUE
#endif
#ifndef pdFAIL
#define pdFAIL                          pdFALSE
#endif

/* Milliseconds → ticks.  Defined here (as upstream) but only *expanded*
 * at call sites where TickType_t/configTICK_RATE_HZ are visible. */
#ifndef pdMS_TO_TICKS
#define pdMS_TO_TICKS(xTimeInMs)                                             \
    ((TickType_t)(((uint64_t)(xTimeInMs) * (uint64_t)configTICK_RATE_HZ) /   \
                  1000ULL))
#endif

/* pdTICKS_TO_MS is an IDF convenience macro. */
#ifndef pdTICKS_TO_MS
#define pdTICKS_TO_MS(xTicks)                                                \
    ((uint32_t)(((uint64_t)(xTicks) * 1000ULL) / (uint64_t)configTICK_RATE_HZ))
#endif

/* errno-style kernel error codes (subset, matching upstream numbering). */
#define pdFREERTOS_ERRNO_NONE            0
#define pdFREERTOS_ERRNO_ENOENT          2
#define pdFREERTOS_ERRNO_EINTR           4
#define pdFREERTOS_ERRNO_EIO             5
#define pdFREERTOS_ERRNO_EAGAIN          11
#define pdFREERTOS_ERRNO_ENOMEM          12
#define pdFREERTOS_ERRNO_EINVAL          22
#define pdFREERTOS_ERRNO_ETIMEDOUT       116

#ifdef __cplusplus
}
#endif

#endif /* INC_PROJDEFS_H */
