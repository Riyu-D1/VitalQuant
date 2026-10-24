/* esp_event.h — host-sim shim of the ESP-IDF event loop library.
 *
 * Faithful surface for firmware/vitalq: a single default loop whose
 * handlers run serialised on a dedicated "sys event" thread (pthread),
 * exactly like the target's sys_evt task. esp_event_post() copies the
 * payload into the queue — handlers always see stable memory.
 *
 * Implemented by sim/esp_shim/net/event_sim.c.
 */
#ifndef ESP_EVENT_H_
#define ESP_EVENT_H_

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"   /* TickType_t */

#ifdef __cplusplus
extern "C" {
#endif

/* ---- base types (esp_event_base.h folded in — sim keeps one header) ---- */

#define ESP_EVENT_DECLARE_BASE(id) extern esp_event_base_t const id
#define ESP_EVENT_DEFINE_BASE(id)  esp_event_base_t const id = #id

typedef const char*  esp_event_base_t;          /**< unique pointer to a subsystem that exposes events */
typedef void*        esp_event_loop_handle_t;   /**< identifies an event loop */
typedef void*        esp_event_handler_instance_t; /**< context identifying a registered handler instance */
typedef void (*esp_event_handler_t)(void* event_handler_arg,
                                    esp_event_base_t event_base,
                                    int32_t event_id,
                                    void* event_data); /**< function called when an event is posted */

#define ESP_EVENT_ANY_BASE     NULL             /**< register handler for any event base */
#define ESP_EVENT_ANY_ID       -1               /**< register handler for any event id */

/// Configuration for creating event loops
typedef struct {
    int32_t queue_size;                         /**< size of the event loop queue */
    const char *task_name;                      /**< name of the event loop task */
    UBaseType_t task_priority;                  /**< priority of the event loop task */
    uint32_t task_stack_size;                   /**< stack size of the event loop task */
    BaseType_t task_core_id;                    /**< core to which the event loop task is pinned */
} esp_event_loop_args_t;

/**
 * @brief Create a new event loop (sim: one backing thread per handle).
 */
esp_err_t esp_event_loop_create(const esp_event_loop_args_t *event_loop_args,
                                esp_event_loop_handle_t *event_loop);

/**
 * @brief Delete an existing event loop.
 */
esp_err_t esp_event_loop_delete(esp_event_loop_handle_t event_loop);

/**
 * @brief Create the default event loop. Second call → ESP_ERR_INVALID_STATE
 *        (matches IDF; wifi_mgr already tolerates it).
 */
esp_err_t esp_event_loop_create_default(void);

/**
 * @brief Delete the default event loop (stops the thread, frees handlers).
 */
esp_err_t esp_event_loop_delete_default(void);

/**
 * @brief Run the event loop (sim: drains pending events then returns).
 */
esp_err_t esp_event_loop_run(esp_event_loop_handle_t event_loop, TickType_t ticks_to_run);

/**
 * @brief Register a handler on a specific loop.
 */
esp_err_t esp_event_handler_register_with(esp_event_loop_handle_t event_loop,
                                          esp_event_base_t event_base,
                                          int32_t event_id,
                                          esp_event_handler_t event_handler,
                                          void *event_handler_arg);

/**
 * @brief Register a handler on the default loop.
 */
esp_err_t esp_event_handler_register(esp_event_base_t event_base,
                                     int32_t event_id,
                                     esp_event_handler_t event_handler,
                                     void *event_handler_arg);

/**
 * @brief Register a handler instance on a specific loop.
 */
esp_err_t esp_event_handler_instance_register_with(esp_event_loop_handle_t event_loop,
                                                   esp_event_base_t event_base,
                                                   int32_t event_id,
                                                   esp_event_handler_t event_handler,
                                                   void *event_handler_arg,
                                                   esp_event_handler_instance_t *instance);

/**
 * @brief Register a handler instance on the default loop.
 */
esp_err_t esp_event_handler_instance_register(esp_event_base_t event_base,
                                              int32_t event_id,
                                              esp_event_handler_t event_handler,
                                              void *event_handler_arg,
                                              esp_event_handler_instance_t *instance);

/**
 * @brief Unregister a handler on a specific loop.
 */
esp_err_t esp_event_handler_unregister_with(esp_event_loop_handle_t event_loop,
                                            esp_event_base_t event_base,
                                            int32_t event_id,
                                            esp_event_handler_t event_handler);

/**
 * @brief Unregister a handler on the default loop.
 */
esp_err_t esp_event_handler_unregister(esp_event_base_t event_base,
                                       int32_t event_id,
                                       esp_event_handler_t event_handler);

/**
 * @brief Unregister a handler instance on a specific loop.
 */
esp_err_t esp_event_handler_instance_unregister_with(esp_event_loop_handle_t event_loop,
                                                     esp_event_base_t event_base,
                                                     int32_t event_id,
                                                     esp_event_handler_instance_t instance);

/**
 * @brief Unregister a handler instance on the default loop.
 */
esp_err_t esp_event_handler_instance_unregister(esp_event_base_t event_base,
                                                int32_t event_id,
                                                esp_event_handler_instance_t instance);

/**
 * @brief Post an event to a specific loop. Payload is copied.
 */
esp_err_t esp_event_post_to(esp_event_loop_handle_t event_loop,
                            esp_event_base_t event_base,
                            int32_t event_id,
                            const void *event_data,
                            size_t event_data_size,
                            TickType_t ticks_to_wait);

/**
 * @brief Post an event to the default loop. Thread-safe; the wifi shim
 *        calls this from its connect worker while the app may post from
 *        any thread.
 */
esp_err_t esp_event_post(esp_event_base_t event_base,
                         int32_t event_id,
                         const void *event_data,
                         size_t event_data_size,
                         TickType_t ticks_to_wait);

/**
 * @brief Post an event from an ISR (sim: identical to esp_event_post).
 */
esp_err_t esp_event_isr_post_to(esp_event_loop_handle_t event_loop,
                                esp_event_base_t event_base,
                                int32_t event_id,
                                const void *event_data,
                                size_t event_data_size,
                                TickType_t ticks_to_wait);

esp_err_t esp_event_isr_post(esp_event_base_t event_base,
                             int32_t event_id,
                             const void *event_data,
                             size_t event_data_size,
                             TickType_t ticks_to_wait);

/**
 * @brief Dumps registered handlers (sim: prints to stdout).
 */
esp_err_t esp_event_dump_handler_instances(esp_event_loop_handle_t event_loop, void *file);

#ifdef __cplusplus
}
#endif

#endif /* ESP_EVENT_H_ */
