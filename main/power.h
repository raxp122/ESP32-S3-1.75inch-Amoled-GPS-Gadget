// power.h - battery life: CPU frequency scaling and automatic light sleep, Wi-Fi only when
// needed, battery warnings and shutdown, movement from the IMU, the night window.
#pragma once
#include <stdbool.h>
#include <stdint.h>

void     power_init(void);              // after settings_load(): PMU charger, power management
void     power_tick(void);              // every 200 ms from the UI (LVGL lock held)

// Full CPU speed for a while (rendering, JPEG decoding). Counted: every begin needs an end.
void     power_boost_begin(void);
void     power_boost_end(void);
// No light sleep while held (web server, OTA download, SD writes from other tasks). Counted.
void     power_hold(bool on);
void     power_apply(void);             // after changing g_set.power_save

uint32_t power_motion_idle_ms(void);    // since the last movement (0 if the IMU is missing)
void     power_motion_reset(void);
bool     power_night(void);             // inside the night window of the settings (time known)
bool     power_time_valid(void);
// battery state read every few seconds (the UI asks often, the PMU is on a shared I2C bus)
int      power_battery_percent(void);   // -1 = no battery
bool     power_charging(void);
