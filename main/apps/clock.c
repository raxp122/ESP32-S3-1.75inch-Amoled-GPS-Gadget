// clock.c - a quiet watch face: thin digits, the date, seconds as a ring along the edge.
#include "ui.h"
#include "power.h"
#include <stdio.h>
#include <time.h>

static lv_obj_t *time_l, *date_l, *sec_arc, *bat_l;
static int last_sec = -1;

static void update(void)
{
    time_t t = time(NULL);
    struct tm lt;
    localtime_r(&t, &lt);
    if (lt.tm_sec == last_sec) return;
    last_sec = lt.tm_sec;
    char b[48];
    ui_time_str(b, sizeof(b));
    ui_set_text(time_l, b);
    if (power_time_valid()) snprintf(b, sizeof(b), "%s %d %s", i18n_weekday(lt.tm_wday), lt.tm_mday, i18n_month(lt.tm_mon));
    else snprintf(b, sizeof(b), "%s", T(S_TIME_UNKNOWN));
    ui_set_text(date_l, b);
    lv_arc_set_value(sec_arc, power_time_valid() ? lt.tm_sec : 0);
    ui_battery_icon(b, sizeof(b));
    ui_set_text(bat_l, b);
}

static void enter(lv_obj_t *root, void *arg)
{
    // minute marks
    for (int i = 0; i < 12; i++) {
        int d = i % 3 ? 5 : 8, r = SCR_C - 34;
        lv_obj_t *o = ui_circle(root, d, i % 3 ? C_FAINT : C_DIM);
        lv_obj_set_pos(o, SCR_C + r * lv_trigo_cos(i * 30 - 90) / LV_TRIGO_SIN_MAX - d / 2,
                       SCR_C + r * lv_trigo_sin(i * 30 - 90) / LV_TRIGO_SIN_MAX - d / 2);
    }
    sec_arc = lv_arc_create(root);
    lv_obj_set_size(sec_arc, SCR_W - 10, SCR_H - 10);
    lv_obj_center(sec_arc);
    lv_arc_set_rotation(sec_arc, 270);
    lv_arc_set_bg_angles(sec_arc, 0, 360);
    lv_arc_set_range(sec_arc, 0, 59);
    lv_obj_remove_style(sec_arc, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_width(sec_arc, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_color(sec_arc, C_CARD, LV_PART_MAIN);
    lv_obj_set_style_arc_width(sec_arc, 3, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(sec_arc, ui_accent(), LV_PART_INDICATOR);

    time_l = ui_label(root, &font_xl, C_TEXT, "");
    lv_obj_align(time_l, LV_ALIGN_CENTER, 0, -18);
    date_l = ui_label(root, &font_m, ui_accent(), "");
    lv_obj_align(date_l, LV_ALIGN_CENTER, 0, 62);
    bat_l = ui_label(root, &font_s, C_DIM, "");
    lv_obj_align(bat_l, LV_ALIGN_CENTER, 0, 132);
    last_sec = -1;
    update();
}

static void tick(void) { update(); }

const screen_t scr_clock = {"clock", enter, NULL, NULL, tick, SCR_NO_STATUS};
