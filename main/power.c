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
static int low_count;   // battery checks in a row below the power-off threshold
static int bat_pct = -1;
static bool bat_chg, bat_est, bat_present;
static int new_battery_in;   // ticks until the fuel gauge reset (0 = none pending)

static int64_t off_at_us;   // battery power off due at this time (0 = none)

#define OFF_WARN_S 30

int power_shutdown_in_s(void)
{
    if (!off_at_us) return -1;
    int64_t d = off_at_us - esp_timer_get_time();
    return d > 0 ? (int)((d + 999999) / 1000000) : 0;
}

void power_shutdown_extend(int s)
{
    int64_t t = esp_timer_get_time() + (int64_t)s * 1000000;
    if (off_at_us && off_at_us < t) off_at_us = t;
}

// below the threshold of the settings and not charging?
static bool below_threshold(int p)
{
    int lim = g_set.battery_off_pct;
    return lim && p >= 0 && p <= lim && !usb && !bat_chg;
}

int power_battery_percent(void) { return bat_pct; }
bool power_charging(void) { return bat_chg; }
bool power_battery_estimated(void) { return bat_pct >= 0 && bat_est; }
void power_new_battery(void) { new_battery_in = 15; }   // 3 s

static void battery_read(void)
{
    bat_chg = board_charging();
    bat_pct = battery_percent_pick(board_battery_percent(), board_battery_volts(), bat_chg, &bat_est);
}

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
    bat_present = board_battery_present();
    battery_read();
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
    if (board_charging() || usb) { warned = 0; low_count = 0; return; }
    float v = board_battery_volts();
    if (v <= 0) return;   // no battery (USB only)
    bool e = bat_est;
    int p = battery_percent_pick(board_battery_percent(), v, false, &e);
    // below the threshold of the settings, two checks in a row (a dip under load is not enough):
    // a screen says the pendant switches off in 30 s (plug in the charger or change the
    // threshold to stay on)
    low_count = below_threshold(p) ? low_count + 1 : 0;
    if (low_count >= 2 && !off_at_us) {
        ESP_LOGW(TAG, "battery %d%% (%.2f V), under the threshold: power off in %d s", p, v, OFF_WARN_S);
        off_at_us = esp_timer_get_time() + OFF_WARN_S * 1000000LL;
        ui_wake();
        ui_push(&scr_battery_off, NULL);
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
    // movement (not while locked with PWR: nothing may wake the screen then, the IMU rests)
    vec3_t a;
    if (ui_is_off() && ui_is_locked()) have_prev = false;
    else if (board_imu_ok() && board_imu_accel(&a)) {
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
        // a battery plugged in while on USB: a different one, maybe
        bool b = board_battery_present();
        if (b && !bat_present) power_new_battery();
        bat_present = b;
    }
    if (new_battery_in && --new_battery_in == 0) {
        board_gauge_reset();
        bat_est = true;   // the voltage until the gauge agrees with it again
        warned = 0;
    }
    // every 5 s; a gauge that has not learned the battery yet is corrected
    if (n % 25 == 0) battery_read();
    // the battery power off: called off by the charger or a lower threshold, else done
    if (off_at_us) {
        if (!below_threshold(bat_pct)) {
            off_at_us = 0;
            low_count = 0;
        } else if (esp_timer_get_time() >= off_at_us) {
            off_at_us = 0;
            ESP_LOGW(TAG, "battery under the threshold: power off");
            ui_power_off();
            return;
        }
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
