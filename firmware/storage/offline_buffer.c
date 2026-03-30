#include <stdbool.h>
#include <stdint.h>

// Stub for offline event buffer in flash.

typedef struct {
    // TODO: define event representation for storage.
    uint8_t dummy;
} offline_event_t;

void offline_buffer_init(void) {
    // TODO: initialize underlying storage (SPIFFS/NVS).
}

bool offline_buffer_push(const offline_event_t *evt) {
    (void)evt;
    // TODO: append event, evict oldest if full.
    return false;
}

bool offline_buffer_pop(offline_event_t *out_evt) {
    (void)out_evt;
    // TODO: get next event to upload.
    return false;
}

