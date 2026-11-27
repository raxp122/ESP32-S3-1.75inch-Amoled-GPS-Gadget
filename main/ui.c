// ui.c - screen stack, global gestures and buttons, overlays (clock at the top, toasts, the
// progress ring of a held button, quick panel), screen power and the look.
#include "ui.h"
#include "board.h"
#include "power.h"
#include "settings.h"
#include "wifi_mgr.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ui";

#define STACK_MAX 8
static struct { const screen_t *s; void *arg; } stack[STACK_MAX];
static int depth;
static bool closing, off, locked, swallow;
static int bright_override = -1;

static lv_obj_t *status_lbl, *toast_box, *toast_lbl, *hold_box, *hold_arc, *hold_icon, *hold_lbl;
static uint32_t toast_until;
static char toast_pending[128];
static volatile bool toast_flag;

/* ---------------- look ---------------- */

static const struct { const char *it, *en; uint32_t c; } accents[] = {
    {"Oro", "Gold", 0xE9C46A},
    {"Rosa", "Rose", 0xFF8FB1},
    {"Lavanda", "Lavender", 0xB9A2FF},
    {"Acqua", "Aqua", 0x5FE3D4},
    {"Corallo", "Coral", 0xFF8A65},
    {"Smeraldo", "Emerald", 0x5BE38C},
    {"Zaffiro", "Sapphire", 0x6FA8FF},
    {"Perla", "Pearl", 0xECE6DA},
};
#define N_ACCENTS (int)(sizeof(accents) / sizeof(accents[0]))

int ui_accent_count(void) { return N_ACCENTS; }
const char *ui_accent_name(int i) { return g_set.lang == LANG_IT ? accents[i % N_ACCENTS].it : accents[i % N_ACCENTS].en; }
lv_color_t ui_accent(void) { return lv_color_hex(accents[g_set.accent % N_ACCENTS].c); }
lv_color_t ui_accent_dark(void) { return lv_color_mix(ui_accent(), lv_color_black(), 64); }

static const struct { const char *name, *posix; } tzs[] = {
    {"Europe/Rome", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/London", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"UTC", "UTC0"},
    {"Europe/Athens", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Moscow", "MSK-3"},
    {"Asia/Dubai", "<+04>-4"},
    {"Asia/Kolkata", "IST-5:30"},
    {"Asia/Tokyo", "JST-9"},
    {"Australia/Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"America/Sao_Paulo", "<-03>3"},
    {"America/New_York", "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Chicago", "CST6CDT,M3.2.0,M11.1.0"},
    {"America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0"},
};
int ui_tz_count(void) { return (int)(sizeof(tzs) / sizeof(tzs[0])); }
const char *ui_tz_name(int i) { return tzs[i % ui_tz_count()].name; }
void ui_apply_tz(void)
{
    setenv("TZ", tzs[g_set.tz % ui_tz_count()].posix, 1);
    tzset();
}

lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *f, lv_color_t c, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(l, text ? text : "");
    return l;
}

lv_obj_t *ui_circle(lv_obj_t *parent, int d, lv_color_t bg)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, d, d);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, bg, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

void ui_one_line(lv_obj_t *l, int w)
{
    lv_obj_set_width(l, w);
    lv_obj_set_height(l, lv_font_get_line_height(lv_obj_get_style_text_font(l, 0)));
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
}

bool ui_set_text(lv_obj_t *l, const char *t)
{
    if (!l) return false;
    const char *cur = lv_label_get_text(l);
    if (cur && !strcmp(cur, t)) return false;
    lv_label_set_text(l, t);
    return true;
}

void ui_time_str(char *b, int n)
{
    if (!power_time_valid()) { snprintf(b, n, "--:--"); return; }
    time_t t = time(NULL);
    struct tm lt;
    localtime_r(&t, &lt);
    snprintf(b, n, "%02d:%02d", lt.tm_hour, lt.tm_min);
}

void ui_battery_icon(char *b, int n)
{
    int p = power_battery_percent();
    if (p < 0) { snprintf(b, n, IC_BOLT); return; }
    const char *ic = p > 85 ? IC_BAT_FULL : p > 60 ? IC_BAT_3 : p > 35 ? IC_BAT_2 : p > 12 ? IC_BAT_1 : IC_BAT_0;
    // "~": estimated from the voltage, the fuel gauge has not learned the battery yet
    snprintf(b, n, "%s%s %s%d%%", power_charging() ? IC_BOLT " " : "", ic, power_battery_estimated() ? "~" : "", p);
}

/* ---------------- overlays ---------------- */

static void overlays_create(void)
{
    lv_obj_t *top = lv_layer_top();

    status_lbl = ui_label(top, &font_s, C_DIM, "");
    lv_obj_align(status_lbl, LV_ALIGN_TOP_MID, 0, 14);

    toast_box = lv_obj_create(top);
    lv_obj_remove_style_all(toast_box);
    lv_obj_set_style_bg_color(toast_box, lv_color_hex(0x1E1C19), 0);
    lv_obj_set_style_bg_opa(toast_box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(toast_box, 28, 0);
    lv_obj_set_style_border_color(toast_box, C_FAINT, 0);
    lv_obj_set_style_border_width(toast_box, 1, 0);
    lv_obj_set_style_pad_hor(toast_box, 22, 0);
    lv_obj_set_style_pad_ver(toast_box, 12, 0);
    lv_obj_set_size(toast_box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(toast_box, LV_ALIGN_BOTTOM_MID, 0, -58);
    toast_lbl = ui_label(toast_box, &font_s, C_TEXT, "");
    lv_obj_set_style_max_width(toast_lbl, 300, 0);
    lv_label_set_long_mode(toast_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_add_flag(toast_box, LV_OBJ_FLAG_HIDDEN);

    // progress ring of a held button (PWR: power off, BOOT: exit / home)
    hold_box = lv_obj_create(top);
    lv_obj_remove_style_all(hold_box);
    lv_obj_set_size(hold_box, SCR_W, SCR_H);
    lv_obj_set_style_bg_color(hold_box, C_BG, 0);
    lv_obj_set_style_bg_opa(hold_box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(hold_box, LV_RADIUS_CIRCLE, 0);
    hold_arc = lv_arc_create(hold_box);
    lv_obj_set_size(hold_arc, SCR_W - 8, SCR_H - 8);
    lv_obj_center(hold_arc);
    lv_arc_set_bg_angles(hold_arc, 0, 360);
    lv_arc_set_rotation(hold_arc, 270);
    lv_arc_set_range(hold_arc, 0, 100);
    lv_obj_remove_style(hold_arc, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_width(hold_arc, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_width(hold_arc, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(hold_arc, C_FAINT, LV_PART_MAIN);
    hold_icon = ui_label(hold_box, &font_icon, C_TEXT, IC_POWER);
    lv_obj_align(hold_icon, LV_ALIGN_CENTER, 0, -30);
    hold_lbl = ui_label(hold_box, &font_m, C_TEXT, "");
    lv_obj_align(hold_lbl, LV_ALIGN_CENTER, 0, 48);
    lv_obj_add_flag(hold_box, LV_OBJ_FLAG_HIDDEN);
}

void ui_toast(const char *msg)
{
    lv_label_set_text(toast_lbl, msg);
    lv_obj_remove_flag(toast_box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(toast_box);
    toast_until = lv_tick_get() + 2600 + strlen(msg) * 30;
    ESP_LOGI(TAG, "toast: %s", msg);
}

void ui_toast_async(const char *msg)
{
    strlcpy(toast_pending, msg, sizeof(toast_pending));
    toast_flag = true;
}

// the held button ring: PWR wins over BOOT
static bool hold_shown;
static void hold_update(void)
{
    int pwr = input_pwr_held_ms(), boot = input_boot_held_ms();
    if (pwr > HOLD_SHOW_MS && off) ui_wake();   // the power-off countdown shows even from a dark screen
    bool show = pwr > HOLD_SHOW_MS || (boot > HOLD_SHOW_MS && !off && !locked);
    if (!show) {
        if (hold_shown) { lv_obj_add_flag(hold_box, LV_OBJ_FLAG_HIDDEN); hold_shown = false; }
        return;
    }
    // in pendant mode LVGL is paused: give the screen back to it while the ring is shown
    if (display_direct_on()) display_direct_end();
    bool p = pwr > HOLD_SHOW_MS;
    int held = p ? pwr : boot, total = p ? PWR_LONG_MS : BOOT_LONG_MS;
    int v = (held - HOLD_SHOW_MS) * 100 / (total - HOLD_SHOW_MS);
    if (!hold_shown) {
        lv_obj_set_style_arc_color(hold_arc, p ? C_WARN : ui_accent(), LV_PART_INDICATOR);
        lv_label_set_text(hold_icon, p ? IC_POWER : (ui_current() == &scr_pendant ? IC_EYE : IC_HOME));
        lv_obj_set_style_text_color(hold_icon, p ? C_WARN : ui_accent(), 0);
        lv_label_set_text(hold_lbl, T(p ? S_HOLD_POWER : (ui_current() == &scr_pendant ? S_HOLD_EXIT : S_HOLD_HOME)));
        lv_obj_remove_flag(hold_box, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(hold_box);
        hold_shown = true;
    }
    lv_arc_set_value(hold_arc, v > 100 ? 100 : v);
}

static void status_update(void)
{
    const screen_t *s = ui_current();
    bool show = s && !(s->flags & SCR_NO_STATUS);
    if (!show) { lv_obj_add_flag(status_lbl, LV_OBJ_FLAG_HIDDEN); return; }
    lv_obj_remove_flag(status_lbl, LV_OBJ_FLAG_HIDDEN);
    char t[16], b[48];
    ui_time_str(t, sizeof(t));
    int p = power_battery_percent();
    if (power_charging()) snprintf(b, sizeof(b), "%s  " IC_BOLT, t);
    else if (p >= 0 && p <= 15) snprintf(b, sizeof(b), "%s  " IC_BAT_0, t);
    else snprintf(b, sizeof(b), "%s", t);
    ui_set_text(status_lbl, b);
}

/* ---------------- screen power ---------------- */

void ui_brightness_override(int pct)
{
    if (pct == bright_override) return;   // called every tick by the pendant: only real changes
    bright_override = pct;
    if (!off) ui_apply_brightness();
}

void ui_apply_brightness(void)
{
    int b = off ? 0 : bright_override >= 0 ? bright_override : g_set.brightness;
    display_set_brightness(b);
}

static void touch_policy(void)
{
    const screen_t *s = ui_current();
    input_set_touch(!locked && !(s && (s->flags & SCR_NO_TOUCH)));
}

void ui_screen_off(bool lock)
{
    off = true;
    locked = lock;
    display_set_brightness(0);
    touch_policy();
}

void ui_wake(void)
{
    if (!off) return;
    off = false;
    locked = false;
    input_mark_activity();
    power_motion_reset();
    touch_policy();
    ui_apply_brightness();
}

bool ui_is_off(void) { return off; }
bool ui_is_locked(void) { return locked; }

static void power_off_check(lv_timer_t *t)
{
    // still running: on USB power the PMU keeps the board alive
    lv_timer_delete(t);
    ui_home();
    ui_toast(T(S_USB_STAYS_ON));
}

void ui_power_off(void)
{
    ESP_LOGW(TAG, "power off");
    if (display_direct_on()) display_direct_end();
    lv_obj_t *top = lv_layer_top();
    lv_obj_t *bye = lv_obj_create(top);
    lv_obj_remove_style_all(bye);
    lv_obj_set_size(bye, SCR_W, SCR_H);
    lv_obj_set_style_bg_color(bye, C_BG, 0);
    lv_obj_set_style_bg_opa(bye, LV_OPA_COVER, 0);
    lv_obj_t *ic = ui_label(bye, &font_icon, ui_accent(), IC_GEM);
    lv_obj_align(ic, LV_ALIGN_CENTER, 0, -30);
    lv_obj_t *l = ui_label(bye, &font_m, C_TEXT, T(S_GOODBYE));
    lv_obj_align(l, LV_ALIGN_CENTER, 0, 50);
    lv_refr_now(NULL);
    settings_save();
    vTaskDelay(pdMS_TO_TICKS(900));
    board_power_off();
    vTaskDelay(pdMS_TO_TICKS(1500));
    lv_obj_delete(bye);
    lv_timer_create(power_off_check, 10, NULL);
}

/* ---------------- stack ---------------- */

const screen_t *ui_current(void) { return depth ? stack[depth - 1].s : NULL; }
bool ui_closing(void) { return closing; }

static void build(void)
{
    const screen_t *s = stack[depth - 1].s;
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    input_set_double_tap(s->flags & SCR_DTAP);
    touch_policy();
    s->enter(scr, stack[depth - 1].arg);
    // straight switch: a fade redraws the whole screen for several frames (tearing on this
    // panel, and battery)
    lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
    status_update();
}

static void leave_top(bool is_closing)
{
    const screen_t *s = ui_current();
    if (!s || !s->leave) return;
    closing = is_closing;
    s->leave();
    closing = false;
}

void ui_push(const screen_t *s, void *arg)
{
    if (depth >= STACK_MAX) return;
    if (depth) leave_top(false);
    stack[depth].s = s;
    stack[depth].arg = arg;
    depth++;
    build();
}

void ui_pop(void)
{
    if (depth <= 1) return;
    leave_top(true);
    depth--;
    build();
}

void ui_home(void)
{
    if (depth <= 1) return;
    leave_top(true);
    depth = 1;
    build();
}

void ui_rebuild(void)
{
    if (!depth) return;
    leave_top(false);
    build();
}

/* ---------------- events ---------------- */

static bool is_touch_ev(ev_t e) { return e >= EV_TAP && e <= EV_DIAL_CCW; }

static void on_event(ev_t e, int x, int y)
{
    if (e == EV_PWR_LONG) { ui_power_off(); return; }
    if (off) {
        if (e == EV_PWR_CLICK) { ui_wake(); return; }
        if (!locked && (e == EV_TOUCH_DOWN || e == EV_BOOT_CLICK)) {
            ui_wake();
            swallow = e == EV_TOUCH_DOWN;   // the touch that woke the screen does nothing else
        }
        return;
    }
    if (e == EV_TOUCH_DOWN) { swallow = false; return; }
    if (swallow && is_touch_ev(e)) return;
    if (e == EV_PWR_CLICK) { ui_screen_off(true); return; }
    const screen_t *s = ui_current();
    if (e == EV_PULL_DOWN && !(s->flags & SCR_NO_PULL)) { ui_push(&scr_quick, NULL); return; }
    if (s->event && s->event(e, x, y)) return;
    if (e == EV_SWIPE_RIGHT || e == EV_BOOT_CLICK) { ui_pop(); return; }
    if (e == EV_BOOT_LONG) { ui_home(); return; }
}

static lv_timer_t *hold_timer, *tick_timer;
static int64_t off_since_us;

bool ui_clock_keeps_on(void)
{
    // a night window set to "screen off" wins: the clock goes off like any other screen
    return ui_current() == &scr_clock && g_set.clock_on && !(power_night() && g_set.night_mode == 1);
}

bool ui_clock_ambient(void)
{
    uint32_t limit = (g_set.sleep_s ? g_set.sleep_s : 30) * 1000u;
    return !off && ui_clock_keeps_on() && input_idle_ms() > limit;
}

// Battery: how often the inputs are polled and whether the Wi-Fi may sleep
static void power_policy(void)
{
    const screen_t *s = ui_current();
    bool pendant = s == &scr_pendant;
    int64_t now = esp_timer_get_time();
    if (!off) off_since_us = 0;
    else if (!off_since_us) off_since_us = now;
    bool ambient = ui_clock_ambient();
    input_set_poll_ms(off ? 60 : (ambient || (s && (s->flags & SCR_NO_TOUCH))) ? 30 : 12);
    lv_timer_set_period(hold_timer, off ? 120 : 40);
    // locked with PWR: only PWR can wake it (handled by the input task), so the housekeeping
    // timer slows down to once a second (battery checks and the like keep running)
    lv_timer_set_period(tick_timer, off && locked ? 1000 : 200);
    // the home Wi-Fi is not needed while worn, nor with the screen off for a minute; the
    // pages that use it (upload, Wi-Fi, updates) have the screen on and bring it back
    bool idle_off = off && now - off_since_us > 60LL * 1000 * 1000;
    wifi_mgr_set_suspend(g_set.wifi_save && (pendant || idle_off || ambient));
}

static void tick_cb(lv_timer_t *t)
{
    power_policy();
    power_tick();
    if (toast_flag) { toast_flag = false; ui_toast(toast_pending); }
    if (toast_until && (int32_t)(lv_tick_get() - toast_until) > 0) {
        toast_until = 0;
        lv_obj_add_flag(toast_box, LV_OBJ_FLAG_HIDDEN);
    }
    const screen_t *s = ui_current();
    if (s && s->tick) s->tick();
    status_update();
    // menus: off after the idle time (pendant mode has its own rules)
    if (!off && s && !(s->flags & SCR_NO_SLEEP) && !ui_clock_keeps_on() && g_set.sleep_s &&
        input_idle_ms() > g_set.sleep_s * 1000u)
        ui_screen_off(false);
}

static void hold_cb(lv_timer_t *t) { hold_update(); }

void ui_init(void)
{
    ui_apply_tz();
    overlays_create();
    input_init(on_event);
    tick_timer = lv_timer_create(tick_cb, 200, NULL);
    hold_timer = lv_timer_create(hold_cb, 40, NULL);
    ui_push(&scr_home, NULL);
}
