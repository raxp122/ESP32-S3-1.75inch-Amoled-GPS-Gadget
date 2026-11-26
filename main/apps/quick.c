// quick.c - quick panel (swipe down from the top edge): time, battery, brightness on the
// bezel, and three shortcuts: pendant mode, lock, Wi-Fi. Swipe up / right or BOOT closes it.
#include "ui.h"
#include "board.h"
#include "settings.h"
#include "wifi_mgr.h"
#include "ota.h"
#include <stdio.h>
#include <time.h>

static lv_obj_t *time_l, *date_l, *arc, *bright_l, *btn[3], *btn_ic[3];
static const int btn_x[3] = {-112, 0, 112};

static void update(void)
{
    char b[48];
    ui_time_str(b, sizeof(b));
    ui_set_text(time_l, b);
    char bat[32];
    ui_battery_icon(bat, sizeof(bat));
    snprintf(b, sizeof(b), "%s   " IC_WIFI " %s", bat, wifi_mgr_state() == WIFI_CONNECTED ? "" : "-");
    ui_set_text(date_l, b);
    snprintf(b, sizeof(b), IC_SUN "  %d%%", g_set.brightness);
    ui_set_text(bright_l, b);
    lv_arc_set_value(arc, g_set.brightness);
    bool on = g_set.wifi_on;
    lv_obj_set_style_bg_color(btn[2], on ? ui_accent_dark() : C_CARD, 0);
    lv_obj_set_style_text_color(btn_ic[2], on ? ui_accent() : C_DIM, 0);
}

static void enter(lv_obj_t *root, void *arg)
{
    arc = lv_arc_create(root);
    lv_obj_set_size(arc, SCR_W - 20, SCR_H - 20);
    lv_obj_center(arc);
    lv_arc_set_bg_angles(arc, 135, 45);   // 270 degrees, open at the bottom
    lv_arc_set_range(arc, 5, 100);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_width(arc, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, C_CARD, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, ui_accent(), LV_PART_INDICATOR);

    time_l = ui_label(root, &font_l, C_TEXT, "");
    lv_obj_align(time_l, LV_ALIGN_CENTER, 0, -118);
    date_l = ui_label(root, &font_s, C_DIM, "");
    lv_obj_align(date_l, LV_ALIGN_CENTER, 0, -72);
    static const char *icons[3] = {IC_GEM, IC_LOCK, IC_WIFI};
    static const int names[3] = {S_APP_PENDANT, S_LOCK, S_WIFI};
    for (int i = 0; i < 3; i++) {
        btn[i] = ui_circle(root, 92, i == 0 ? ui_accent_dark() : C_CARD);
        lv_obj_align(btn[i], LV_ALIGN_CENTER, btn_x[i], 8);
        btn_ic[i] = ui_label(btn[i], &font_l, i == 0 ? ui_accent() : C_TEXT, icons[i]);
        lv_obj_center(btn_ic[i]);
        lv_obj_t *l = ui_label(root, &font_s, C_DIM, T(names[i]));
        lv_obj_align(l, LV_ALIGN_CENTER, btn_x[i], 76);
    }
    bright_l = ui_label(root, &font_s, ui_accent(), "");
    lv_obj_align(bright_l, LV_ALIGN_CENTER, 0, 150);
    update();
}

static void bright(int d)
{
    int b = g_set.brightness + d * 5;
    g_set.brightness = b < 5 ? 5 : b > 100 ? 100 : b;
    ui_apply_brightness();
    settings_mark();
    update();
}

static bool event(ev_t e, int x, int y)
{
    switch (e) {
    case EV_DIAL_CW: bright(1); return true;
    case EV_DIAL_CCW: bright(-1); return true;
    case EV_SWIPE_UP: case EV_SWIPE_RIGHT: case EV_BOOT_CLICK: ui_pop(); return true;
    case EV_TAP:
        for (int i = 0; i < 3; i++) {
            int dx = x - (SCR_C + btn_x[i]), dy = y - (SCR_C + 8);
            if (dx * dx + dy * dy > 56 * 56) continue;
            if (i == 0) { ui_pop(); pendant_resume(); }
            else if (i == 1) { ui_pop(); ui_screen_off(true); }
            else { g_set.wifi_on = !g_set.wifi_on; settings_mark(); wifi_mgr_apply(); if (g_set.wifi_on) ota_auto_start(); update(); }
            return true;
        }
        return true;
    default: return false;
    }
}

static void leave(void) { settings_flush(); }
static void tick(void) { update(); }

const screen_t scr_quick = {"quick", enter, leave, event, tick, SCR_NO_STATUS | SCR_NO_PULL};
