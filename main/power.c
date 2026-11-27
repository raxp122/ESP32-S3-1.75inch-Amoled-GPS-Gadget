// power.c - see power.h
//
// Power management (ESP-IDF esp_pm): the CPU runs at 80 MHz and drops into automatic light
// sleep whenever every task is waiting, and goes to 240 MHz only while someone holds a
// "boost" (the LVGL task while it draws, the player while it decodes). The AMOLED keeps its
// picture on its own, so a still image in pendant mode costs almost nothing between frames.
// Light sleep is off on USB power (the USB serial link would drop) and when the setting is off.
#include "power.h"
#include "board.h"
#include "core.h"
#include "i18n.h"
#include "settings.h"
#include "ui.h"
#include <math.h>
#include <stdio.h>
#include <time.h>
#include "esp_log.h"
#include "esp_pm.h"
#include "sdkconfig.h"
#include "esp_timer.h"

static const char *TAG = "power";

#define MOTION_G 0.07f   // change of acceleration (g) that counts as a movement

static esp_pm_lock_handle_t lock_boost, lock_hold, lock_policy;
static bool pm_ok, policy_held, usb;
static int64_t last_motion_us;
static vec3_t prev;
static bool have_prev;
static int warned;   // lowest battery warning shown: 0, 15, 7
static int bat_pct = -1;
static bool bat_chg;

int power_battery_percent(void) { return bat_pct; }
bool power_charging(void) { return bat_chg; }

void power_init(void)
{
    board_battery_setup(g_set.battery_mah, g_set.battery_care);
#if CONFIG_PM_ENABLE
    esp_pm_config_t cfg = {.max_freq_mhz = 240, .min_freq_mhz = 80, .light_sleep_enable = true};
    if (esp_pm_configure(&cfg) == ESP_OK &&
        esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "boost", &lock_boost) == ESP_OK &&
        esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "hold", &lock_hold) == ESP_OK &&
        esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "policy", &lock_policy) == ESP_OK) {
        pm_ok = true;
    } else {
        ESP_LOGW(TAG, "power management not available");
    }
#endif
    usb = board_usb_power();
    bat_chg = board_charging();
    bat_pct = battery_percent_pick(board_battery_percent(), board_battery_volts(), bat_chg);
    power_apply();
}

// light sleep and the slow clock only on battery and with the setting on; otherwise the
// "policy" lock keeps the CPU at full speed (and so awake) all the time, as before 0.1.7
void power_apply(void)
{
    if (!pm_ok) return;
    bool want = !g_set.power_save || usb;
    if (want == policy_held) return;
    policy_held = want;
    if (want) esp_pm_lock_acquire(lock_policy);
    else esp_pm_lock_release(lock_policy);
    ESP_LOGI(TAG, "power saving %s", want ? "off" : "on");
}

void power_boost_begin(void) { if (pm_ok) esp_pm_lock_acquire(lock_boost); }
void power_boost_end(void) { if (pm_ok) esp_pm_lock_release(lock_boost); }
void power_hold(bool on)
{
    if (!pm_ok) return;
    if (on) esp_pm_lock_acquire(lock_hold);
    else esp_pm_lock_release(lock_hold);
}

/* ---------------- battery ---------------- */

static void battery_check(void)
{
    if (board_charging() || usb) { warned = 0; return; }
    float v = board_battery_volts();
    if (v <= 0) return;   // no battery (USB only)
    int p = battery_percent_pick(board_battery_percent(), v, false);
    // almost empty: switch off while the cell still has its reserve (a LiPo should not be
    // drained below ~3.4-3.5 V at rest); the voltage check filters out a confused gauge
    if (p <= 3 && v > 0 && v < 3.55f) {
        ESP_LOGW(TAG, "battery empty (%d%%, %.2f V): power off", p, v);
        ui_toast(T(S_BATTERY_EMPTY));
        ui_power_off();
        return;
    }
    int level = p <= 7 ? 7 : p <= 15 ? 15 : 0;
    if (level && (!warned || level < warned)) {
        warned = level;
        char t[80];
        snprintf(t, sizeof(t), T(S_BATTERY_LOW), p);
        ui_wake();
        ui_toast(t);
    }
}

void power_tick(void)
{
    static int n;
    n++;
    // movement
    vec3_t a;
    if (board_imu_ok() && board_imu_accel(&a)) {
        if (have_prev) {
            float d = fabsf(a.x - prev.x) + fabsf(a.y - prev.y) + fabsf(a.z - prev.z);
            if (d > MOTION_G) last_motion_us = esp_timer_get_time();
        }
        prev = a;
        have_prev = true;
    }
    if (n % 5 == 0) {   // every second: USB plugged in or out
        bool u = board_usb_power();
        if (u != usb) { usb = u; power_apply(); }
    }
    if (n % 25 == 0) {   // every 5 s; a gauge that has not learned the battery yet is corrected
        bat_chg = board_charging();
        bat_pct = battery_percent_pick(board_battery_percent(), board_battery_volts(), bat_chg);
    }
    if (n % 50 == 1) battery_check();   // every 10 s
}

/* ---------------- movement / night ---------------- */

void power_motion_reset(void) { last_motion_us = esp_timer_get_time(); }

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
