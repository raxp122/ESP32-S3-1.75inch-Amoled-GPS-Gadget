// power.c - see power.h
#include "power.h"
#include "board.h"
#include "core.h"
#include "settings.h"
#include <math.h>
#include <time.h>
#include "esp_timer.h"

#define MOTION_G 0.07f   // change of acceleration (g) that counts as a movement

static int64_t last_motion_us;
static vec3_t prev;
static bool have_prev;

void power_motion_reset(void) { last_motion_us = esp_timer_get_time(); }

void power_poll(void)
{
    vec3_t a;
    if (!board_imu_ok() || !board_imu_accel(&a)) return;
    if (have_prev) {
        float d = fabsf(a.x - prev.x) + fabsf(a.y - prev.y) + fabsf(a.z - prev.z);
        if (d > MOTION_G) last_motion_us = esp_timer_get_time();
    }
    prev = a;
    have_prev = true;
}

uint32_t power_motion_idle_ms(void)
{
    if (!board_imu_ok()) return 0;
    return (uint32_t)((esp_timer_get_time() - last_motion_us) / 1000);
}

bool power_time_valid(void) { return time(NULL) > 1735689600;   /* after 2025-01-01 */ }

bool power_night(void)
{
    if (!g_set.night_on || !power_time_valid()) return false;
    time_t t = time(NULL);
    struct tm lt;
    localtime_r(&t, &lt);
    return night_active(lt.tm_hour, g_set.night_from_h, g_set.night_to_h);
}
