// power.h - movement from the IMU and the night window
#pragma once
#include <stdbool.h>
#include <stdint.h>

void     power_poll(void);              // every 200 ms (UI tick)
uint32_t power_motion_idle_ms(void);    // since the last movement (0 if the IMU is missing)
void     power_motion_reset(void);      // counts as a movement now
bool     power_night(void);             // inside the night window of the settings (and the time is known)
bool     power_time_valid(void);
