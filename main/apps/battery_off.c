// battery_off.c - the battery went under the power-off threshold (Settings › Battery) and no
// charger is plugged in: the pendant switches off in 30 s to protect the cell (power.c counts
// down). The button opens the threshold setting, which gives another minute; plugging in the
// charger or lowering the threshold calls the power off off and this screen goes away.
#include "ui.h"
#include "power.h"
#include "settings.h"
#include <stdio.h>

#define BTN_W 300
#define BTN_H 60
#define BTN_Y 128   // from the centre

static lv_obj_t *title_l, *msg_l, *btn_l;

static void update(void)
{
    int s = power_shutdown_in_s();
    char b[96];
    snprintf(b, sizeof(b), T(S_BATT_OFF_TITLE), power_battery_percent());
    ui_set_text(title_l, b);
    snprintf(b, sizeof(b), T(S_BATT_OFF_MSG), s < 0 ? 0 : s);
    ui_set_text(msg_l, b);
    snprintf(b, sizeof(b), "%s %d%%", T(S_BATT_OFF_AT), g_set.battery_off_pct);
    ui_set_text(btn_l, b);
}

static void enter(lv_obj_t *root, void *arg)
{
    lv_obj_t *ic = ui_label(root, &font_icon, C_WARN, IC_BAT_0);
    lv_obj_align(ic, LV_ALIGN_CENTER, 0, -128);
    title_l = ui_label(root, &font_l, C_TEXT, "");
    lv_obj_align(title_l, LV_ALIGN_CENTER, 0, -50);
    msg_l = ui_label(root, &font_s, C_TEXT, "");
    lv_obj_set_style_text_align(msg_l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(msg_l, LV_ALIGN_CENTER, 0, 10);
    lv_obj_t *hint = ui_label(root, &font_xs, C_DIM, T(S_BATT_OFF_HINT));
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 64);
    lv_obj_t *btn = lv_obj_create(root);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, BTN_W, BTN_H);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, BTN_Y);
    lv_obj_set_style_radius(btn, BTN_H / 2, 0);
    lv_obj_set_style_bg_color(btn, ui_accent_dark(), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    btn_l = ui_label(btn, &font_s, ui_accent(), "");
    lv_obj_center(btn_l);
    update();
}

static void tick(void)
{
    if (power_shutdown_in_s() < 0) { ui_pop(); return; }   // charger plugged in / threshold lowered
    update();
}

static bool event(ev_t e, int x, int y)
{
    if (e == EV_TAP) {
        int dx = x - SCR_C, dy = y - (SCR_C + BTN_Y);
        if (dx * dx * 4 <= BTN_W * BTN_W && dy * dy * 4 <= (BTN_H + 30) * (BTN_H + 30)) {
            power_shutdown_extend(60);   // time to choose
            settings_open_battery();
        }
        return true;
    }
    // BOOT does not dismiss it: the power off goes on in the background anyway
    return e == EV_BOOT_CLICK || e == EV_BOOT_LONG || e == EV_SWIPE_RIGHT;
}

const screen_t scr_battery_off = {"battery-off", enter, NULL, event, tick, SCR_NO_STATUS | SCR_NO_SLEEP | SCR_NO_PULL};
