// menu.c - the round menu (see ui.h)
#include "ui.h"
#include "settings.h"
#include <stdio.h>
#include <string.h>

static menu_t *m;
static lv_obj_t *title, *group, *prev, *next, *track, *thumb;
static bool editing;
static int confirm_sel = -1;
static uint32_t confirm_until;

static void anim_ty(void *o, int32_t v) { lv_obj_set_style_translate_y(o, v, 0); }
static lv_obj_t *icon, *label, *value;
// fade of the middle item through the text opacity of its labels: a whole-object opacity
// would make LVGL draw the group in a separate layer (slow, and it tears on this panel)
static void anim_opa(void *o, int32_t v)
{
    lv_obj_set_style_text_opa(icon, (lv_opa_t)v, 0);
    lv_obj_set_style_text_opa(label, (lv_opa_t)v, 0);
    lv_obj_set_style_text_opa(value, (lv_opa_t)v, 0);
}

static const char *item_text(const menu_item_t *it) { return it->str >= 0 ? T(it->str) : (it->text ? it->text : ""); }

static void refresh(int dir)
{
    if (!m || !m->count) return;
    const menu_item_t *it = &m->items[m->sel];
    lv_label_set_text(icon, it->icon ? it->icon : "");
    lv_label_set_text(label, item_text(it));
    char v[96] = "";
    if (confirm_sel == m->sel) strlcpy(v, T(S_TAP_AGAIN), sizeof(v));
    else if (it->value) it->value(v, sizeof(v));
    if (editing) {
        char e[112];
        snprintf(e, sizeof(e), IC_LEFT "   %s   " IC_RIGHT, v);
        lv_label_set_text(value, e);
        lv_obj_set_style_text_font(value, &font_m, 0);
        lv_obj_set_style_text_color(value, ui_accent(), 0);
        lv_obj_set_style_text_color(label, C_DIM, 0);
    } else {
        lv_label_set_text(value, v);
        lv_obj_set_style_text_font(value, &font_s, 0);
        lv_obj_set_style_text_color(value, confirm_sel == m->sel ? C_WARN : it->adjust ? ui_accent() : C_DIM, 0);
        lv_obj_set_style_text_color(label, C_TEXT, 0);
    }
    lv_obj_align(value, LV_ALIGN_CENTER, 0, editing ? 58 : 52);
    lv_label_set_text(prev, m->sel > 0 ? item_text(&m->items[m->sel - 1]) : "");
    lv_label_set_text(next, m->sel < m->count - 1 ? item_text(&m->items[m->sel + 1]) : "");

    // position on the arc at the right edge (330..30 degrees)
    if (m->count > 1) {
        int span = 60, seg = span / m->count < 6 ? 6 : span / m->count;
        int start = 330 + (span - seg) * m->sel / (m->count - 1);
        lv_arc_set_bg_angles(thumb, start % 360, (start + seg) % 360);
    }
    if (dir) {
        lv_obj_set_style_translate_y(group, dir * 26, 0);
        anim_opa(NULL, LV_OPA_40);
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, group);
        lv_anim_set_values(&a, dir * 26, 0);
        lv_anim_set_duration(&a, 150);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_set_exec_cb(&a, anim_ty);
        lv_anim_start(&a);
        lv_anim_t b = a;
        lv_anim_set_values(&b, LV_OPA_40, LV_OPA_COVER);
        lv_anim_set_exec_cb(&b, anim_opa);
        lv_anim_start(&b);
    }
}

static lv_obj_t *arc(lv_obj_t *root, int a1, int a2, lv_color_t c, int w)
{
    lv_obj_t *o = lv_arc_create(root);
    lv_obj_set_size(o, SCR_W - 18, SCR_H - 18);
    lv_obj_center(o);
    lv_obj_remove_style(o, NULL, LV_PART_KNOB);
    lv_obj_remove_style(o, NULL, LV_PART_INDICATOR);
    lv_arc_set_bg_angles(o, a1, a2);
    lv_obj_set_style_arc_color(o, c, LV_PART_MAIN);
    lv_obj_set_style_arc_width(o, w, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(o, true, LV_PART_MAIN);
    return o;
}

static void enter(lv_obj_t *root, void *arg)
{
    static menu_t *last;
    // a rebuild of the same menu (new accent, language) stays in edit mode
    if (arg != last) editing = false;
    last = m = arg;
    confirm_sel = -1;
    if (m->sel >= m->count) m->sel = m->count ? m->count - 1 : 0;
    title = ui_label(root, &font_s, ui_accent(), T(m->title));
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 50);
    prev = ui_label(root, &font_m, C_FAINT, "");
    lv_obj_set_width(prev, 320);
    lv_label_set_long_mode(prev, LV_LABEL_LONG_DOT);
    lv_obj_align(prev, LV_ALIGN_CENTER, 0, -128);
    next = ui_label(root, &font_m, C_FAINT, "");
    lv_obj_set_width(next, 320);
    lv_label_set_long_mode(next, LV_LABEL_LONG_DOT);
    lv_obj_align(next, LV_ALIGN_CENTER, 0, 128);
    lv_obj_set_style_text_color(prev, C_DIM, 0);
    lv_obj_set_style_text_color(next, C_DIM, 0);
    lv_obj_set_style_text_opa(prev, LV_OPA_60, 0);
    lv_obj_set_style_text_opa(next, LV_OPA_60, 0);

    group = lv_obj_create(root);
    lv_obj_remove_style_all(group);
    lv_obj_set_size(group, SCR_W, 210);
    lv_obj_center(group);
    lv_obj_remove_flag(group, LV_OBJ_FLAG_SCROLLABLE);
    icon = ui_label(group, &font_m, ui_accent(), "");
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -58);
    label = ui_label(group, &font_l, C_TEXT, "");
    lv_obj_set_width(label, 390);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    value = ui_label(group, &font_s, C_DIM, "");
    lv_obj_set_width(value, 380);
    lv_label_set_long_mode(value, LV_LABEL_LONG_DOT);

    track = arc(root, 330, 30, C_FAINT, 4);
    thumb = arc(root, 330, 340, ui_accent(), 8);
    if (m->count <= 1) { lv_obj_add_flag(track, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(thumb, LV_OBJ_FLAG_HIDDEN); }
    refresh(0);
}

static void move(int d)
{
    int n = m->sel + d;
    if (n < 0 || n >= m->count) return;
    m->sel = n;
    confirm_sel = -1;
    refresh(d);
}

static void activate(void)
{
    const menu_item_t *it = &m->items[m->sel];
    if (it->adjust) { editing = !editing; refresh(0); return; }
    if (it->confirm && confirm_sel != m->sel) {
        confirm_sel = m->sel;
        confirm_until = lv_tick_get() + 4000;
        refresh(0);
        return;
    }
    confirm_sel = -1;
    menu_t *before = m;
    if (it->select) it->select();
    if (it->open) { ui_push(it->open, it->arg); return; }
    if (m == before && m) refresh(0);   // still this menu (select() may have changed screen)
}

static bool event(ev_t e, int x, int y)
{
    if (!m || !m->count) return false;
    const menu_item_t *it = &m->items[m->sel];
    if (editing) {
        switch (e) {
        case EV_DIAL_CW: case EV_SWIPE_UP: case EV_SWIPE_LEFT: it->adjust(1); refresh(0); return true;
        case EV_DIAL_CCW: case EV_SWIPE_DOWN: it->adjust(-1); refresh(0); return true;
        case EV_TAP: case EV_SWIPE_RIGHT: case EV_BOOT_CLICK: editing = false; refresh(0); return true;
        default: return false;
        }
    }
    switch (e) {
    case EV_DIAL_CW: case EV_SWIPE_UP: move(1); return true;
    case EV_DIAL_CCW: case EV_SWIPE_DOWN: move(-1); return true;
    case EV_TAP:
        if (y < SCR_C - 85) move(-1);
        else if (y > SCR_C + 85) move(1);
        else activate();
        return true;
    default: return false;
    }
}

static void tick(void)
{
    if (!m || !m->count) return;
    if (confirm_sel >= 0 && (int32_t)(lv_tick_get() - confirm_until) > 0) { confirm_sel = -1; refresh(0); return; }
    // values that change by themselves (Wi-Fi state, battery...)
    const menu_item_t *it = &m->items[m->sel];
    if (it->value && !editing && confirm_sel < 0) {
        char v[96];
        it->value(v, sizeof(v));
        ui_set_text(value, v);
    }
}

static void leave(void)
{
    m = NULL;
    settings_flush();   // values changed with adjust() are saved once, here
}

const screen_t scr_menu = {"menu", enter, leave, event, tick, 0};
