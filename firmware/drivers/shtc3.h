#pragma once

#include <stdbool.h>

void shtc3_init(void);
bool shtc3_read(float *temperature_c, float *humidity_percent);

