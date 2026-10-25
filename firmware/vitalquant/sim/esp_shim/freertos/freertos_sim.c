/* freertos_sim.c — FreeRTOS kernel surface implemented on pthreads.
 *
 * REAL concurrency: every task is a detached pthread; "ISR" callbacks
 * arrive on the vboard tick thread, so FromISR paths race with task
 * code exactly like silicon.  All object state is mutex+condvar
 * protected; timeouts derive ONLY from the vboard virtual clock.
 *
 * Timing model:
 *   tick = virtual ms   (configTICK_RATE_HZ = 1000)
 *   vTaskDelay(n)       → vboard_clock_sleep_us(n * 1000)
 *   timed waits         → predicate-check loop; sleep bounded by a
 *                         wall-clock-mapped virtual deadline via an
 *                         empirical time-scale estimator.
 */
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <assert.h>
#include <errno.h>
#include <time.h>
#include <sched.h>
#include <unistd.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "freertos/timers.h"
#include "freertos/list.h"
#include "freertos/stream_buffer.h"
#include "freertos/message_buffer.h"
#include "vboard.h"

/* ================================================================== */
/*  internal shared types                                              */
/* ================================================================== */

typedef struct tskTaskControlBlock sim_tcb_t;

typedef enum {
    SIM_OBJ_QUEUE = 0,
    SIM_SEM_BINARY,
    SIM_SEM_COUNTING,
    SIM_SEM_MUTEX,
    SIM_SEM_RECURSIVE
} sim_obj_kind_t;

/* ================================================================== */
/*  virtual-clock helpers                                              */
/* ================================================================== */

static int64_t sim_wall_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}

/* virtual µs per wall µs — estimated over a sliding window so that
 * vboard_clock_set_scale() is honoured without a getter. */
static double sim_scale(void)
{
    static pthread_mutex_t m   = PTHREAD_MUTEX_INITIALIZER;
    static int64_t         v0  = -1, w0;
    static double          s   = 1.0;

    int64_t v = vboard_now_us(), w = sim_wall_us();
    pthread_mutex_lock(&m);
    if (v0 < 0) {
        v0 = v; w0 = w;
    } else {
        int64_t dv = v - v0, dw = w - w0;
        if (dw >= 5000) {                    /* ≥5 ms wall sample window */
            double ns = (double)dv / (double)dw;
            if (ns > 1e-4 && ns < 1e5) s = ns;
            v0 = v; w0 = w;
        }
    }
    double r = s;
    pthread_mutex_unlock(&m);
    return r;
}

/* map a virtual-time duration to a wall-clock duration (µs). */
static int64_t sim_virt_to_wall_us(int64_t virt_us)
{
    double s = sim_scale();
    if (s <= 0.0) s = 1.0;
    double w = (double)virt_us / s;
    if (w < 1.0)    w = 1.0;
    if (w > 1e13)   w = 1e13;
    return (int64_t)w;
}

/* bound on each cond_timedwait slice — recheck cadence. */
#define SIM_WALL_SLICE_US  10000

/* m must be held.  Waits on cv until the virtual deadline or spurious
 * wake; caller re-checks its predicate + deadline. */
static void sim_wait_until_locked(pthread_cond_t *cv, pthread_mutex_t *m,
                                  int64_t deadline_us)
{
    int64_t rem = deadline_us - vboard_now_us();
    if (rem <= 0) return;
    int64_t w = sim_virt_to_wall_us(rem);
    if (w > SIM_WALL_SLICE_US) w = SIM_WALL_SLICE_US;

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec  += w / 1000000;
    ts.tv_nsec += (w % 1000000) * 1000;
    if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
    (void)pthread_cond_timedwait(cv, m, &ts);
}

static void sim_unlock_cb(void *p)
{
    (void)pthread_mutex_unlock((pthread_mutex_t *)p);
}

static int64_t sim_deadline(TickType_t wait)
{
    if (wait == portMAX_DELAY) return INT64_MAX;
    return vboard_now_us() + (int64_t)wait * 1000;
}

/* ================================================================== */
/*  task control blocks                                                */
/* ================================================================== */

#define SIM_SIG_SUSPEND  (SIGRTMIN + 6)

struct tskTaskControlBlock {
    pthread_t        th;
    TaskFunction_t   fn;
    void            *arg;
    char             name[configMAX_TASK_NAME_LEN];
    _Atomic UBaseType_t prio;
    _Atomic UBaseType_t affinity;
    _Atomic int      state;        /* eTaskState                      */
    _Atomic int      suspended;
    _Atomic int      deleted;
    TaskHookFunction_t tag;
    void            *tls[configNUM_THREAD_LOCAL_STORAGE_POINTERS];
    /* notifications — guarded by nlk */
    pthread_mutex_t  nlk;
    pthread_cond_t   ncv;
    uint32_t         notify_val;
    int              notify_state; /* 0 none, 1 waiting, 2 notified   */
    /* suspend park — guarded by slk, ONLY the suspend machinery uses it */
    pthread_mutex_t  slk;
    pthread_cond_t   scv;
    sim_tcb_t       *next;
};

static pthread_mutex_t g_reg_m  = PTHREAD_MUTEX_INITIALIZER;
static sim_tcb_t      *g_tasks;
static unsigned        g_ntasks;
static _Atomic unsigned g_taskseq;

/* global critical-section lock used by taskENTER_CRITICAL() — recursive
 * so nested enters don't self-deadlock.  Initialised in the ctor. */
pthread_mutex_t sim_freertos_crit_mux;
static pthread_mutex_t g_sched_mux;

static _Thread_local sim_tcb_t *tl_tcb;

/* lazily give any foreign pthread (main, vboard tick, scenario threads)
 * a task identity so notifications/state queries work everywhere. */
static sim_tcb_t *sim_self(void)
{
    if (tl_tcb) return tl_tcb;
    sim_tcb_t *t = calloc(1, sizeof(*t));
    configASSERT(t);
    t->th       = pthread_self();
    t->prio     = tskIDLE_PRIORITY;
    t->affinity = (UBaseType_t)-1;
    t->state    = eRunning;
    snprintf(t->name, sizeof(t->name), "ext%04lx",
             (unsigned long)(uintptr_t)pthread_self() & 0xffffu);
    pthread_mutex_init(&t->nlk, NULL);
    pthread_cond_init(&t->ncv, NULL);
    pthread_mutex_init(&t->slk, NULL);
    pthread_cond_init(&t->scv, NULL);
    pthread_mutex_lock(&g_reg_m);
    t->next = g_tasks; g_tasks = t; g_ntasks++;
    pthread_mutex_unlock(&g_reg_m);
    tl_tcb = t;
    return t;
}

static inline void sim_block(sim_tcb_t *t)   { if (t) t->state = eBlocked; }
static inline void sim_unblock(sim_tcb_t *t) { if (t) t->state = eRunning; }

/* suspend signal handler: parks the victim thread until resumed. */
static void sim_susp_handler(int sig)
{
    (void)sig;
    sim_tcb_t *t = tl_tcb;
    if (!t) return;
    pthread_mutex_lock(&t->slk);
    pthread_cleanup_push(sim_unlock_cb, &t->slk);
    while (t->suspended && !t->deleted)
        pthread_cond_wait(&t->scv, &t->slk);
    pthread_cleanup_pop(0);
    pthread_mutex_unlock(&t->slk);
}

static void sim_init_once(void)
{
    /* recursive mux for taskENTER_CRITICAL() */
    pthread_mutexattr_t at;
    pthread_mutexattr_init(&at);
    pthread_mutexattr_settype(&at, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&sim_freertos_crit_mux, &at);
    pthread_mutexattr_destroy(&at);
    pthread_mutex_init(&g_sched_mux, NULL);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sim_susp_handler;
    sa.sa_flags   = SA_RESTART;         /* don't chop vTaskDelay naps   */
    sigemptyset(&sa.sa_mask);
    sigaction(SIM_SIG_SUSPEND, &sa, NULL);
}
__attribute__((constructor)) static void sim_ctor(void) { sim_init_once(); }

static void *sim_tramp(void *p)
{
    sim_tcb_t *t = p;
    tl_tcb    = t;
    t->state  = eRunning;
    /* a suspend may have arrived before tl_tcb was set — the signal
     * handler skipped it, so honour the flag here. */
    if (t->suspended) sim_susp_handler(0);
    t->fn(t->arg);
    t->deleted = 1;
    t->state   = eDeleted;
    return NULL;
}

static sim_tcb_t *sim_task_spawn(TaskFunction_t fn, const char *name,
                                 void *arg, UBaseType_t prio)
{
    if (!fn) return NULL;
    sim_tcb_t *t = calloc(1, sizeof(*t));
    if (!t) return NULL;
    t->fn       = fn;
    t->arg      = arg;
    t->prio     = prio;
    t->affinity = (UBaseType_t)-1;
    t->state    = eReady;
    if (name && name[0]) snprintf(t->name, sizeof(t->name), "%s", name);
    else snprintf(t->name, sizeof(t->name), "task%u", ++g_taskseq);
    pthread_mutex_init(&t->nlk, NULL);
    pthread_cond_init(&t->ncv, NULL);
    pthread_mutex_init(&t->slk, NULL);
    pthread_cond_init(&t->scv, NULL);

    pthread_mutex_lock(&g_reg_m);
    t->next = g_tasks; g_tasks = t; g_ntasks++;
    pthread_mutex_unlock(&g_reg_m);

    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    int rc = pthread_create(&t->th, &at, sim_tramp, t);
    pthread_attr_destroy(&at);
    if (rc != 0) {
        pthread_mutex_lock(&g_reg_m);
        g_tasks = t->next; g_ntasks--;
        pthread_mutex_unlock(&g_reg_m);
        free(t);
        return NULL;
    }
    return t;
}

/* ================================================================== */
/*  task lifecycle API                                                 */
/* ================================================================== */

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t pxTaskCode,
                                   const char *pcName,
                                   const uint32_t usStackDepth,
                                   void *pvParameters,
                                   UBaseType_t uxPriority,
                                   TaskHandle_t *pxCreatedTask,
                                   const BaseType_t xCoreID)
{
    (void)usStackDepth; (void)xCoreID;
    sim_tcb_t *t = sim_task_spawn(pxTaskCode, pcName, pvParameters, uxPriority);
    if (!t) return pdFAIL;
    if (pxCreatedTask) *pxCreatedTask = (TaskHandle_t)t;
    return pdPASS;
}

BaseType_t xTaskCreate(TaskFunction_t pxTaskCode, const char *pcName,
                       const uint32_t usStackDepth, void *pvParameters,
                       UBaseType_t uxPriority, TaskHandle_t *pxCreatedTask)
{
    return xTaskCreatePinnedToCore(pxTaskCode, pcName, usStackDepth,
                                   pvParameters, uxPriority, pxCreatedTask,
                                   tskNO_AFFINITY);
}

TaskHandle_t xTaskCreateStaticPinnedToCore(TaskFunction_t pxTaskCode,
                                           const char *pcName,
                                           const uint32_t ulStackDepth,
                                           void *pvParameters,
                                           UBaseType_t uxPriority,
                                           StackType_t *puxStackBuffer,
                                           StaticTask_t *pxTaskBuffer,
                                           const BaseType_t xCoreID)
{
    (void)puxStackBuffer; (void)pxTaskBuffer;
    sim_tcb_t *t = sim_task_spawn(pxTaskCode, pcName, pvParameters,
                                  uxPriority);
    (void)xCoreID;
    (void)ulStackDepth;
    return (TaskHandle_t)t;
}

TaskHandle_t xTaskCreateStatic(TaskFunction_t pxTaskCode, const char *pcName,
                               const uint32_t ulStackDepth, void *pvParameters,
                               UBaseType_t uxPriority, StackType_t *puxStackBuffer,
                               StaticTask_t *pxTaskBuffer)
{
    return xTaskCreateStaticPinnedToCore(pxTaskCode, pcName, ulStackDepth,
                                       pvParameters, uxPriority,
                                       puxStackBuffer, pxTaskBuffer,
                                       tskNO_AFFINITY);
}

void vTaskDelete(TaskHandle_t xTaskToDelete)
{
    sim_tcb_t *t = xTaskToDelete ? (sim_tcb_t *)xTaskToDelete : sim_self();
    if (!t || t->deleted) return;
    t->deleted = 1;
    t->state   = eDeleted;
    /* wake anything parked on this task's own condvars */
    pthread_mutex_lock(&t->slk);
    t->suspended = 0;
    pthread_cond_broadcast(&t->scv);
    pthread_mutex_unlock(&t->slk);
    pthread_mutex_lock(&t->nlk);
    pthread_cond_broadcast(&t->ncv);
    pthread_mutex_unlock(&t->nlk);

    if (pthread_equal(t->th, pthread_self())) {
        tl_tcb = NULL;
        pthread_exit(NULL);
    }
    /* land at the next cancellation point (cond waits, sleeps) — the
     * wait loops push cleanup handlers so held mutexes get released. */
    (void)pthread_cancel(t->th);
}

/* ================================================================== */
/*  delays / ticks                                                     */
/* ================================================================== */

void vTaskDelay(const TickType_t xTicksToDelay)
{
    sim_tcb_t *me = tl_tcb;
    if (me && me->deleted) pthread_exit(NULL);
    if (xTicksToDelay == 0) { sched_yield(); return; }
    sim_block(me);
    vboard_clock_sleep_us((int64_t)xTicksToDelay * 1000);
    sim_unblock(me);
}

void vTaskDelayUntil(TickType_t *pxPreviousWakeTime,
                     const TickType_t xTimeIncrement)
{
    sim_tcb_t *me = tl_tcb;
    if (me && me->deleted) pthread_exit(NULL);
    TickType_t now  = xTaskGetTickCount();
    TickType_t wake = *pxPreviousWakeTime + xTimeIncrement;

    /* wait only if the target is still in the future (wrap-tolerant) */
    if ((TickType_t)(wake - now) < ((TickType_t)1 << 31) && wake != now) {
        sim_block(me);
        vboard_clock_sleep_us((int64_t)(wake - now) * 1000);
        sim_unblock(me);
        now = xTaskGetTickCount();
    }
    /* late by more than a full period → resync (no burst catch-up) */
    if ((TickType_t)(now - *pxPreviousWakeTime) >= xTimeIncrement)
        *pxPreviousWakeTime = now;
    else
        *pxPreviousWakeTime = wake;
}

TickType_t xTaskGetTickCount(void)
{
    return (TickType_t)(vboard_now_us() / 1000);
}

TickType_t xTaskGetTickCountFromISR(void) { return xTaskGetTickCount(); }

BaseType_t xTaskAbortDelay(TaskHandle_t xTask)
{
    sim_tcb_t *t = (sim_tcb_t *)xTask;
    if (!t) return pdFAIL;
    pthread_mutex_lock(&t->nlk);
    pthread_cond_broadcast(&t->ncv);
    pthread_mutex_unlock(&t->nlk);
    return pdPASS;
}

/* ================================================================== */
/*  identity / state                                                   */
/* ================================================================== */

TaskHandle_t xTaskGetCurrentTaskHandle(void)
{
    return (TaskHandle_t)sim_self();
}

TaskHandle_t xTaskGetHandle(const char *pcNameToQuery)
{
    if (!pcNameToQuery) return NULL;
    TaskHandle_t r = NULL;
    pthread_mutex_lock(&g_reg_m);
    for (sim_tcb_t *t = g_tasks; t; t = t->next)
        if (!t->deleted && !strncmp(t->name, pcNameToQuery,
                                    configMAX_TASK_NAME_LEN)) {
            r = (TaskHandle_t)t;
            break;
        }
    pthread_mutex_unlock(&g_reg_m);
    return r;
}

char *pcTaskGetName(TaskHandle_t xTaskToQuery)
{
    sim_tcb_t *t = xTaskToQuery ? (sim_tcb_t *)xTaskToQuery : sim_self();
    return t ? t->name : (char *)"?";
}

UBaseType_t uxTaskPriorityGet(const TaskHandle_t xTask)
{
    const sim_tcb_t *t = xTask ? (const sim_tcb_t *)xTask : sim_self();
    return t ? t->prio : 0;
}

UBaseType_t uxTaskPriorityGetFromISR(const TaskHandle_t xTask)
{
    return uxTaskPriorityGet(xTask);
}

void vTaskPrioritySet(TaskHandle_t xTask, UBaseType_t uxNewPriority)
{
    sim_tcb_t *t = xTask ? (sim_tcb_t *)xTask : sim_self();
    if (t) t->prio = uxNewPriority;
}

void vTaskSuspend(TaskHandle_t xTaskToSuspend)
{
    sim_tcb_t *t = xTaskToSuspend ? (sim_tcb_t *)xTaskToSuspend : sim_self();
    if (!t) return;
    pthread_mutex_lock(&t->slk);
    t->suspended = 1;
    pthread_mutex_unlock(&t->slk);
    (void)pthread_kill(t->th, SIM_SIG_SUSPEND);
}

void vTaskResume(TaskHandle_t xTaskToResume)
{
    sim_tcb_t *t = (sim_tcb_t *)xTaskToResume;
    if (!t) return;
    pthread_mutex_lock(&t->slk);
    t->suspended = 0;
    pthread_cond_broadcast(&t->scv);
    pthread_mutex_unlock(&t->slk);
}

BaseType_t xTaskResumeFromISR(TaskHandle_t xTaskToResume)
{
    vTaskResume(xTaskToResume);
    return pdFALSE;
}

eTaskState eTaskGetState(TaskHandle_t xTask)
{
    sim_tcb_t *t = xTask ? (sim_tcb_t *)xTask : sim_self();
    if (!t) return eInvalid;
    if (t->deleted)   return eDeleted;
    if (t->suspended) return eSuspended;
    if (t == tl_tcb)  return eRunning;
    return (eTaskState)t->state;
}

UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t xTask)
{
    (void)xTask;
    return 1024;             /* fabricated — no per-task stacks in sim */
}

UBaseType_t uxTaskGetNumberOfTasks(void)
{
    pthread_mutex_lock(&g_reg_m);
    unsigned n = g_ntasks;
    pthread_mutex_unlock(&g_reg_m);
    return (UBaseType_t)n;
}

BaseType_t xTaskGetSchedulerState(void) { return taskSCHEDULER_RUNNING; }

TaskHandle_t xTaskGetIdleTaskHandle(void) { return NULL; }

/* ================================================================== */
/*  timeouts                                                           */
/* ================================================================== */

void vTaskSetTimeOutState(TimeOut_t *pxTimeOut)
{
    if (!pxTimeOut) return;
    pxTimeOut->xOverflowCount  = 0;
    pxTimeOut->xTimeOnEntering = xTaskGetTickCount();
}

BaseType_t xTaskCheckForTimeOut(TimeOut_t *pxTimeOut,
                                TickType_t *pxTicksToWait)
{
    if (!pxTimeOut || !pxTicksToWait) return pdTRUE;
    TickType_t now     = xTaskGetTickCount();
    TickType_t elapsed = now - pxTimeOut->xTimeOnEntering;
    if (elapsed >= *pxTicksToWait) { *pxTicksToWait = 0; return pdTRUE; }
    *pxTicksToWait            -= elapsed;
    pxTimeOut->xTimeOnEntering = now;
    return pdFALSE;
}

/* ================================================================== */
/*  scheduler control                                                  */
/* ================================================================== */

void       vTaskStartScheduler(void) { /* always running in sim */ }
void       vTaskEndScheduler(void)   { /* never ends in sim      */ }
void       vTaskSuspendAll(void)     { (void)pthread_mutex_lock(&g_sched_mux); }
BaseType_t xTaskResumeAll(void)
{
    (void)pthread_mutex_unlock(&g_sched_mux);
    return pdFALSE;
}

/* ================================================================== */
/*  task notifications                                                 */
/* ================================================================== */

static BaseType_t sim_notify(sim_tcb_t *t, uint32_t ulValue,
                             eNotifyAction eAction, uint32_t *prev)
{
    if (!t) return pdFAIL;
    pthread_mutex_lock(&t->nlk);
    if (prev) *prev = t->notify_val;
    BaseType_t ok = pdPASS;
    switch (eAction) {
    case eNoAction:                                        break;
    case eSetBits:              t->notify_val |= ulValue;  break;
    case eIncrement:            t->notify_val++;           break;
    case eSetValueWithOverwrite: t->notify_val = ulValue;  break;
    case eSetValueWithoutOverwrite:
        if (t->notify_state == 2) ok = pdFAIL;
        else                      t->notify_val = ulValue;
        break;
    default:                    ok = pdFAIL;               break;
    }
    if (ok) {
        t->notify_state = 2;                 /* notification received  */
        pthread_cond_broadcast(&t->ncv);
    }
    pthread_mutex_unlock(&t->nlk);
    return ok;
}

BaseType_t xTaskNotify(TaskHandle_t xTaskToNotify, uint32_t ulValue,
                       eNotifyAction eAction)
{
    return sim_notify((sim_tcb_t *)xTaskToNotify, ulValue, eAction, NULL);
}

BaseType_t xTaskNotifyAndQuery(TaskHandle_t xTaskToNotify, uint32_t ulValue,
                               eNotifyAction eAction,
                               uint32_t *pulPreviousNotifyValue)
{
    return sim_notify((sim_tcb_t *)xTaskToNotify, ulValue, eAction,
                      pulPreviousNotifyValue);
}

BaseType_t xTaskNotifyFromISR(TaskHandle_t xTaskToNotify, uint32_t ulValue,
                              eNotifyAction eAction,
                              BaseType_t *pxHigherPriorityTaskWoken)
{
    if (pxHigherPriorityTaskWoken) *pxHigherPriorityTaskWoken = pdFALSE;
    return xTaskNotify(xTaskToNotify, ulValue, eAction);
}

BaseType_t xTaskNotifyAndQueryFromISR(TaskHandle_t xTaskToNotify,
                                      uint32_t ulValue,
                                      eNotifyAction eAction,
                                      uint32_t *pulPreviousNotificationValue,
                                      BaseType_t *pxHigherPriorityTaskWoken)
{
    if (pxHigherPriorityTaskWoken) *pxHigherPriorityTaskWoken = pdFALSE;
    return xTaskNotifyAndQuery(xTaskToNotify, ulValue, eAction,
                               pulPreviousNotificationValue);
}

void vTaskNotifyGiveFromISR(TaskHandle_t xTaskToNotify,
                            BaseType_t *pxHigherPriorityTaskWoken)
{
    if (pxHigherPriorityTaskWoken) *pxHigherPriorityTaskWoken = pdFALSE;
    (void)xTaskNotify(xTaskToNotify, 0, eIncrement);
}

BaseType_t xTaskNotifyWait(uint32_t ulBitsToClearOnEntry,
                           uint32_t ulBitsToClearOnExit,
                           uint32_t *pulNotificationValue,
                           TickType_t xTicksToWait)
{
    sim_tcb_t *me = sim_self();
    int64_t    dl = sim_deadline(xTicksToWait);
    BaseType_t ret = pdTRUE;

    pthread_mutex_lock(&me->nlk);
    pthread_cleanup_push(sim_unlock_cb, &me->nlk);
    me->notify_val &= ~ulBitsToClearOnEntry;
    sim_block(me);
    if (me->notify_state != 2) {
        me->notify_state = 1;                /* waiting                  */
        for (;;) {
            if (me->deleted) pthread_exit(NULL);
            if (me->notify_state == 2) break;
            if (xTicksToWait == 0) { ret = pdFALSE; break; }
            if (dl == INT64_MAX) {
                pthread_cond_wait(&me->ncv, &me->nlk);
                continue;
            }
            if (vboard_now_us() >= dl) { ret = pdFALSE; break; }
            sim_wait_until_locked(&me->ncv, &me->nlk, dl);
        }
    }
    if (ret == pdTRUE) {
        if (pulNotificationValue) *pulNotificationValue = me->notify_val;
        me->notify_val &= ~ulBitsToClearOnExit;
    }
    me->notify_state = 0;
    sim_unblock(me);
    pthread_cleanup_pop(0);
    pthread_mutex_unlock(&me->nlk);
    return ret;
}

uint32_t ulTaskNotifyTake(BaseType_t xClearCountOnExit,
                          TickType_t xTicksToWait)
{
    sim_tcb_t *me = sim_self();
    int64_t    dl = sim_deadline(xTicksToWait);
    uint32_t   v  = 0;

    pthread_mutex_lock(&me->nlk);
    pthread_cleanup_push(sim_unlock_cb, &me->nlk);
    sim_block(me);
    for (;;) {
        if (me->deleted) pthread_exit(NULL);
        if (me->notify_val > 0) break;
        if (xTicksToWait == 0) break;
        if (dl == INT64_MAX) {
            pthread_cond_wait(&me->ncv, &me->nlk);
            continue;
        }
        if (vboard_now_us() >= dl) break;
        sim_wait_until_locked(&me->ncv, &me->nlk, dl);
    }
    v = me->notify_val;
    if (v > 0) {
        if (xClearCountOnExit) me->notify_val = 0;
        else                   me->notify_val--;
    }
    if (me->notify_val == 0 && me->notify_state == 2)
        me->notify_state = 0;
    sim_unblock(me);
    pthread_cleanup_pop(0);
    pthread_mutex_unlock(&me->nlk);
    return v;
}

BaseType_t xTaskNotifyStateClear(TaskHandle_t xTask)
{
    sim_tcb_t *t = xTask ? (sim_tcb_t *)xTask : sim_self();
    if (!t) return pdFALSE;
    pthread_mutex_lock(&t->nlk);
    BaseType_t had = (t->notify_state == 2) ? pdTRUE : pdFALSE;
    t->notify_state = 0;
    pthread_mutex_unlock(&t->nlk);
    return had;
}

uint32_t ulTaskNotifyValueClear(TaskHandle_t xTask, uint32_t ulBitsToClear)
{
    sim_tcb_t *t = xTask ? (sim_tcb_t *)xTask : sim_self();
    if (!t) return 0;
    pthread_mutex_lock(&t->nlk);
    uint32_t prev = t->notify_val;
    t->notify_val &= ~ulBitsToClear;
    pthread_mutex_unlock(&t->nlk);
    return prev;
}

/* ================================================================== */
/*  thread-local storage / tags / affinity                             */
/* ================================================================== */

void vTaskSetThreadLocalStoragePointer(TaskHandle_t xTaskToSet,
                                       BaseType_t xIndex, void *pvValue)
{
    sim_tcb_t *t = xTaskToSet ? (sim_tcb_t *)xTaskToSet : sim_self();
    if (!t || xIndex < 0 ||
        xIndex >= (BaseType_t)configNUM_THREAD_LOCAL_STORAGE_POINTERS)
        return;
    t->tls[xIndex] = pvValue;
}

void *pvTaskGetThreadLocalStoragePointer(TaskHandle_t xTaskToQuery,
                                         BaseType_t xIndex)
{
    sim_tcb_t *t = xTaskToQuery ? (sim_tcb_t *)xTaskToQuery : sim_self();
    if (!t || xIndex < 0 ||
        xIndex >= (BaseType_t)configNUM_THREAD_LOCAL_STORAGE_POINTERS)
        return NULL;
    return t->tls[xIndex];
}

void vTaskSetApplicationTaskTag(TaskHandle_t xTask, TaskHookFunction_t pxHook)
{
    sim_tcb_t *t = xTask ? (sim_tcb_t *)xTask : sim_self();
    if (t) t->tag = pxHook;
}

TaskHookFunction_t xTaskGetApplicationTaskTag(TaskHandle_t xTask)
{
    sim_tcb_t *t = xTask ? (sim_tcb_t *)xTask : sim_self();
    return t ? t->tag : NULL;
}

void vTaskCoreAffinitySet(TaskHandle_t xTask, UBaseType_t uxCoreAffinityMask)
{
    sim_tcb_t *t = xTask ? (sim_tcb_t *)xTask : sim_self();
    if (t) t->affinity = uxCoreAffinityMask;
}

UBaseType_t xTaskGetAffinity(TaskHandle_t xTask)
{
    sim_tcb_t *t = xTask ? (sim_tcb_t *)xTask : sim_self();
    return t ? t->affinity : (UBaseType_t)-1;
}

/* ================================================================== */
/*  shared queue-family object (queues + all semaphore flavours)        */
/* ================================================================== */

struct QueueDefinition {
    pthread_mutex_t  m;
    pthread_cond_t   cv_rx;         /* "item/sem available" waiters  */
    pthread_cond_t   cv_tx;         /* "space available" waiters     */
    sim_obj_kind_t   kind;
    _Atomic int      deleted;
    /* queue state */
    uint8_t         *buf;
    UBaseType_t      len;
    UBaseType_t      item;
    UBaseType_t      head;
    UBaseType_t      count;
    /* semaphore state */
    long             scount;
    long             smax;
    TaskHandle_t     owner;
    long             rcount;
};

typedef struct QueueDefinition sim_qd_t;

/* queue-level aliases for semaphore ops (upstream exposes these too);
 * defined after the semaphore section below via forward decls. */
static SemaphoreHandle_t sim_sem_create(sim_obj_kind_t kind, long max,
                                        long initial);
BaseType_t xSemaphoreTake(SemaphoreHandle_t xSemaphore,
                          TickType_t xTicksToWait);
BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t xMutex,
                                   TickType_t xTicksToWait);
BaseType_t xSemaphoreGive(SemaphoreHandle_t xSemaphore);
BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t xMutex);
TaskHandle_t xSemaphoreGetMutexHolder(SemaphoreHandle_t xMutex);

QueueHandle_t xQueueCreateMutex(const uint8_t ucQueueType)
{
    (void)ucQueueType;
    return (QueueHandle_t)sim_sem_create(SIM_SEM_MUTEX, 1, 1);
}

QueueHandle_t xQueueCreateMutexStatic(const uint8_t ucQueueType,
                                      StaticQueue_t *pxStaticQueue)
{
    (void)pxStaticQueue;
    return xQueueCreateMutex(ucQueueType);
}

QueueHandle_t xQueueCreateRecursiveMutex(void)
{
    return (QueueHandle_t)sim_sem_create(SIM_SEM_RECURSIVE, 1, 1);
}

QueueHandle_t xQueueCreateCountingSemaphore(const UBaseType_t uxMaxCount,
                                            const UBaseType_t uxInitialCount)
{
    if (uxMaxCount == 0 || uxInitialCount > uxMaxCount) return NULL;
    return (QueueHandle_t)sim_sem_create(SIM_SEM_COUNTING,
                                         (long)uxMaxCount,
                                         (long)uxInitialCount);
}

QueueHandle_t xQueueCreateCountingSemaphoreStatic(
    const UBaseType_t uxMaxCount, const UBaseType_t uxInitialCount,
    StaticQueue_t *pxStaticQueue)
{
    (void)pxStaticQueue;
    return xQueueCreateCountingSemaphore(uxMaxCount, uxInitialCount);
}

BaseType_t xQueueSemaphoreTake(QueueHandle_t xQueue, TickType_t xTicksToWait)
{
    return xSemaphoreTake((SemaphoreHandle_t)xQueue, xTicksToWait);
}

BaseType_t xQueueTakeMutexRecursive(QueueHandle_t xMutex,
                                    TickType_t xTicksToWait)
{
    return xSemaphoreTakeRecursive((SemaphoreHandle_t)xMutex,
                                   xTicksToWait);
}

BaseType_t xQueueGiveMutexRecursive(QueueHandle_t xMutex)
{
    return xSemaphoreGiveRecursive((SemaphoreHandle_t)xMutex);
}

BaseType_t xQueueGiveFromISR(QueueHandle_t xQueue,
                             BaseType_t *pxHigherPriorityTaskWoken)
{
    if (pxHigherPriorityTaskWoken) *pxHigherPriorityTaskWoken = pdFALSE;
    return xSemaphoreGive((SemaphoreHandle_t)xQueue);
}

TaskHandle_t xQueueGetMutexHolder(QueueHandle_t xSemaphore)
{
    return xSemaphoreGetMutexHolder((SemaphoreHandle_t)xSemaphore);
}

TaskHandle_t xQueueGetMutexHolderFromISR(QueueHandle_t xSemaphore)
{
    return xSemaphoreGetMutexHolder((SemaphoreHandle_t)xSemaphore);
}

/* -------------------------- queues ---------------------------------- */

QueueHandle_t xQueueCreate(const UBaseType_t uxQueueLength,
                           const UBaseType_t uxItemSize)
{
    if (uxQueueLength == 0 || uxItemSize == 0) return NULL;
    sim_qd_t *q = calloc(1, sizeof(*q));
    if (!q) return NULL;
    q->buf = malloc((size_t)uxQueueLength * uxItemSize);
    if (!q->buf) { free(q); return NULL; }
    q->kind = SIM_OBJ_QUEUE;
    q->len  = uxQueueLength;
    q->item = uxItemSize;
    pthread_mutex_init(&q->m, NULL);
    pthread_cond_init(&q->cv_rx, NULL);
    pthread_cond_init(&q->cv_tx, NULL);
    return (QueueHandle_t)q;
}

QueueHandle_t xQueueCreateStatic(const UBaseType_t uxQueueLength,
                                 const UBaseType_t uxItemSize,
                                 uint8_t *pucQueueStorage,
                                 StaticQueue_t *pxStaticQueue)
{
    (void)pucQueueStorage; (void)pxStaticQueue;
    return xQueueCreate(uxQueueLength, uxItemSize);
}

BaseType_t xQueueGenericSend(QueueHandle_t xQueue, const void *pvItemToQueue,
                             TickType_t xTicksToWait,
                             const BaseType_t xCopyPosition)
{
    sim_qd_t *q = (sim_qd_t *)xQueue;
    if (!q || q->kind != SIM_OBJ_QUEUE || !pvItemToQueue)
        return errQUEUE_FULL;
    sim_tcb_t *me  = tl_tcb;
    int64_t    dl  = sim_deadline(xTicksToWait);
    BaseType_t ret = errQUEUE_FULL;

    pthread_mutex_lock(&q->m);
    pthread_cleanup_push(sim_unlock_cb, &q->m);
    sim_block(me);
    for (;;) {
        if (me && me->deleted) pthread_exit(NULL);
        if (q->deleted) { ret = errQUEUE_FULL; break; }
        if (xCopyPosition == queueOVERWRITE) {
            UBaseType_t wp;
            if (q->count == q->len)
                wp = (q->head + q->count - 1) % q->len;   /* newest      */
            else {
                wp = (q->head + q->count) % q->len;
                q->count++;
            }
            memcpy(q->buf + wp * q->item, pvItemToQueue, q->item);
            pthread_cond_signal(&q->cv_rx);
            ret = pdPASS;
            break;
        }
        if (q->count < q->len) {
            UBaseType_t wp;
            if (xCopyPosition == queueSEND_TO_FRONT) {
                q->head = (q->head + q->len - 1) % q->len;
                wp = q->head;
            } else {
                wp = (q->head + q->count) % q->len;
            }
            memcpy(q->buf + wp * q->item, pvItemToQueue, q->item);
            q->count++;
            pthread_cond_signal(&q->cv_rx);
            ret = pdPASS;
            break;
        }
        if (xTicksToWait == 0) { ret = errQUEUE_FULL; break; }
        if (dl == INT64_MAX) {
            pthread_cond_wait(&q->cv_tx, &q->m);
            continue;
        }
        if (vboard_now_us() >= dl) { ret = errQUEUE_FULL; break; }
        sim_wait_until_locked(&q->cv_tx, &q->m, dl);
    }
    sim_unblock(me);
    pthread_cleanup_pop(0);
    pthread_mutex_unlock(&q->m);
    return ret;
}

BaseType_t xQueueGenericSendFromISR(QueueHandle_t xQueue,
                                    const void *pvItemToQueue,
                                    BaseType_t *pxHigherPriorityTaskWoken,
                                    const BaseType_t xCopyPosition)
{
    if (pxHigherPriorityTaskWoken) *pxHigherPriorityTaskWoken = pdFALSE;
    sim_qd_t *q = (sim_qd_t *)xQueue;
    if (!q || q->kind != SIM_OBJ_QUEUE || !pvItemToQueue)
        return errQUEUE_FULL;
    BaseType_t ret = errQUEUE_FULL;
    pthread_mutex_lock(&q->m);
    if (q->deleted) { pthread_mutex_unlock(&q->m); return errQUEUE_FULL; }
    if (xCopyPosition == queueOVERWRITE) {
        UBaseType_t wp;
        if (q->count == q->len)
            wp = (q->head + q->count - 1) % q->len;
        else {
            wp = (q->head + q->count) % q->len;
            q->count++;
        }
        memcpy(q->buf + wp * q->item, pvItemToQueue, q->item);
        pthread_cond_signal(&q->cv_rx);
        ret = pdPASS;
    } else if (q->count < q->len) {
        UBaseType_t wp;
        if (xCopyPosition == queueSEND_TO_FRONT) {
            q->head = (q->head + q->len - 1) % q->len;
            wp = q->head;
        } else {
            wp = (q->head + q->count) % q->len;
        }
        memcpy(q->buf + wp * q->item, pvItemToQueue, q->item);
        q->count++;
        pthread_cond_signal(&q->cv_rx);
        ret = pdPASS;
    }
    pthread_mutex_unlock(&q->m);
    return ret;
}

BaseType_t xQueueReceive(QueueHandle_t xQueue, void *pvBuffer,
                         TickType_t xTicksToWait)
{
    sim_qd_t *q = (sim_qd_t *)xQueue;
    if (!q || q->kind != SIM_OBJ_QUEUE || !pvBuffer) return pdFALSE;
    sim_tcb_t *me  = tl_tcb;
    int64_t    dl  = sim_deadline(xTicksToWait);
    BaseType_t ret = pdFALSE;

    pthread_mutex_lock(&q->m);
    pthread_cleanup_push(sim_unlock_cb, &q->m);
    sim_block(me);
    for (;;) {
        if (me && me->deleted) pthread_exit(NULL);
        if (q->deleted) { ret = pdFALSE; break; }
        if (q->count > 0) {
            memcpy(pvBuffer, q->buf + q->head * q->item, q->item);
            q->head = (q->head + 1) % q->len;
            q->count--;
            pthread_cond_signal(&q->cv_tx);
            ret = pdPASS;
            break;
        }
        if (xTicksToWait == 0) break;
        if (dl == INT64_MAX) {
            pthread_cond_wait(&q->cv_rx, &q->m);
            continue;
        }
        if (vboard_now_us() >= dl) break;
        sim_wait_until_locked(&q->cv_rx, &q->m, dl);
    }
    sim_unblock(me);
    pthread_cleanup_pop(0);
    pthread_mutex_unlock(&q->m);
    return ret;
}

BaseType_t xQueuePeek(QueueHandle_t xQueue, void *pvBuffer,
                      TickType_t xTicksToWait)
{
    sim_qd_t *q = (sim_qd_t *)xQueue;
    if (!q || q->kind != SIM_OBJ_QUEUE || !pvBuffer) return pdFALSE;
    sim_tcb_t *me  = tl_tcb;
    int64_t    dl  = sim_deadline(xTicksToWait);
    BaseType_t ret = pdFALSE;

    pthread_mutex_lock(&q->m);
    pthread_cleanup_push(sim_unlock_cb, &q->m);
    sim_block(me);
    for (;;) {
        if (me && me->deleted) pthread_exit(NULL);
        if (q->deleted) { ret = pdFALSE; break; }
        if (q->count > 0) {
            memcpy(pvBuffer, q->buf + q->head * q->item, q->item);
            ret = pdPASS;
            break;
        }
        if (xTicksToWait == 0) break;
        if (dl == INT64_MAX) {
            pthread_cond_wait(&q->cv_rx, &q->m);
            continue;
        }
        if (vboard_now_us() >= dl) break;
        sim_wait_until_locked(&q->cv_rx, &q->m, dl);
    }
    sim_unblock(me);
    pthread_cleanup_pop(0);
    pthread_mutex_unlock(&q->m);
    return ret;
}

BaseType_t xQueueReceiveFromISR(QueueHandle_t xQueue, void *pvBuffer,
                                BaseType_t *pxHigherPriorityTaskWoken)
{
    if (pxHigherPriorityTaskWoken) *pxHigherPriorityTaskWoken = pdFALSE;
    sim_qd_t *q = (sim_qd_t *)xQueue;
    if (!q || q->kind != SIM_OBJ_QUEUE || !pvBuffer) return pdFALSE;
    BaseType_t ret = pdFALSE;
    pthread_mutex_lock(&q->m);
    if (!q->deleted && q->count > 0) {
        memcpy(pvBuffer, q->buf + q->head * q->item, q->item);
        q->head = (q->head + 1) % q->len;
        q->count--;
        pthread_cond_signal(&q->cv_tx);
        ret = pdPASS;
    }
    pthread_mutex_unlock(&q->m);
    return ret;
}

BaseType_t xQueuePeekFromISR(QueueHandle_t xQueue, void *pvBuffer)
{
    sim_qd_t *q = (sim_qd_t *)xQueue;
    if (!q || q->kind != SIM_OBJ_QUEUE || !pvBuffer) return pdFALSE;
    BaseType_t ret = pdFALSE;
    pthread_mutex_lock(&q->m);
    if (!q->deleted && q->count > 0) {
        memcpy(pvBuffer, q->buf + q->head * q->item, q->item);
        ret = pdPASS;
    }
    pthread_mutex_unlock(&q->m);
    return ret;
}

BaseType_t xQueueReset(QueueHandle_t xQueue)
{
    sim_qd_t *q = (sim_qd_t *)xQueue;
    if (!q || q->kind != SIM_OBJ_QUEUE) return pdFALSE;
    pthread_mutex_lock(&q->m);
    q->head  = 0;
    q->count = 0;
    pthread_cond_broadcast(&q->cv_tx);       /* blocked senders proceed */
    pthread_mutex_unlock(&q->m);
    return pdPASS;
}

UBaseType_t uxQueueMessagesWaiting(const QueueHandle_t xQueue)
{
    sim_qd_t *q = (sim_qd_t *)xQueue;
    if (!q) return 0;
    pthread_mutex_lock(&q->m);
    UBaseType_t n = q->count;
    pthread_mutex_unlock(&q->m);
    return n;
}

UBaseType_t uxQueueMessagesWaitingFromISR(const QueueHandle_t xQueue)
{
    return uxQueueMessagesWaiting(xQueue);
}

UBaseType_t uxQueueSpacesAvailable(const QueueHandle_t xQueue)
{
    sim_qd_t *q = (sim_qd_t *)xQueue;
    if (!q) return 0;
    pthread_mutex_lock(&q->m);
    UBaseType_t n = q->len - q->count;
    pthread_mutex_unlock(&q->m);
    return n;
}

BaseType_t xQueueIsQueueEmptyFromISR(const QueueHandle_t xQueue)
{
    sim_qd_t *q = (sim_qd_t *)xQueue;
    if (!q) return pdTRUE;
    pthread_mutex_lock(&q->m);
    UBaseType_t n = q->count;
    pthread_mutex_unlock(&q->m);
    return n == 0 ? pdTRUE : pdFALSE;
}

BaseType_t xQueueIsQueueFullFromISR(const QueueHandle_t xQueue)
{
    sim_qd_t *q = (sim_qd_t *)xQueue;
    if (!q) return pdFALSE;
    pthread_mutex_lock(&q->m);
    UBaseType_t n = q->count;
    pthread_mutex_unlock(&q->m);
    return n >= q->len ? pdTRUE : pdFALSE;
}

void vQueueDelete(QueueHandle_t xQueue)
{
    sim_qd_t *q = (sim_qd_t *)xQueue;
    if (!q) return;
    pthread_mutex_lock(&q->m);
    q->deleted = 1;
    pthread_cond_broadcast(&q->cv_rx);
    pthread_cond_broadcast(&q->cv_tx);
    pthread_mutex_unlock(&q->m);
    /* storage intentionally leaked — waiters may still be draining */
}

/* -------------------------- semaphores ------------------------------- */

static SemaphoreHandle_t sim_sem_create(sim_obj_kind_t kind, long max,
                                        long initial)
{
    sim_qd_t *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->kind   = kind;
    s->smax   = max;
    s->scount = initial;
    pthread_mutex_init(&s->m, NULL);
    pthread_cond_init(&s->cv_rx, NULL);
    pthread_cond_init(&s->cv_tx, NULL);
    return (SemaphoreHandle_t)s;
}

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    return sim_sem_create(SIM_SEM_MUTEX, 1, 1);
}

SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *pxMutexBuffer)
{
    (void)pxMutexBuffer;
    return xSemaphoreCreateMutex();
}

SemaphoreHandle_t xSemaphoreCreateBinary(void)
{
    return sim_sem_create(SIM_SEM_BINARY, 1, 0);   /* created EMPTY */
}

SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t *pxBuffer)
{
    (void)pxBuffer;
    return xSemaphoreCreateBinary();
}

SemaphoreHandle_t xSemaphoreCreateCounting(const UBaseType_t uxMaxCount,
                                           const UBaseType_t uxInitialCount)
{
    if (uxMaxCount == 0 || uxInitialCount > uxMaxCount) return NULL;
    return sim_sem_create(SIM_SEM_COUNTING, (long)uxMaxCount,
                          (long)uxInitialCount);
}

SemaphoreHandle_t xSemaphoreCreateCountingStatic(const UBaseType_t uxMaxCount,
                                                 const UBaseType_t uxInitialCount,
                                                 StaticSemaphore_t *pxBuffer)
{
    (void)pxBuffer;
    return xSemaphoreCreateCounting(uxMaxCount, uxInitialCount);
}

SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void)
{
    return sim_sem_create(SIM_SEM_RECURSIVE, 1, 1);
}

SemaphoreHandle_t xSemaphoreCreateRecursiveMutexStatic(StaticSemaphore_t *pxBuffer)
{
    (void)pxBuffer;
    return xSemaphoreCreateRecursiveMutex();
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t xSemaphore,
                          TickType_t xTicksToWait)
{
    sim_qd_t *s = (sim_qd_t *)xSemaphore;
    if (!s || s->kind == SIM_OBJ_QUEUE) return pdFALSE;
    if (s->kind == SIM_SEM_RECURSIVE)
        return xSemaphoreTakeRecursive(xSemaphore, xTicksToWait);
    sim_tcb_t *me  = sim_self();   /* identity needed for mutex owner */
    int64_t    dl  = sim_deadline(xTicksToWait);
    BaseType_t ret = pdFALSE;

    pthread_mutex_lock(&s->m);
    pthread_cleanup_push(sim_unlock_cb, &s->m);
    sim_block(me);
    for (;;) {
        if (me && me->deleted) pthread_exit(NULL);
        if (s->deleted) { ret = pdFALSE; break; }
        if (s->scount > 0) {
            s->scount--;
            if (s->kind == SIM_SEM_MUTEX) s->owner = (TaskHandle_t)me;
            ret = pdPASS;
            break;
        }
        if (xTicksToWait == 0) break;
        if (dl == INT64_MAX) {
            pthread_cond_wait(&s->cv_rx, &s->m);
            continue;
        }
        if (vboard_now_us() >= dl) break;
        sim_wait_until_locked(&s->cv_rx, &s->m, dl);
    }
    sim_unblock(me);
    pthread_cleanup_pop(0);
    pthread_mutex_unlock(&s->m);
    return ret;
}

BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t xMutex,
                                   TickType_t xTicksToWait)
{
    sim_qd_t *s = (sim_qd_t *)xMutex;
    if (!s || s->kind != SIM_SEM_RECURSIVE) return pdFALSE;
    sim_tcb_t *me  = sim_self();
    int64_t    dl  = sim_deadline(xTicksToWait);
    BaseType_t ret = pdFALSE;

    pthread_mutex_lock(&s->m);
    pthread_cleanup_push(sim_unlock_cb, &s->m);
    if (s->owner == (TaskHandle_t)me) {
        /* already held by this task — recurse, no wait */
        s->rcount++;
        ret = pdTRUE;
    } else {
        sim_block(me);
        for (;;) {
            if (me->deleted) pthread_exit(NULL);
            if (s->deleted) { ret = pdFALSE; break; }
            if (s->scount > 0) {
                s->scount = 0;
                s->owner  = (TaskHandle_t)me;
                s->rcount = 1;
                ret = pdPASS;
                break;
            }
            if (xTicksToWait == 0) break;
            if (dl == INT64_MAX) {
                pthread_cond_wait(&s->cv_rx, &s->m);
                continue;
            }
            if (vboard_now_us() >= dl) break;
            sim_wait_until_locked(&s->cv_rx, &s->m, dl);
        }
        sim_unblock(me);
    }
    pthread_cleanup_pop(0);
    pthread_mutex_unlock(&s->m);
    return ret;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t xSemaphore)
{
    sim_qd_t *s = (sim_qd_t *)xSemaphore;
    if (!s || s->kind == SIM_OBJ_QUEUE) return pdFALSE;
    if (s->kind == SIM_SEM_RECURSIVE)
        return xSemaphoreGiveRecursive(xSemaphore);
    BaseType_t ret = pdPASS;
    pthread_mutex_lock(&s->m);
    if (s->scount >= s->smax) {
        ret = pdFALSE;                     /* give without take — full */
    } else {
        s->scount++;
        if (s->kind == SIM_SEM_MUTEX) s->owner = NULL;
        pthread_cond_signal(&s->cv_rx);
    }
    pthread_mutex_unlock(&s->m);
    return ret;
}

BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t xMutex)
{
    sim_qd_t *s = (sim_qd_t *)xMutex;
    if (!s || s->kind != SIM_SEM_RECURSIVE) return pdFALSE;
    sim_tcb_t *me = sim_self();
    BaseType_t ret = pdTRUE;
    pthread_mutex_lock(&s->m);
    if (s->owner != (TaskHandle_t)me || s->rcount <= 0) {
        ret = pdFALSE;
    } else if (--s->rcount == 0) {
        s->owner  = NULL;
        s->scount = 1;
        pthread_cond_signal(&s->cv_rx);
    }
    pthread_mutex_unlock(&s->m);
    return ret;
}

BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t xSemaphore,
                                 BaseType_t *pxHigherPriorityTaskWoken)
{
    if (pxHigherPriorityTaskWoken) *pxHigherPriorityTaskWoken = pdFALSE;
    return xSemaphoreGive(xSemaphore);
}

BaseType_t xSemaphoreTakeFromISR(SemaphoreHandle_t xSemaphore,
                                 BaseType_t *pxHigherPriorityTaskWoken)
{
    if (pxHigherPriorityTaskWoken) *pxHigherPriorityTaskWoken = pdFALSE;
    sim_qd_t *s = (sim_qd_t *)xSemaphore;
    if (!s || s->kind == SIM_OBJ_QUEUE ||
        s->kind == SIM_SEM_RECURSIVE) return pdFALSE;
    BaseType_t ret = pdFALSE;
    pthread_mutex_lock(&s->m);
    if (!s->deleted && s->scount > 0) {
        s->scount--;
        ret = pdTRUE;
    }
    pthread_mutex_unlock(&s->m);
    return ret;
}

void vSemaphoreDelete(SemaphoreHandle_t xSemaphore)
{
    sim_qd_t *s = (sim_qd_t *)xSemaphore;
    if (!s) return;
    pthread_mutex_lock(&s->m);
    s->deleted = 1;
    pthread_cond_broadcast(&s->cv_rx);
    pthread_cond_broadcast(&s->cv_tx);
    pthread_mutex_unlock(&s->m);
}

UBaseType_t uxSemaphoreGetCount(SemaphoreHandle_t xSemaphore)
{
    sim_qd_t *s = (sim_qd_t *)xSemaphore;
    if (!s) return 0;
    pthread_mutex_lock(&s->m);
    long n = s->scount;
    pthread_mutex_unlock(&s->m);
    return (UBaseType_t)n;
}

TaskHandle_t xSemaphoreGetMutexHolder(SemaphoreHandle_t xMutex)
{
    sim_qd_t *s = (sim_qd_t *)xMutex;
    if (!s) return NULL;
    pthread_mutex_lock(&s->m);
    TaskHandle_t h = s->owner;
    pthread_mutex_unlock(&s->m);
    return h;
}

TaskHandle_t xSemaphoreGetMutexHolderFromISR(SemaphoreHandle_t xMutex)
{
    return xSemaphoreGetMutexHolder(xMutex);
}

/* ================================================================== */
/*  event groups                                                       */
/* ================================================================== */

struct EventGroupDef_t {
    pthread_mutex_t m;
    pthread_cond_t  cv;
    EventBits_t     bits;
    _Atomic int     deleted;
};

typedef struct EventGroupDef_t sim_eg_t;

EventGroupHandle_t xEventGroupCreate(void)
{
    sim_eg_t *eg = calloc(1, sizeof(*eg));
    if (!eg) return NULL;
    pthread_mutex_init(&eg->m, NULL);
    pthread_cond_init(&eg->cv, NULL);
    return (EventGroupHandle_t)eg;
}

EventGroupHandle_t xEventGroupCreateStatic(StaticEventGroup_t *pxBuf)
{
    (void)pxBuf;
    return xEventGroupCreate();
}

EventBits_t xEventGroupWaitBits(EventGroupHandle_t xEventGroup,
                                const EventBits_t uxBitsToWaitFor,
                                const BaseType_t xClearOnExit,
                                const BaseType_t xWaitForAllBits,
                                TickType_t xTicksToWait)
{
    sim_eg_t *eg = (sim_eg_t *)xEventGroup;
    if (!eg) return 0;
    sim_tcb_t *me  = tl_tcb;
    int64_t    dl  = sim_deadline(xTicksToWait);
    EventBits_t ret = 0;

    pthread_mutex_lock(&eg->m);
    pthread_cleanup_push(sim_unlock_cb, &eg->m);
    sim_block(me);
    for (;;) {
        if (me && me->deleted) pthread_exit(NULL);
        EventBits_t cur = eg->bits;
        BaseType_t met  = xWaitForAllBits
            ? ((cur & uxBitsToWaitFor) == uxBitsToWaitFor)
            : ((cur & uxBitsToWaitFor) != 0);
        if (met) {
            if (xClearOnExit) eg->bits &= ~uxBitsToWaitFor;
            ret = cur;
            break;
        }
        if (eg->deleted) { ret = cur; break; }
        if (xTicksToWait == 0) { ret = cur; break; }
        if (dl == INT64_MAX) {
            pthread_cond_wait(&eg->cv, &eg->m);
            continue;
        }
        if (vboard_now_us() >= dl) { ret = cur; break; }
        sim_wait_until_locked(&eg->cv, &eg->m, dl);
    }
    sim_unblock(me);
    pthread_cleanup_pop(0);
    pthread_mutex_unlock(&eg->m);
    return ret;
}

EventBits_t xEventGroupSetBits(EventGroupHandle_t xEventGroup,
                               const EventBits_t uxBitsToSet)
{
    sim_eg_t *eg = (sim_eg_t *)xEventGroup;
    if (!eg) return 0;
    pthread_mutex_lock(&eg->m);
    eg->bits |= uxBitsToSet;
    EventBits_t cur = eg->bits;
    pthread_cond_broadcast(&eg->cv);
    pthread_mutex_unlock(&eg->m);
    return cur;
}

EventBits_t xEventGroupClearBits(EventGroupHandle_t xEventGroup,
                                 const EventBits_t uxBitsToClear)
{
    sim_eg_t *eg = (sim_eg_t *)xEventGroup;
    if (!eg) return 0;
    pthread_mutex_lock(&eg->m);
    EventBits_t cur = eg->bits;
    eg->bits &= ~uxBitsToClear;
    pthread_mutex_unlock(&eg->m);
    return cur;
}

EventBits_t xEventGroupSync(EventGroupHandle_t xEventGroup,
                            const EventBits_t uxBitsToSet,
                            const EventBits_t uxBitsToWaitFor,
                            TickType_t xTicksToWait)
{
    sim_eg_t *eg = (sim_eg_t *)xEventGroup;
    if (!eg) return 0;
    sim_tcb_t *me  = tl_tcb;
    int64_t    dl  = sim_deadline(xTicksToWait);
    EventBits_t ret = 0;

    pthread_mutex_lock(&eg->m);
    pthread_cleanup_push(sim_unlock_cb, &eg->m);
    eg->bits |= uxBitsToSet;
    pthread_cond_broadcast(&eg->cv);
    sim_block(me);
    for (;;) {
        if (me && me->deleted) pthread_exit(NULL);
        EventBits_t cur = eg->bits;
        if ((cur & uxBitsToWaitFor) == uxBitsToWaitFor) {
            eg->bits &= ~uxBitsToWaitFor;
            ret = cur;
            break;
        }
        if (eg->deleted) { ret = cur; break; }
        if (xTicksToWait == 0) { ret = cur; break; }
        if (dl == INT64_MAX) {
            pthread_cond_wait(&eg->cv, &eg->m);
            continue;
        }
        if (vboard_now_us() >= dl) { ret = cur; break; }
        sim_wait_until_locked(&eg->cv, &eg->m, dl);
    }
    sim_unblock(me);
    pthread_cleanup_pop(0);
    pthread_mutex_unlock(&eg->m);
    return ret;
}

BaseType_t xEventGroupSetBitsFromISR(EventGroupHandle_t xEventGroup,
                                     const EventBits_t uxBitsToSet,
                                     BaseType_t *pxHigherPriorityTaskWoken)
{
    if (pxHigherPriorityTaskWoken) *pxHigherPriorityTaskWoken = pdFALSE;
    if (!xEventGroup) return pdFAIL;
    (void)xEventGroupSetBits(xEventGroup, uxBitsToSet);
    return pdPASS;
}

BaseType_t xEventGroupClearBitsFromISR(EventGroupHandle_t xEventGroup,
                                       const EventBits_t uxBitsToClear)
{
    if (!xEventGroup) return pdFAIL;
    (void)xEventGroupClearBits(xEventGroup, uxBitsToClear);
    return pdPASS;
}

EventBits_t xEventGroupGetBitsFromISR(EventGroupHandle_t xEventGroup)
{
    sim_eg_t *eg = (sim_eg_t *)xEventGroup;
    if (!eg) return 0;
    pthread_mutex_lock(&eg->m);
    EventBits_t cur = eg->bits;
    pthread_mutex_unlock(&eg->m);
    return cur;
}

void vEventGroupDelete(EventGroupHandle_t xEventGroup)
{
    sim_eg_t *eg = (sim_eg_t *)xEventGroup;
    if (!eg) return;
    pthread_mutex_lock(&eg->m);
    eg->deleted = 1;
    pthread_cond_broadcast(&eg->cv);
    pthread_mutex_unlock(&eg->m);
}

/* ================================================================== */
/*  software timers — ONE shared daemon thread                         */
/* ================================================================== */

struct tmrTimerControl {
    char                    *name;
    _Atomic TickType_t       period_ticks;
    _Atomic int64_t          period_us;
    _Atomic int              reload;
    _Atomic(void *)          id;
    TimerCallbackFunction_t  cb;
    _Atomic int              active;
    _Atomic int              deleted;
    int                      in_list;      /* under g_tm_m            */
    _Atomic int64_t          expiry_us;   /* virtual                  */
    struct tmrTimerControl  *next;        /* sorted by expiry        */
};

typedef struct tmrTimerControl sim_tmr_t;

typedef struct sim_pfn {
    PendedFunction_t  fn;
    void             *a1;
    uint32_t          a2;
    struct sim_pfn   *next;
} sim_pfn_t;

static pthread_mutex_t g_tm_m   = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_tm_cv  = PTHREAD_COND_INITIALIZER;
static sim_tmr_t      *g_tm_head;
static sim_pfn_t      *g_pfn_head, *g_pfn_tail;
static int             g_tm_started;

static void sim_tm_insert(sim_tmr_t *t)
{
    sim_tmr_t **pp = &g_tm_head;
    while (*pp && (*pp)->expiry_us <= t->expiry_us) pp = &(*pp)->next;
    t->next   = *pp;
    *pp       = t;
    t->in_list = 1;
}

static void sim_tm_unlink(sim_tmr_t *t)
{
    sim_tmr_t **pp = &g_tm_head;
    while (*pp && *pp != t) pp = &(*pp)->next;
    if (*pp) *pp = t->next;
    t->next    = NULL;
    t->in_list = 0;
}

static void *sim_timer_daemon(void *arg)
{
    (void)arg;
    for (;;) {
        pthread_mutex_lock(&g_tm_m);

        /* drain pended functions first (FIFO) */
        while (g_pfn_head) {
            sim_pfn_t *p = g_pfn_head;
            g_pfn_head = p->next;
            if (!g_pfn_head) g_pfn_tail = NULL;
            pthread_mutex_unlock(&g_tm_m);
            p->fn(p->a1, p->a2);
            free(p);
            pthread_mutex_lock(&g_tm_m);
        }

        sim_tmr_t *t = g_tm_head;
        if (!t) {
            pthread_cond_wait(&g_tm_cv, &g_tm_m);
            pthread_mutex_unlock(&g_tm_m);
            continue;
        }
        int64_t now = vboard_now_us();
        if (t->expiry_us > now) {
            int64_t w = sim_virt_to_wall_us(t->expiry_us - now);
            if (w > SIM_WALL_SLICE_US) w = SIM_WALL_SLICE_US;
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec  += w / 1000000;
            ts.tv_nsec += (w % 1000000) * 1000;
            if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
            pthread_cond_timedwait(&g_tm_cv, &g_tm_m, &ts);
            pthread_mutex_unlock(&g_tm_m);
            continue;
        }

        /* expired — pop, reschedule or retire, fire callback unlocked */
        sim_tm_unlink(t);
        if (t->deleted) { pthread_mutex_unlock(&g_tm_m); continue; }
        TimerCallbackFunction_t cb = t->cb;
        if (t->reload) {
            t->expiry_us += t->period_us;
            if (t->expiry_us <= now) t->expiry_us = now + t->period_us;
            sim_tm_insert(t);
        } else {
            t->active = 0;
        }
        pthread_mutex_unlock(&g_tm_m);
        if (cb) cb((TimerHandle_t)t);
    }
    return NULL;
}

static void sim_timer_daemon_start(void)
{
    pthread_mutex_lock(&g_tm_m);
    if (!g_tm_started) {
        pthread_t th;
        pthread_attr_t at;
        pthread_attr_init(&at);
        pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&th, &at, sim_timer_daemon, NULL) == 0)
            g_tm_started = 1;
        pthread_attr_destroy(&at);
    }
    pthread_mutex_unlock(&g_tm_m);
}

TimerHandle_t xTimerCreate(const char *pcTimerName,
                           const TickType_t xTimerPeriodInTicks,
                           const UBaseType_t uxAutoReload,
                           void *pvTimerID,
                           TimerCallbackFunction_t pxCallbackFunction)
{
    if (!pxCallbackFunction || xTimerPeriodInTicks == 0) return NULL;
    sim_tmr_t *t = calloc(1, sizeof(*t));
    if (!t) return NULL;
    t->name         = pcTimerName ? strdup(pcTimerName) : NULL;
    t->period_ticks = xTimerPeriodInTicks;
    t->period_us    = (int64_t)xTimerPeriodInTicks * 1000;
    t->reload       = (uxAutoReload != 0);
    t->id           = pvTimerID;
    t->cb           = pxCallbackFunction;
    sim_timer_daemon_start();
    return (TimerHandle_t)t;
}

TimerHandle_t xTimerCreateStatic(const char *pcTimerName,
                                 const TickType_t xTimerPeriodInTicks,
                                 const UBaseType_t uxAutoReload,
                                 void *pvTimerID,
                                 TimerCallbackFunction_t pxCallbackFunction,
                                 StaticTimer_t *pxTimerBuffer)
{
    (void)pxTimerBuffer;
    return xTimerCreate(pcTimerName, xTimerPeriodInTicks, uxAutoReload,
                        pvTimerID, pxCallbackFunction);
}

static BaseType_t sim_tmr_start(sim_tmr_t *t)
{
    if (!t || t->deleted) return pdFAIL;
    pthread_mutex_lock(&g_tm_m);
    t->expiry_us = vboard_now_us() + t->period_us;
    t->active    = 1;
    if (t->in_list) sim_tm_unlink(t);
    sim_tm_insert(t);
    pthread_cond_signal(&g_tm_cv);
    pthread_mutex_unlock(&g_tm_m);
    return pdPASS;
}

BaseType_t xTimerStart(TimerHandle_t xTimer, TickType_t xTicksToWait)
{
    (void)xTicksToWait;
    return sim_tmr_start((sim_tmr_t *)xTimer);
}

BaseType_t xTimerReset(TimerHandle_t xTimer, TickType_t xTicksToWait)
{
    return xTimerStart(xTimer, xTicksToWait);
}

BaseType_t xTimerStop(TimerHandle_t xTimer, TickType_t xTicksToWait)
{
    (void)xTicksToWait;
    sim_tmr_t *t = (sim_tmr_t *)xTimer;
    if (!t || t->deleted) return pdFAIL;
    pthread_mutex_lock(&g_tm_m);
    t->active = 0;
    if (t->in_list) sim_tm_unlink(t);
    pthread_cond_signal(&g_tm_cv);
    pthread_mutex_unlock(&g_tm_m);
    return pdPASS;
}

BaseType_t xTimerDelete(TimerHandle_t xTimer, TickType_t xTicksToWait)
{
    (void)xTicksToWait;
    sim_tmr_t *t = (sim_tmr_t *)xTimer;
    if (!t) return pdFAIL;
    pthread_mutex_lock(&g_tm_m);
    t->deleted = 1;
    t->active  = 0;
    if (t->in_list) sim_tm_unlink(t);
    pthread_cond_signal(&g_tm_cv);
    pthread_mutex_unlock(&g_tm_m);
    /* struct intentionally leaked — callers may still hold the handle */
    return pdPASS;
}

BaseType_t xTimerChangePeriod(TimerHandle_t xTimer,
                              const TickType_t xNewPeriod,
                              TickType_t xTicksToWait)
{
    (void)xTicksToWait;
    sim_tmr_t *t = (sim_tmr_t *)xTimer;
    if (!t || t->deleted || xNewPeriod == 0) return pdFAIL;
    pthread_mutex_lock(&g_tm_m);
    t->period_ticks = xNewPeriod;
    t->period_us    = (int64_t)xNewPeriod * 1000;
    if (t->active) {
        t->expiry_us = vboard_now_us() + t->period_us;
        if (t->in_list) sim_tm_unlink(t);
        sim_tm_insert(t);
    }
    pthread_cond_signal(&g_tm_cv);
    pthread_mutex_unlock(&g_tm_m);
    return pdPASS;
}

BaseType_t xTimerStartFromISR(TimerHandle_t xTimer,
                              BaseType_t *pxHigherPriorityTaskWoken)
{
    if (pxHigherPriorityTaskWoken) *pxHigherPriorityTaskWoken = pdFALSE;
    return sim_tmr_start((sim_tmr_t *)xTimer);
}

BaseType_t xTimerStopFromISR(TimerHandle_t xTimer,
                             BaseType_t *pxHigherPriorityTaskWoken)
{
    if (pxHigherPriorityTaskWoken) *pxHigherPriorityTaskWoken = pdFALSE;
    return xTimerStop(xTimer, 0);
}

BaseType_t xTimerResetFromISR(TimerHandle_t xTimer,
                              BaseType_t *pxHigherPriorityTaskWoken)
{
    if (pxHigherPriorityTaskWoken) *pxHigherPriorityTaskWoken = pdFALSE;
    return sim_tmr_start((sim_tmr_t *)xTimer);
}

BaseType_t xTimerChangePeriodFromISR(TimerHandle_t xTimer,
                                     const TickType_t xNewPeriod,
                                     BaseType_t *pxHigherPriorityTaskWoken)
{
    if (pxHigherPriorityTaskWoken) *pxHigherPriorityTaskWoken = pdFALSE;
    return xTimerChangePeriod(xTimer, xNewPeriod, 0);
}

BaseType_t xTimerIsTimerActive(TimerHandle_t xTimer)
{
    sim_tmr_t *t = (sim_tmr_t *)xTimer;
    return (t && !t->deleted && t->active) ? pdTRUE : pdFALSE;
}

void *pvTimerGetTimerID(const TimerHandle_t xTimer)
{
    const sim_tmr_t *t = (const sim_tmr_t *)xTimer;
    return t ? t->id : NULL;
}

void vTimerSetTimerID(TimerHandle_t xTimer, void *pvNewID)
{
    sim_tmr_t *t = (sim_tmr_t *)xTimer;
    if (t) t->id = pvNewID;
}

TickType_t xTimerGetPeriod(TimerHandle_t xTimer)
{
    sim_tmr_t *t = (sim_tmr_t *)xTimer;
    return t ? t->period_ticks : 0;
}

TickType_t xTimerGetExpiryTime(TimerHandle_t xTimer)
{
    sim_tmr_t *t = (sim_tmr_t *)xTimer;
    return t ? (TickType_t)(t->expiry_us / 1000) : 0;
}

const char *pcTimerGetName(TimerHandle_t xTimer)
{
    sim_tmr_t *t = (sim_tmr_t *)xTimer;
    return (t && t->name) ? t->name : "";
}

BaseType_t xTimerGetReloadMode(TimerHandle_t xTimer)
{
    sim_tmr_t *t = (sim_tmr_t *)xTimer;
    return (t && t->reload) ? pdTRUE : pdFALSE;
}

UBaseType_t uxTimerGetReloadMode(TimerHandle_t xTimer)
{
    sim_tmr_t *t = (sim_tmr_t *)xTimer;
    return (t && t->reload) ? 1u : 0u;
}

void vTimerSetReloadMode(TimerHandle_t xTimer, const UBaseType_t uxAutoReload)
{
    sim_tmr_t *t = (sim_tmr_t *)xTimer;
    if (t) t->reload = (uxAutoReload != 0);
}

BaseType_t xTimerPendFunctionCall(PendedFunction_t xFunctionToPend,
                                  void *pvParameter1,
                                  uint32_t ulParameter2,
                                  TickType_t xTicksToWait)
{
    (void)xTicksToWait;
    if (!xFunctionToPend) return pdFAIL;
    sim_pfn_t *p = calloc(1, sizeof(*p));
    if (!p) return pdFAIL;
    p->fn = xFunctionToPend;
    p->a1 = pvParameter1;
    p->a2 = ulParameter2;
    sim_timer_daemon_start();
    pthread_mutex_lock(&g_tm_m);
    if (g_pfn_tail) g_pfn_tail->next = p;
    else            g_pfn_head       = p;
    g_pfn_tail = p;
    pthread_cond_signal(&g_tm_cv);
    pthread_mutex_unlock(&g_tm_m);
    return pdPASS;
}

BaseType_t xTimerPendFunctionCallFromISR(PendedFunction_t xFunctionToPend,
                                         void *pvParameter1,
                                         uint32_t ulParameter2,
                                         BaseType_t *pxHigherPriorityTaskWoken)
{
    if (pxHigherPriorityTaskWoken) *pxHigherPriorityTaskWoken = pdFALSE;
    return xTimerPendFunctionCall(xFunctionToPend, pvParameter1,
                                  ulParameter2, 0);
}

/* ================================================================== */
/*  lists                                                              */
/* ================================================================== */

void vListInitialise(List_t *pxList)
{
    pxList->pxIndex               = (ListItem_t *)&pxList->xListEnd;
    pxList->xListEnd.xItemValue   = portMAX_DELAY;
    pxList->xListEnd.pxNext       = (ListItem_t *)&pxList->xListEnd;
    pxList->xListEnd.pxPrevious   = (ListItem_t *)&pxList->xListEnd;
    pxList->uxNumberOfItems       = 0;
}

void vListInitialiseItem(ListItem_t *pxItem)
{
    pxItem->pxContainer = NULL;
}

void vListInsertEnd(List_t *pxList, ListItem_t *pxNewListItem)
{
    ListItem_t *idx = pxList->pxIndex;
    pxNewListItem->pxNext         = idx;
    pxNewListItem->pxPrevious     = idx->pxPrevious;
    idx->pxPrevious->pxNext       = pxNewListItem;
    idx->pxPrevious               = pxNewListItem;
    pxNewListItem->pxContainer    = pxList;
    pxList->uxNumberOfItems++;
}

void vListInsert(List_t *pxList, ListItem_t *pxNewListItem)
{
    ListItem_t *it = (ListItem_t *)&pxList->xListEnd;
    while (it->pxNext->xItemValue <= pxNewListItem->xItemValue)
        it = it->pxNext;
    pxNewListItem->pxNext         = it->pxNext;
    pxNewListItem->pxPrevious     = it;
    it->pxNext->pxPrevious        = pxNewListItem;
    it->pxNext                    = pxNewListItem;
    pxNewListItem->pxContainer    = pxList;
    pxList->uxNumberOfItems++;
}

UBaseType_t uxListRemove(ListItem_t *pxItemToRemove)
{
    List_t *l = (List_t *)pxItemToRemove->pxContainer;
    if (!l) return 0;
    pxItemToRemove->pxPrevious->pxNext = pxItemToRemove->pxNext;
    pxItemToRemove->pxNext->pxPrevious = pxItemToRemove->pxPrevious;
    if (l->pxIndex == pxItemToRemove)
        l->pxIndex = pxItemToRemove->pxPrevious;
    pxItemToRemove->pxContainer = NULL;
    l->uxNumberOfItems--;
    return l->uxNumberOfItems;
}

/* ================================================================== */
/*  stream / message buffers                                           */
/* ================================================================== */

struct StreamBufferDef_t {
    pthread_mutex_t m;
    pthread_cond_t  cv_rx, cv_tx;
    uint8_t        *buf;
    size_t          len;
    size_t          head;           /* read position                */
    size_t          count;
    size_t          trigger;
    _Atomic int     deleted;
};

typedef struct StreamBufferDef_t sim_sb_t;

static void sb_write(sim_sb_t *s, const uint8_t *p, size_t n)
{
    size_t tail = (s->head + s->count) % s->len;
    size_t first = s->len - tail;
    if (first > n) first = n;
    memcpy(s->buf + tail, p, first);
    memcpy(s->buf, p + first, n - first);
    s->count += n;
}

static void sb_read(sim_sb_t *s, uint8_t *p, size_t n)
{
    size_t first = s->len - s->head;
    if (first > n) first = n;
    memcpy(p, s->buf + s->head, first);
    memcpy(p + first, s->buf, n - first);
    s->head   = (s->head + n) % s->len;
    s->count -= n;
}

static void sb_peek(sim_sb_t *s, uint8_t *p, size_t n, size_t off)
{
    size_t pos   = (s->head + off) % s->len;
    size_t first = s->len - pos;
    if (first > n) first = n;
    memcpy(p, s->buf + pos, first);
    memcpy(p + first, s->buf, n - first);
}

StreamBufferHandle_t xStreamBufferCreate(size_t xBufferSizeBytes,
                                         size_t xTriggerLevelBytes)
{
    if (xBufferSizeBytes == 0) return NULL;
    sim_sb_t *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->buf = malloc(xBufferSizeBytes);
    if (!s->buf) { free(s); return NULL; }
    s->len     = xBufferSizeBytes;
    s->trigger = xTriggerLevelBytes ? xTriggerLevelBytes : 1;
    pthread_mutex_init(&s->m, NULL);
    pthread_cond_init(&s->cv_rx, NULL);
    pthread_cond_init(&s->cv_tx, NULL);
    return (StreamBufferHandle_t)s;
}

StreamBufferHandle_t xStreamBufferCreateStatic(size_t xBufferSizeBytes,
                                               size_t xTriggerLevelBytes,
                                               uint8_t *pucStorage,
                                               StaticStreamBuffer_t *pxStatic)
{
    (void)pucStorage; (void)pxStatic;
    return xStreamBufferCreate(xBufferSizeBytes, xTriggerLevelBytes);
}

void vStreamBufferDelete(StreamBufferHandle_t xStreamBuffer)
{
    sim_sb_t *s = (sim_sb_t *)xStreamBuffer;
    if (!s) return;
    pthread_mutex_lock(&s->m);
    s->deleted = 1;
    pthread_cond_broadcast(&s->cv_rx);
    pthread_cond_broadcast(&s->cv_tx);
    pthread_mutex_unlock(&s->m);
}

size_t xStreamBufferSend(StreamBufferHandle_t xStreamBuffer,
                         const void *pvTxData, size_t xDataLengthBytes,
                         TickType_t xTicksToWait)
{
    sim_sb_t *s = (sim_sb_t *)xStreamBuffer;
    if (!s || (!pvTxData && xDataLengthBytes)) return 0;
    int64_t dl = sim_deadline(xTicksToWait);
    size_t  sent = 0;

    pthread_mutex_lock(&s->m);
    pthread_cleanup_push(sim_unlock_cb, &s->m);
    while (sent < xDataLengthBytes) {
        if (s->deleted) break;
        size_t avail = s->len - s->count;
        if (avail) {
            size_t n = avail < xDataLengthBytes - sent
                     ? avail : xDataLengthBytes - sent;
            sb_write(s, (const uint8_t *)pvTxData + sent, n);
            sent += n;
            pthread_cond_broadcast(&s->cv_rx);
            continue;
        }
        if (xTicksToWait == 0) break;
        if (dl == INT64_MAX) {
            pthread_cond_wait(&s->cv_tx, &s->m);
            continue;
        }
        if (vboard_now_us() >= dl) break;
        sim_wait_until_locked(&s->cv_tx, &s->m, dl);
    }
    pthread_cleanup_pop(0);
    pthread_mutex_unlock(&s->m);
    return sent;
}

size_t xStreamBufferSendFromISR(StreamBufferHandle_t xStreamBuffer,
                                const void *pvTxData, size_t xDataLengthBytes,
                                BaseType_t *pxHigherPriorityTaskWoken)
{
    if (pxHigherPriorityTaskWoken) *pxHigherPriorityTaskWoken = pdFALSE;
    sim_sb_t *s = (sim_sb_t *)xStreamBuffer;
    if (!s || (!pvTxData && xDataLengthBytes)) return 0;
    size_t sent = 0;
    pthread_mutex_lock(&s->m);
    if (!s->deleted) {
        size_t avail = s->len - s->count;
        size_t n = avail < xDataLengthBytes ? avail : xDataLengthBytes;
        if (n) {
            sb_write(s, pvTxData, n);
            sent = n;
            pthread_cond_broadcast(&s->cv_rx);
        }
    }
    pthread_mutex_unlock(&s->m);
    return sent;
}

size_t xStreamBufferReceive(StreamBufferHandle_t xStreamBuffer,
                            void *pvRxData, size_t xBufferLengthBytes,
                            TickType_t xTicksToWait)
{
    sim_sb_t *s = (sim_sb_t *)xStreamBuffer;
    if (!s || (!pvRxData && xBufferLengthBytes)) return 0;
    int64_t dl = sim_deadline(xTicksToWait);
    size_t  got = 0;

    pthread_mutex_lock(&s->m);
    pthread_cleanup_push(sim_unlock_cb, &s->m);
    for (;;) {
        if (s->deleted) break;
        /* wake at trigger level, then deliver what's available */
        if (s->count >= s->trigger && s->count > 0) {
            got = s->count < xBufferLengthBytes
                ? s->count : xBufferLengthBytes;
            if (got) sb_read(s, (uint8_t *)pvRxData, got);
            pthread_cond_broadcast(&s->cv_tx);
            break;
        }
        if (xTicksToWait == 0) break;
        if (dl == INT64_MAX) {
            pthread_cond_wait(&s->cv_rx, &s->m);
            continue;
        }
        if (vboard_now_us() >= dl) {
            /* timeout — deliver partial data if any */
            if (s->count) {
                got = s->count < xBufferLengthBytes
                    ? s->count : xBufferLengthBytes;
                sb_read(s, (uint8_t *)pvRxData, got);
                pthread_cond_broadcast(&s->cv_tx);
            }
            break;
        }
        sim_wait_until_locked(&s->cv_rx, &s->m, dl);
    }
    pthread_cleanup_pop(0);
    pthread_mutex_unlock(&s->m);
    return got;
}

size_t xStreamBufferReceiveFromISR(StreamBufferHandle_t xStreamBuffer,
                                   void *pvRxData, size_t xBufferLengthBytes,
                                   BaseType_t *pxHigherPriorityTaskWoken)
{
    if (pxHigherPriorityTaskWoken) *pxHigherPriorityTaskWoken = pdFALSE;
    sim_sb_t *s = (sim_sb_t *)xStreamBuffer;
    if (!s || !pvRxData) return 0;
    size_t got = 0;
    pthread_mutex_lock(&s->m);
    if (!s->deleted && s->count) {
        size_t n = s->count < xBufferLengthBytes
                 ? s->count : xBufferLengthBytes;
        sb_read(s, pvRxData, n);
        got = n;
        pthread_cond_broadcast(&s->cv_tx);
    }
    pthread_mutex_unlock(&s->m);
    return got;
}

BaseType_t xStreamBufferReset(StreamBufferHandle_t xStreamBuffer)
{
    sim_sb_t *s = (sim_sb_t *)xStreamBuffer;
    if (!s) return pdFALSE;
    pthread_mutex_lock(&s->m);
    s->head = 0; s->count = 0;
    pthread_cond_broadcast(&s->cv_tx);
    pthread_mutex_unlock(&s->m);
    return pdPASS;
}

BaseType_t xStreamBufferSetTriggerLevel(StreamBufferHandle_t xStreamBuffer,
                                        size_t xTriggerLevel)
{
    sim_sb_t *s = (sim_sb_t *)xStreamBuffer;
    if (!s || xTriggerLevel == 0 || xTriggerLevel > s->len) return pdFALSE;
    pthread_mutex_lock(&s->m);
    s->trigger = xTriggerLevel;
    pthread_mutex_unlock(&s->m);
    return pdTRUE;
}

size_t xStreamBufferBytesAvailable(StreamBufferHandle_t xStreamBuffer)
{
    sim_sb_t *s = (sim_sb_t *)xStreamBuffer;
    if (!s) return 0;
    pthread_mutex_lock(&s->m);
    size_t n = s->count;
    pthread_mutex_unlock(&s->m);
    return n;
}

size_t xStreamBufferSpacesAvailable(StreamBufferHandle_t xStreamBuffer)
{
    sim_sb_t *s = (sim_sb_t *)xStreamBuffer;
    if (!s) return 0;
    pthread_mutex_lock(&s->m);
    size_t n = s->len - s->count;
    pthread_mutex_unlock(&s->m);
    return n;
}

BaseType_t xStreamBufferIsEmpty(StreamBufferHandle_t xStreamBuffer)
{
    return xStreamBufferBytesAvailable(xStreamBuffer) == 0 ? pdTRUE : pdFALSE;
}

BaseType_t xStreamBufferIsFull(StreamBufferHandle_t xStreamBuffer)
{
    return xStreamBufferSpacesAvailable(xStreamBuffer) == 0 ? pdTRUE : pdFALSE;
}

BaseType_t xStreamBufferSendCompleted(StreamBufferHandle_t xStreamBuffer)
{
    (void)xStreamBuffer;
    return pdPASS;
}

BaseType_t xStreamBufferReceiveCompleted(StreamBufferHandle_t xStreamBuffer)
{
    (void)xStreamBuffer;
    return pdPASS;
}

/* -------- message buffers (length-prefixed records) ------------------ */

#define SB_LEN_BYTES  sizeof(size_t)

size_t xMessageBufferSend(MessageBufferHandle_t xMessageBuffer,
                          const void *pvTxData, size_t xDataLengthBytes,
                          TickType_t xTicksToWait)
{
    sim_sb_t *s = (sim_sb_t *)xMessageBuffer;
    if (!s || (!pvTxData && xDataLengthBytes)) return 0;
    size_t  need = xDataLengthBytes + SB_LEN_BYTES;
    if (need > s->len) return 0;
    int64_t dl = sim_deadline(xTicksToWait);
    size_t  done = 0;

    pthread_mutex_lock(&s->m);
    pthread_cleanup_push(sim_unlock_cb, &s->m);
    for (;;) {
        if (s->deleted) break;
        if (s->len - s->count >= need) {
            size_t hdr = xDataLengthBytes;
            sb_write(s, (const uint8_t *)&hdr, SB_LEN_BYTES);
            sb_write(s, pvTxData, xDataLengthBytes);
            done = xDataLengthBytes;
            pthread_cond_broadcast(&s->cv_rx);
            break;
        }
        if (xTicksToWait == 0) break;
        if (dl == INT64_MAX) {
            pthread_cond_wait(&s->cv_tx, &s->m);
            continue;
        }
        if (vboard_now_us() >= dl) break;
        sim_wait_until_locked(&s->cv_tx, &s->m, dl);
    }
    pthread_cleanup_pop(0);
    pthread_mutex_unlock(&s->m);
    return done;
}

size_t xMessageBufferSendFromISR(MessageBufferHandle_t xMessageBuffer,
                                 const void *pvTxData,
                                 size_t xDataLengthBytes,
                                 BaseType_t *pxHigherPriorityTaskWoken)
{
    if (pxHigherPriorityTaskWoken) *pxHigherPriorityTaskWoken = pdFALSE;
    sim_sb_t *s = (sim_sb_t *)xMessageBuffer;
    if (!s || (!pvTxData && xDataLengthBytes)) return 0;
    size_t need = xDataLengthBytes + SB_LEN_BYTES;
    size_t done = 0;
    pthread_mutex_lock(&s->m);
    if (!s->deleted && need <= s->len && s->len - s->count >= need) {
        size_t hdr = xDataLengthBytes;
        sb_write(s, (const uint8_t *)&hdr, SB_LEN_BYTES);
        sb_write(s, pvTxData, xDataLengthBytes);
        done = xDataLengthBytes;
        pthread_cond_broadcast(&s->cv_rx);
    }
    pthread_mutex_unlock(&s->m);
    return done;
}

size_t xMessageBufferReceive(MessageBufferHandle_t xMessageBuffer,
                             void *pvRxData, size_t xBufferLengthBytes,
                             TickType_t xTicksToWait)
{
    sim_sb_t *s = (sim_sb_t *)xMessageBuffer;
    if (!s || (!pvRxData && xBufferLengthBytes)) return 0;
    int64_t dl = sim_deadline(xTicksToWait);
    size_t  got = 0;

    pthread_mutex_lock(&s->m);
    pthread_cleanup_push(sim_unlock_cb, &s->m);
    for (;;) {
        if (s->deleted) break;
        if (s->count >= SB_LEN_BYTES) {
            size_t mlen;
            sb_peek(s, (uint8_t *)&mlen, SB_LEN_BYTES, 0);
            size_t hdr = SB_LEN_BYTES;
            /* consume header + message (truncate over-long payloads) */
            sb_read(s, (uint8_t *)&hdr, SB_LEN_BYTES);
            size_t take = mlen < xBufferLengthBytes ? mlen : xBufferLengthBytes;
            if (take) sb_read(s, (uint8_t *)pvRxData, take);
            if (mlen > take) {                      /* discard remainder */
                uint8_t junk[64];
                size_t left = mlen - take;
                while (left) {
                    size_t n = left < sizeof(junk) ? left : sizeof(junk);
                    sb_read(s, junk, n);
                    left -= n;
                }
            }
            got = take;
            pthread_cond_broadcast(&s->cv_tx);
            break;
        }
        if (xTicksToWait == 0) break;
        if (dl == INT64_MAX) {
            pthread_cond_wait(&s->cv_rx, &s->m);
            continue;
        }
        if (vboard_now_us() >= dl) break;
        sim_wait_until_locked(&s->cv_rx, &s->m, dl);
    }
    pthread_cleanup_pop(0);
    pthread_mutex_unlock(&s->m);
    return got;
}

size_t xMessageBufferReceiveFromISR(MessageBufferHandle_t xMessageBuffer,
                                    void *pvRxData, size_t xBufferLengthBytes,
                                    BaseType_t *pxHigherPriorityTaskWoken)
{
    if (pxHigherPriorityTaskWoken) *pxHigherPriorityTaskWoken = pdFALSE;
    sim_sb_t *s = (sim_sb_t *)xMessageBuffer;
    if (!s || !pvRxData) return 0;
    size_t got = 0;
    pthread_mutex_lock(&s->m);
    if (!s->deleted && s->count >= SB_LEN_BYTES) {
        size_t mlen;
        sb_peek(s, (uint8_t *)&mlen, SB_LEN_BYTES, 0);
        if (mlen <= xBufferLengthBytes && s->count >= SB_LEN_BYTES + mlen) {
            size_t hdr;
            sb_read(s, (uint8_t *)&hdr, SB_LEN_BYTES);
            sb_read(s, (uint8_t *)pvRxData, mlen);
            got = mlen;
            pthread_cond_broadcast(&s->cv_tx);
        }
    }
    pthread_mutex_unlock(&s->m);
    return got;
}

size_t xMessageBufferNextLengthBytes(MessageBufferHandle_t xMessageBuffer)
{
    sim_sb_t *s = (sim_sb_t *)xMessageBuffer;
    if (!s) return 0;
    size_t mlen = 0;
    pthread_mutex_lock(&s->m);
    if (s->count >= SB_LEN_BYTES)
        sb_peek(s, (uint8_t *)&mlen, SB_LEN_BYTES, 0);
    pthread_mutex_unlock(&s->m);
    return mlen;
}

size_t xMessageBufferSpaceAvailable(MessageBufferHandle_t xMessageBuffer)
{
    size_t n = xStreamBufferSpacesAvailable((StreamBufferHandle_t)xMessageBuffer);
    return n > SB_LEN_BYTES ? n - SB_LEN_BYTES : 0;
}

BaseType_t xMessageBufferReset(MessageBufferHandle_t xMessageBuffer)
{
    return xStreamBufferReset((StreamBufferHandle_t)xMessageBuffer);
}

BaseType_t xMessageBufferIsEmpty(MessageBufferHandle_t xMessageBuffer)
{
    return xStreamBufferIsEmpty((StreamBufferHandle_t)xMessageBuffer);
}

BaseType_t xMessageBufferIsFull(MessageBufferHandle_t xMessageBuffer)
{
    return xStreamBufferIsFull((StreamBufferHandle_t)xMessageBuffer);
}

/* ================================================================== */
/*  port / scheduler stubs                                             */
/* ================================================================== */

void     *pxPortInitialiseStack(StackType_t *pxTopOfStack,
                                TaskFunction_t pxCode, void *pvParameters)
{
    (void)pxTopOfStack; (void)pxCode; (void)pvParameters;
    return NULL;
}

BaseType_t xPortStartScheduler(void)                 { return pdPASS; }
void       vPortEndScheduler(void)                   { }
void       vPortYield(void)                          { sched_yield(); }
void       vPortYieldOtherCore(BaseType_t coreid)    { (void)coreid; sched_yield(); }
BaseType_t xPortInIsrContext(void)                   { return pdFALSE; }
BaseType_t xPortInterruptedFromISRContext(void)      { return pdFALSE; }
uint32_t   xPortGetTickRateHz(void)                  { return configTICK_RATE_HZ; }
