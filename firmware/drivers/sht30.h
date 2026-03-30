#pragma once

#include <stdbool.h>

void sht30_init(void);
bool sht30_read(float *temperature_c, float *humidity_percent);
