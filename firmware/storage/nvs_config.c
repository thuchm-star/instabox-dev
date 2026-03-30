#include <stdbool.h>
#include <stdint.h>

// Stub for reading/writing config from NVS.

bool nvs_config_load_uint32(const char *key, uint32_t *out_value) {
    if (!key || !out_value) {
        return false;
    }
    // TODO: read from NVS, return false if not found.
    return false;
}

