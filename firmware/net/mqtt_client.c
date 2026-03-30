#include <stdint.h>
#include <stdbool.h>

// Stub for MQTT client according to mqtt_schema.md.

void mqtt_client_init(void) {
    // TODO: configure MQTT client, TLS, and callbacks.
}

void mqtt_client_loop(void) {
    // TODO: maintain connection, handle incoming/outgoing messages.
}

bool mqtt_publish_status(void) {
    // TODO: publish status payload.
    return false;
}

bool mqtt_publish_event_person_detected(void) {
    // TODO: publish person_detected event.
    return false;
}

