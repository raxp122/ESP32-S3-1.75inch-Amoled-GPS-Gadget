// ota_app.c - firmware update: a ring for the progress, one tap to check, two to install
#include "ui.h"
#include "ota.h"
#include "wifi_mgr.h"
#include <stdio.h>

static lv_obj_t *arc, *icon_l, *ver_l, *state_l, *hint_l;
static bool armed;   // first tap on "install" done
static ota_state_t last = (ota_state_t)-1;

static void update(void)
{
    ota_state_t s = ota_state();
    char b[96];
    int p = s == OTA_DOWNLOADING || s == OTA_DONE ? ota_progress() : s == OTA_CHECKING ? 15 : 0;
    lv_arc_set_value(arc, p);
    snprintf(b, sizeof(b), "%s %s", T(S_INFO_VERSION), ota_current());
    ui_set_text(ver_l, b);
    switch (s) {
    case OTA_CHECKING: snprintf(b, sizeof(b), "%s", T(S_OTA_CHECKING)); break;
    case OTA_UP_TO_DATE: snprintf(b, sizeof(b), "%s", T(S_OTA_UP_TO_DATE)); break;
    case OTA_AVAILABLE: snprintf(b, sizeof(b), T(S_OTA_AVAILABLE), ota_latest()); break;
    case OTA_DOWNLOADING: snprintf(b, sizeof(b), "%s  %d%%", T(S_OTA_DOWNLOADING), ota_progress()); break;
    case OTA_DONE: snprintf(b, sizeof(b), "%s", T(S_OTA_DONE)); break;
    case OTA_ERROR: snprintf(b, sizeof(b), "%s", ota_error()); break;
    default: b[0] = 0; break;
    }
    ui_set_text(state_l, b);
    lv_obj_set_style_text_color(state_l, s == OTA_ERROR ? C_WARN : s == OTA_AVAILABLE || s == OTA_DONE ? C_OK : C_TEXT, 0);
    const char *h = "";
    if (s == OTA_AVAILABLE) h = T(armed ? S_TAP_AGAIN : S_OTA_TAP_INSTALL);
    else if (s != OTA_CHECKING && s != OTA_DOWNLOADING && s != OTA_DONE) h = T(S_OTA_TAP_CHECK);
    ui_set_text(hint_l, h);
    lv_obj_set_style_text_color(hint_l, armed ? C_WARN : C_DIM, 0);
    if (s != last) {
        ui_set_text(icon_l, s == OTA_ERROR ? IC_WARN : s == OTA_DONE || s == OTA_UP_TO_DATE ? IC_CHECK : IC_DOWNLOAD);
        last = s;
    }
}

static void enter(lv_obj_t *root, void *arg)
{
    arc = lv_arc_create(root);
    lv_obj_set_size(arc, SCR_W - 30, SCR_H - 30);
    lv_obj_center(arc);
    lv_arc_set_rotation(arc, 270);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_range(arc, 0, 100);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_width(arc, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, C_CARD, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, ui_accent(), LV_PART_INDICATOR);
    icon_l = ui_label(root, &font_icon, ui_accent(), IC_DOWNLOAD);
    lv_obj_align(icon_l, LV_ALIGN_CENTER, 0, -80);
    ver_l = ui_label(root, &font_s, C_DIM, "");
    lv_obj_align(ver_l, LV_ALIGN_CENTER, 0, -18);
    state_l = ui_label(root, &font_m, C_TEXT, "");
    lv_obj_set_width(state_l, 340);
    lv_label_set_long_mode(state_l, LV_LABEL_LONG_WRAP);
    lv_obj_align(state_l, LV_ALIGN_CENTER, 0, 34);
    hint_l = ui_label(root, &font_s, C_DIM, "");
    lv_obj_align(hint_l, LV_ALIGN_CENTER, 0, 110);
    armed = false;
    last = (ota_state_t)-1;
    if (ota_state() == OTA_IDLE || ota_state() == OTA_ERROR) ota_check();
    update();
}

static bool event(ev_t e, int x, int y)
{
    if (e != EV_TAP) return false;
    ota_state_t s = ota_state();
    if (s == OTA_AVAILABLE) {
        if (!armed) { armed = true; update(); return true; }
        armed = false;
        ota_install();
    } else if (s != OTA_CHECKING && s != OTA_DOWNLOADING && s != OTA_DONE) {
        ota_check();
    }
    update();
    return true;
}

static void tick(void) { update(); }

const screen_t scr_ota = {"ota", enter, NULL, event, tick, SCR_NO_SLEEP};
