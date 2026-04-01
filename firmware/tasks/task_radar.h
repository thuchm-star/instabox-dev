#pragma once

#include <stdbool.h>
#include <stdint.h>

/* ── Radar presence events ─────────────────────────── */

typedef enum {
    RADAR_EVT_PERSON_ENTER,     /* debounced: nobody → person   */
    RADAR_EVT_PERSON_LEAVE,     /* debounced: person → nobody   */
    RADAR_EVT_RANGE_UPDATE,     /* range changed while present  */
    RADAR_EVT_HEARTBEAT,        /* periodic status (present or not) */
} radar_event_type_t;

typedef struct {
    radar_event_type_t type;
    bool     present;           /* current debounced presence   */
    int      range_cm;          /* -1 = no data, 0 = clear, >0 */
    uint32_t hold_ms;           /* how long in current state    */
} radar_event_t;

/*
 * Register a callback to receive radar events.
 * Called from the radar task context — keep it fast / non-blocking.
 * Set to NULL to unregister.
 */
typedef void (*radar_event_cb_t)(const radar_event_t *evt);
void task_radar_set_callback(radar_event_cb_t cb);

/* Start the radar FreeRTOS task (core 1, prio 5, stack 8192). */
void task_radar_start(void);
