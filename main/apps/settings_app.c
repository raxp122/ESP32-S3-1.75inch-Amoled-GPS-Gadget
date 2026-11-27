// settings_app.c - Settings and their sub-menus
#include "ui.h"
#include "board.h"
#include "ota.h"
#include "power.h"
#include "settings.h"
#include "wifi_mgr.h"
#include <stdio.h>
#include "esp_system.h"

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static const char *on_off(bool on) { return T(on ? S_ON : S_OFF); }

// picks the next value in a list (wrapping), starting from the closest one
static int step_list(const int *vals, int n, int cur, int dir)
{
    int k = 0;
    for (int i = 0; i < n; i++) if (vals[i] <= cur) k = i;
    return vals[((k + dir) % n + n) % n];
}

static void fmt_minutes(char *b, int n, int min)
{
    if (!min) snprintf(b, n, "%s", T(S_NEVER));
    else if (min < 60) snprintf(b, n, "%d min", min);
    else snprintf(b, n, "%d h", min / 60);
}

/* ---------------- Screen ---------------- */

static void v_bright(char *b, int n) { snprintf(b, n, "%d%%", g_set.brightness); }
static void a_bright(int d) { g_set.brightness = clampi(g_set.brightness + d * 5, 5, 100); ui_apply_brightness(); settings_mark(); }
static void v_rot(char *b, int n) { snprintf(b, n, "%d\xC2\xB0", g_set.rotation * 90); }
static void a_rot(int d)
{
    g_set.rotation = (g_set.rotation + d + 4) & 3;
    display_set_rotation(g_set.rotation);
    settings_mark();
}
static void v_accent(char *b, int n) { snprintf(b, n, "%s", ui_accent_name(g_set.accent)); }
static void a_accent(int d)
{
    g_set.accent = (g_set.accent + d + ui_accent_count()) % ui_accent_count();
    settings_mark();
    ui_rebuild();   // the whole screen takes the new colour
}
static const int sleep_vals[] = {0, 15, 30, 60, 120, 300, 600};
static void v_sleep(char *b, int n)
{
    if (!g_set.sleep_s) snprintf(b, n, "%s", T(S_NEVER));
    else if (g_set.sleep_s < 60) snprintf(b, n, "%d s", g_set.sleep_s);
    else snprintf(b, n, "%d min", g_set.sleep_s / 60);
}
static void a_sleep(int d) { g_set.sleep_s = step_list(sleep_vals, 7, g_set.sleep_s, d); settings_mark(); }

static void v_homeloop(char *b, int n) { snprintf(b, n, "%s", T(g_set.home_loop ? S_HOME_LOOP : S_HOME_STOP)); }
static void a_homeloop(int d) { g_set.home_loop = !g_set.home_loop; settings_mark(); }

static void v_shortcut(char *b, int n) { snprintf(b, n, "%s", home_app_name(g_set.home_shortcut)); }
static void a_shortcut(int d)
{
    // through the apps in the order of the ring (alphabetical)
    g_set.home_shortcut = (uint8_t)home_app_at(home_app_pos(g_set.home_shortcut) + d);
    settings_mark();
}

static void v_clockon(char *b, int n) { snprintf(b, n, "%s", on_off(g_set.clock_on)); }
static void s_clockon(void) { g_set.clock_on = !g_set.clock_on; settings_mark(); }

static const menu_item_t screen_items[] = {
    {IC_SUN, S_BRIGHTNESS, NULL, v_bright, NULL, a_bright},
    {IC_ROTATE, S_ROTATION, NULL, v_rot, NULL, a_rot},
    {IC_PALETTE, S_ACCENT, NULL, v_accent, NULL, a_accent},
    {IC_HOURGLASS, S_AUTO_OFF, NULL, v_sleep, NULL, a_sleep},
    {IC_HOME, S_HOME_LIST, NULL, v_homeloop, NULL, a_homeloop},
    {IC_STAR, S_HOME_SHORTCUT, NULL, v_shortcut, NULL, a_shortcut},
    {IC_CLOCK, S_CLOCK_ON, NULL, v_clockon, s_clockon},
};
static menu_t screen_menu = {S_SET_SCREEN, screen_items, 7, 0};

/* ---------------- Battery ---------------- */

static const int mah_vals[] = {150, 200, 250, 300, 400, 500, 600, 800, 1000, 1200, 1500, 2000};
static void v_mah(char *b, int n) { snprintf(b, n, "%d mAh", g_set.battery_mah); }
static void a_mah(int d)
{
    g_set.battery_mah = step_list(mah_vals, 12, g_set.battery_mah, d);
    board_battery_setup(g_set.battery_mah, g_set.battery_care);
    power_new_battery();
    settings_mark();
}
static void v_care(char *b, int n) { snprintf(b, n, "%s", on_off(g_set.battery_care)); }
static void s_care(void)
{
    g_set.battery_care = !g_set.battery_care;
    board_battery_setup(g_set.battery_mah, g_set.battery_care);
    settings_mark();
}
static void v_psave(char *b, int n) { snprintf(b, n, "%s", on_off(g_set.power_save)); }
static void s_psave(void) { g_set.power_save = !g_set.power_save; power_apply(); settings_mark(); }
static void v_wsave(char *b, int n) { snprintf(b, n, "%s", on_off(g_set.wifi_save)); }
static void s_wsave(void) { g_set.wifi_save = !g_set.wifi_save; settings_mark(); }
static void v_batnow(char *b, int n) { ui_battery_icon(b, n); }
static const int offpct_vals[] = {0, 3, 5, 10, 15, 20, 25, 30, 35, 40, 50};
static void v_offpct(char *b, int n)
{
    if (!g_set.battery_off_pct) snprintf(b, n, "%s", T(S_NEVER));
    else snprintf(b, n, "%d%%", g_set.battery_off_pct);
}
static void a_offpct(int d) { g_set.battery_off_pct = (uint8_t)step_list(offpct_vals, 11, g_set.battery_off_pct, d); settings_mark(); }

static const menu_item_t battery_items[] = {
    {IC_BAT_FULL, S_INFO_BATTERY, NULL, v_batnow},
    {IC_BOLT, S_POWER_SAVE, NULL, v_psave, s_psave},
    {IC_WIFI, S_WIFI_SAVE, NULL, v_wsave, s_wsave},
    {IC_BAT_3, S_BATT_CAPACITY, NULL, v_mah, NULL, a_mah},
    {IC_HEART, S_BATT_CARE, NULL, v_care, s_care},
    {IC_POWER, S_BATT_OFF_AT, NULL, v_offpct, NULL, a_offpct},
};
static menu_t battery_menu = {S_SET_BATTERY, battery_items, 6, 0};

// from the battery warning: straight to the threshold
void settings_open_battery(void)
{
    battery_menu.sel = 5;
    ui_push(&scr_menu, &battery_menu);
}

/* ---------------- Pendant ---------------- */

static void v_dbright(char *b, int n) { snprintf(b, n, "%d%%", g_set.disp_brightness); }
static void a_dbright(int d) { g_set.disp_brightness = clampi(g_set.disp_brightness + d * 5, 5, 100); settings_mark(); }
static const int interval_vals[] = {5, 10, 20, 30, 60, 120, 300, 600, 1800};
static void fmt_secs(char *b, int n, int s)
{
    if (s < 60) snprintf(b, n, "%d s", s);
    else snprintf(b, n, "%d min", s / 60);
}
// one interval per content choice
static void v_int_all(char *b, int n) { fmt_secs(b, n, g_set.disp_int_all_s); }
static void v_int_img(char *b, int n) { fmt_secs(b, n, g_set.disp_int_img_s); }
static void v_int_anim(char *b, int n) { fmt_secs(b, n, g_set.disp_int_anim_s); }
static void step_int(uint16_t *v, int d) { *v = (uint16_t)step_list(interval_vals, 9, *v, d); settings_mark(); }
static void a_int_all(int d) { step_int(&g_set.disp_int_all_s, d); }
static void a_int_img(int d) { step_int(&g_set.disp_int_img_s, d); }
static void a_int_anim(int d) { step_int(&g_set.disp_int_anim_s, d); }
static const int filter_names[MEDIA_FILTERS] = {S_FILTER_ALL, S_FILTER_IMAGES, S_FILTER_ANIM};
static void v_filter(char *b, int n) { snprintf(b, n, "%s", T(filter_names[g_set.disp_filter % MEDIA_FILTERS])); }
static void s_filter(void) { g_set.disp_filter = (uint8_t)((g_set.disp_filter + 1) % MEDIA_FILTERS); settings_mark(); }
static void v_shuffle(char *b, int n) { snprintf(b, n, "%s", on_off(g_set.disp_shuffle)); }
static void s_shuffle(void) { g_set.disp_shuffle = !g_set.disp_shuffle; settings_mark(); }
static const int off_vals[] = {0, 5, 10, 15, 30, 60, 120, 240};
static void v_doff(char *b, int n) { fmt_minutes(b, n, g_set.disp_off_min); }
static void a_doff(int d) { g_set.disp_off_min = step_list(off_vals, 8, g_set.disp_off_min, d); settings_mark(); }
static void v_motion(char *b, int n) { snprintf(b, n, "%s", on_off(g_set.motion_wake)); }
static void s_motion(void) { g_set.motion_wake = !g_set.motion_wake; settings_mark(); }

// what the Pendant app plays: one media (BOOT for the next) or all of them in turn. The gallery
// sets it too (tap: one media, double tap: slideshow).
static void v_pmode(char *b, int n) { snprintf(b, n, "%s", T(g_set.disp_slideshow ? S_SLIDESHOW : S_SINGLE_MEDIA)); }
static void s_pmode(void) { g_set.disp_slideshow = !g_set.disp_slideshow; settings_mark(); }

static const menu_item_t pendant_items[] = {
    {IC_PLAY, S_PENDANT_MODE, NULL, v_pmode, s_pmode},
    {IC_FILM, S_PENDANT_FILTER, NULL, v_filter, s_filter},
    {IC_SUN, S_BRIGHTNESS, NULL, v_dbright, NULL, a_dbright},
    {IC_SYNC, S_INTERVAL_ALL, NULL, v_int_all, NULL, a_int_all},
    {IC_IMAGE, S_INTERVAL_IMG, NULL, v_int_img, NULL, a_int_img},
    {IC_GIF, S_INTERVAL_ANIM, NULL, v_int_anim, NULL, a_int_anim},
    {IC_SHUFFLE, S_SHUFFLE, NULL, v_shuffle, s_shuffle},
    {IC_HOURGLASS, S_PENDANT_OFF, NULL, v_doff, NULL, a_doff},
    {IC_WALK, S_MOTION_WAKE, NULL, v_motion, s_motion},
};
static menu_t pendant_menu = {S_APP_PENDANT, pendant_items, 9, 0};

/* ---------------- Night ---------------- */

static void v_night(char *b, int n) { snprintf(b, n, "%s", on_off(g_set.night_on)); }
static void s_night(void) { g_set.night_on = !g_set.night_on; settings_mark(); }
static void v_from(char *b, int n) { snprintf(b, n, "%02d:00", g_set.night_from_h); }
static void a_from(int d) { g_set.night_from_h = (g_set.night_from_h + d + 24) % 24; settings_mark(); }
static void v_to(char *b, int n) { snprintf(b, n, "%02d:00", g_set.night_to_h); }
static void a_to(int d) { g_set.night_to_h = (g_set.night_to_h + d + 24) % 24; settings_mark(); }
static void v_nmode(char *b, int n) { snprintf(b, n, "%s", T(g_set.night_mode ? S_NIGHT_OFF : S_NIGHT_DIM)); }
static void a_nmode(int d) { g_set.night_mode = !g_set.night_mode; settings_mark(); }

static const menu_item_t night_items[] = {
    {IC_MOON, S_NIGHT_MODE, NULL, v_night, s_night},
    {IC_MOON, S_NIGHT_FROM, NULL, v_from, NULL, a_from},
    {IC_SUN, S_NIGHT_TO, NULL, v_to, NULL, a_to},
    {IC_EYE, S_NIGHT_EFFECT, NULL, v_nmode, NULL, a_nmode},
};
static menu_t night_menu = {S_SET_NIGHT, night_items, 4, 0};

/* ---------------- Updates ---------------- */

static void v_ver(char *b, int n) { snprintf(b, n, "%s", ota_current()); }
static void v_auto(char *b, int n) { snprintf(b, n, "%s", on_off(g_set.ota_auto)); }
static void s_auto(void) { g_set.ota_auto = !g_set.ota_auto; settings_mark(); if (g_set.ota_auto) ota_auto_start(); }

static const menu_item_t ota_items[] = {
    {IC_DOWNLOAD, S_OTA_CHECK, NULL, v_ver, NULL, NULL, &scr_ota},
    {IC_SYNC, S_OTA_AUTO, NULL, v_auto, s_auto},
};
static menu_t ota_menu = {S_SET_UPDATES, ota_items, 2, 0};

/* ---------------- System ---------------- */

static void s_restart(void) { settings_save(); esp_restart(); }
static void s_off(void) { ui_power_off(); }
static void s_reset(void)
{
    settings_reset();
    display_set_rotation(g_set.rotation);
    ui_apply_tz();
    ui_apply_brightness();
    ui_toast(T(S_RESET_DONE));
    ui_rebuild();
}
static void v_gps(char *b, int n) { snprintf(b, n, "%s", on_off(g_set.gps_on)); }
static void s_gps(void) { g_set.gps_on = !g_set.gps_on; board_gps_enable(g_set.gps_on); settings_mark(); }

static const menu_item_t sys_items[] = {
    {IC_INFO, S_APP_INFO, NULL, NULL, NULL, NULL, &scr_info},
    {IC_SATELLITE, S_GPS, NULL, v_gps, s_gps},
    {IC_REFRESH, S_RESTART, NULL, NULL, s_restart, .confirm = true},
    {IC_POWER, S_POWER_OFF, NULL, NULL, s_off, .confirm = true},
    {IC_TRASH, S_RESET, NULL, NULL, s_reset, .confirm = true},
};
static menu_t sys_menu = {S_SET_SYSTEM, sys_items, 5, 0};

/* ---------------- top level ---------------- */

static void v_wifi(char *b, int n)
{
    switch (wifi_mgr_state()) {
    case WIFI_CONNECTED: snprintf(b, n, "%s", g_set.wifi_ssid); break;
    case WIFI_CONNECTING: snprintf(b, n, "%s", T(S_CONNECTING)); break;
    case WIFI_OFF: snprintf(b, n, "%s", T(S_OFF)); break;
    default: snprintf(b, n, "%s", T(g_set.wifi_ssid[0] ? S_NOT_CONNECTED : S_NO_NETWORK_SET)); break;
    }
}
static void v_lang(char *b, int n) { snprintf(b, n, "%s", i18n_lang_name(g_set.lang)); }
static void a_lang(int d)
{
    g_set.lang = (g_set.lang + d + LANG_COUNT) % LANG_COUNT;
    settings_mark();
    ui_rebuild();
}
static void v_tz(char *b, int n) { snprintf(b, n, "%s", ui_tz_name(g_set.tz)); }
static void a_tz(int d) { g_set.tz = (g_set.tz + d + ui_tz_count()) % ui_tz_count(); ui_apply_tz(); settings_mark(); }
static void v_screen(char *b, int n) { snprintf(b, n, "%d%%  ·  %d\xC2\xB0", g_set.brightness, g_set.rotation * 90); }
static void v_pendant(char *b, int n)
{
    if (g_set.disp_slideshow) snprintf(b, n, "%s", T(filter_names[g_set.disp_filter % MEDIA_FILTERS]));
    else snprintf(b, n, "%s", T(S_SINGLE_MEDIA));
}
static void v_night_sum(char *b, int n)
{
    if (!g_set.night_on) snprintf(b, n, "%s", T(S_OFF));
    else snprintf(b, n, "%02d:00 - %02d:00", g_set.night_from_h, g_set.night_to_h);
}

static const menu_item_t top_items[] = {
    {IC_WIFI, S_WIFI, NULL, v_wifi, NULL, NULL, &scr_wifi},
    {IC_SUN, S_SET_SCREEN, NULL, v_screen, NULL, NULL, &scr_menu, &screen_menu},
    {IC_GEM, S_APP_PENDANT, NULL, v_pendant, NULL, NULL, &scr_menu, &pendant_menu},
    {IC_MOON, S_SET_NIGHT, NULL, v_night_sum, NULL, NULL, &scr_menu, &night_menu},
    {IC_BAT_FULL, S_SET_BATTERY, NULL, v_batnow, NULL, NULL, &scr_menu, &battery_menu},
    {IC_DOWNLOAD, S_SET_UPDATES, NULL, v_ver, NULL, NULL, &scr_menu, &ota_menu},
    {IC_LANG, S_LANGUAGE, NULL, v_lang, NULL, a_lang},
    {IC_GLOBE, S_TIMEZONE, NULL, v_tz, NULL, a_tz},
    {IC_CHIP, S_SET_SYSTEM, NULL, NULL, NULL, NULL, &scr_menu, &sys_menu},
};
static menu_t top_menu = {S_APP_SETTINGS, top_items, sizeof(top_items) / sizeof(top_items[0]), 0};

MENU_SCREEN(scr_settings, "settings", &top_menu);
