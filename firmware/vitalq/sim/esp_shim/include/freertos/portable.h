/* portable.h — host-sim port layer.
 *
 * Maps the FreeRTOS "port" surface onto pthread primitives so firmware
 * gets REAL concurrency on x86: ISR callbacks (vboard tick thread) and
 * task threads genuinely race, so all port macros are real locks/yields.
 */
#ifndef INC_PORTABLE_H
#define INC_PORTABLE_H

#include <stdint.h>
#include <stddef.h>
#include <pthread.h>
#include <sched.h>

#include "freertos/projdefs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- base types -------------------------------------------------- */
typedef long            BaseType_t;
typedef unsigned long   UBaseType_t;
typedef BaseType_t      portBASE_TYPE;
typedef uint32_t        TickType_t;      /* configTICK_RATE_HZ = 1000  */
typedef uint32_t        StackType_t;

#define portMAX_DELAY               ((TickType_t)0xFFFFFFFFu)
#define portTICK_PERIOD_MS          ((TickType_t)(1000u / configTICK_RATE_HZ))
#define portTICK_RATE_MS            portTICK_PERIOD_MS
#define portBYTE_ALIGNMENT          8
#define portSTACK_GROWTH            (-1)
#define portHAS_STACK_OVERFLOW_CHECKING 0

/* ---- yielding ------------------------------------------------------ */
#define portYIELD()                 sched_yield()
#define portYIELD_WITHIN_API()      sched_yield()
/* Used both as portYIELD_FROM_ISR(hp) and portYIELD_FROM_ISR() in-tree. */
#define portYIELD_FROM_ISR(...)     sched_yield()
#define portEND_SWITCHING_ISR(x)    do { (void)(x); sched_yield(); } while (0)
#define taskYIELD()                 sched_yield()
#define portYIELD_WITHIN_API_FROM_ISR() sched_yield()

/* ---- interrupt masking ---------------------------------------------
 * Host sim has no maskable interrupts; FromISR callbacks arrive on a
 * different thread, so masking is a no-op — real exclusion comes from
 * portMUX critical sections below. */
#define portDISABLE_INTERRUPTS()    do { } while (0)
#define portENABLE_INTERRUPTS()     do { } while (0)
#define portNOP()                   do { } while (0)
#define portMemoryBarrier()         __asm__ __volatile__("" ::: "memory")

/* ---- portMUX spinlock → pthread mutex ------------------------------
 * On real silicon this is a spinlock + interrupt mask.  In the sim a
 * plain mutex is both correct and stronger: task and "ISR" threads
 * serialise through it exactly like an ISR-safe spinlock. */
typedef pthread_mutex_t             portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED    PTHREAD_MUTEX_INITIALIZER
#define portMUX_INITIALIZE(mux)         (void)pthread_mutex_init((mux), NULL)
#define portMUX_FREE_VAL                ((int)0)
#define portMUX_NO_TIMEOUT              ((int)-1)

#define portENTER_CRITICAL(mux)         (void)pthread_mutex_lock(mux)
#define portEXIT_CRITICAL(mux)          (void)pthread_mutex_unlock(mux)
#define portENTER_CRITICAL_ISR(mux)     (void)pthread_mutex_lock(mux)
#define portEXIT_CRITICAL_ISR(mux)      (void)pthread_mutex_unlock(mux)
#define portENTER_CRITICAL_SAFE(mux)    (void)pthread_mutex_lock(mux)
#define portEXIT_CRITICAL_SAFE(mux)     (void)pthread_mutex_unlock(mux)
#define portTRY_ENTER_CRITICAL(mux, to) \
    ((pthread_mutex_trylock(mux) == 0) ? pdTRUE : pdFALSE)

/* taskENTER_CRITICAL family without a mux → a single global recursive
 * lock (extern, defined in freertos_sim.c). */
extern pthread_mutex_t sim_freertos_crit_mux;
#define taskENTER_CRITICAL()            (void)pthread_mutex_lock(&sim_freertos_crit_mux)
#define taskEXIT_CRITICAL()             (void)pthread_mutex_unlock(&sim_freertos_crit_mux)
#define taskENTER_CRITICAL_ISR(mux)     portENTER_CRITICAL_ISR(mux)
#define taskEXIT_CRITICAL_ISR(mux)      portEXIT_CRITICAL_ISR(mux)
#define taskENTER_CRITICAL_SAFE(mux)    portENTER_CRITICAL_SAFE(mux)
#define taskEXIT_CRITICAL_SAFE(mux)     portEXIT_CRITICAL_SAFE(mux)
#define taskDISABLE_INTERRUPTS()        portDISABLE_INTERRUPTS()
#define taskENABLE_INTERRUPTS()         portENABLE_INTERRUPTS()

/* ---- misc port glue ------------------------------------------------ */
static inline BaseType_t xPortGetCoreID(void) { return 0; }
#define portNUM_PROCESSORS          1
#define configNUM_CORES             1
#define portCRITICAL_NESTING_IN_TSKCB 0

/* Port/scheduler entry points — stubs in the sim (scheduler is "always
 * running"; tasks start as soon as their pthread is created). */
void      *pxPortInitialiseStack(StackType_t *pxTopOfStack,
                                 TaskFunction_t pxCode, void *pvParameters);
BaseType_t xPortStartScheduler(void);
void       vPortEndScheduler(void);
void       vPortYield(void);
void       vPortYieldOtherCore(BaseType_t coreid);
BaseType_t xPortInIsrContext(void);
BaseType_t xPortInterruptedFromISRContext(void);
uint32_t   xPortGetTickRateHz(void);

#ifdef __cplusplus
}
#endif

#endif /* INC_PORTABLE_H */
