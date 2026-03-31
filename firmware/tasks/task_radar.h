#pragma once

/* FreeRTOS task: đọc radar LD2420, điều khiển LED, log trạng thái.
 * Pin to Core 1, stack 8192. */
void task_radar_start(void);
