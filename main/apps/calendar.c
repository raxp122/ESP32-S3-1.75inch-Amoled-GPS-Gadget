// calendar.c - a calendar made of three concentric rings: days outside, months in the middle,
// years inside. The chosen values sit in a window at 12 o'clock; the date is in the middle.
//
// Bezel: turns the active ring (passing the end of a month turns the months ring too, the end
// of a year the years ring). Swipe: the active ring, days -> months -> years -> days (left / up
// forward, right / down back). Tap on a ring: that ring becomes active. Tap in the middle: the
// day page. Long press: back to today. BOOT leaves.
//
// The day page: weekday, date, working day or national holiday (no names), how far from today,
// the next holiday, ISO week and day of the year. The bezel moves by one day.
//
// The rings move by placing the labels again (no LVGL transforms); a short slide follows each
// step. Dates come from core.c (tested).
#include "ui.h"
#include "core.h"
#include "power.h"
#include "settings.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

enum { RING_D = 0, RING_M, RING_Y, RINGS };
#define SLOTS 31          // labels per ring (days use up to 31, months and years 12)
#define WIN_W 58
#define C_WIN lv_color_hex(0x1E1C18)

static const int ring_r[RINGS] = {205, 150, 97};

typedef struct {
    lv_obj_t *track;
    lv_obj_t *lbl[SLOTS];
    int n;                // slots in use
    float ofs;            // slide in progress, in slots (0 = at rest)
    float from;           // ofs when the slide started
} ring_t;

static ring_t rings[RINGS];
static lv_obj_t *win, *wday_l, *date_l, *year_l, *dots[RINGS];
static date_t cur, today;
static int mode;              // the ring the bezel turns
static bool keep_date;        // coming back from the day page: keep the chosen date

static date_t now_date(void)
{
    if (!power_time_valid()) return (date_t){2026, 1, 1};
    time_t t = time(NULL);
    struct tm lt;
    localtime_r(&t, &lt);
    return (date_t){(int16_t)(lt.tm_year + 1900), (uint8_t)(lt.tm_mon + 1), (uint8_t)lt.tm_mday};
}

static bool same_date(date_t a, date_t b) { return a.y == b.y && a.m == b.m && a.d == b.d; }

// "nov", "Nov": the first three letters of the month name work in Italian and in English
static void month_short(int m, char *b, int n, bool upper)
{
    snprintf(b, n, "%.3s", i18n_month(m - 1));
    for (char *p = b; *p; p++) *p = upper ? (char)toupper((unsigned char)*p) : *p;
}

// "27 nov" / "Nov 27"
static void day_month(date_t d, char *b, int n)
{
    char m[8];
    month_short(d.m, m, sizeof(m), false);
    if (g_set.lang == LANG_IT) snprintf(b, n, "%d %s", d.d, m);
    else snprintf(b, n, "%s %d", m, d.d);
}

static const char *weekday_name(date_t d) { return i18n_weekday((date_weekday(d) + 1) % 7); }

/* ---------------- rings ---------------- */

// value shown by slot i of a ring, relative to the selection: k = i - selected slot
static int sel_slot(int r)
{
    if (r == RING_D) return cur.d - 1;
    if (r == RING_M) return cur.m - 1;
    return 6;   // years: the slots are -6..+5 around the current year, the current one is slot 6
}

static void style_ring(int r)
{
    ring_t *g = &rings[r];
    bool active = r == mode;
    int sel = sel_slot(r);
    lv_obj_set_style_arc_color(g->track, active ? ui_accent_dark() : C_CARD, LV_PART_MAIN);
    for (int i = 0; i < SLOTS; i++) {
        lv_obj_t *l = g->lbl[i];
        if (i >= g->n) { lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN); continue; }
        lv_obj_remove_flag(l, LV_OBJ_FLAG_HIDDEN);
        char b[12];
        lv_color_t c = active ? C_TEXT : C_DIM;
        if (r == RING_D) {
            date_t d = {cur.y, cur.m, (uint8_t)(i + 1)};
            snprintf(b, sizeof(b), "%d", i + 1);
            if (date_holiday_it(d)) c = ui_accent();
            else if (date_weekday(d) == 6) c = lv_color_mix(ui_accent(), c, 128);
            if (same_date(d, today) && i != sel) c = lv_color_mix(ui_accent(), C_TEXT, 160);
        } else if (r == RING_M) {
            month_short(i + 1, b, sizeof(b), true);
        } else {
            int y = cur.y + i - 6;
            if (i == sel) snprintf(b, sizeof(b), "%d", y);
            else snprintf(b, sizeof(b), "'%02d", ((y % 100) + 100) % 100);
        }
        ui_set_text(l, b);
        if (i == sel) {
            lv_obj_set_style_text_font(l, active ? &font_s : &font_xs, 0);
            lv_obj_set_style_text_color(l, active ? ui_accent() : C_TEXT, 0);
        } else {
            lv_obj_set_style_text_font(l, &font_xs, 0);
            lv_obj_set_style_text_color(l, active ? c : lv_color_mix(c, C_BG, 150), 0);
        }
    }
}

static void place_ring(int r)
{
    ring_t *g = &rings[r];
    int sel = sel_slot(r);
    float step = 2 * (float)M_PI / (r == RING_D ? g->n : 12);
    for (int i = 0; i < g->n; i++) {
        int k = i - sel;
        if (r == RING_D) {   // the nearest way round
            if (k > g->n / 2) k -= g->n;
            if (k < -g->n / 2) k += g->n;
        }
        // later values counter-clockwise: turning the bezel clockwise (forward) brings them to
        // the window, so the ring turns with the finger
        float a = -(float)M_PI / 2 - (k + g->ofs) * step;
        lv_obj_align(g->lbl[i], LV_ALIGN_CENTER, (int)lroundf(ring_r[r] * cosf(a)),
                     (int)lroundf(ring_r[r] * sinf(a)));
    }
}

static void slide_cb(void *var, int32_t v)
{
    ring_t *g = var;
    g->ofs = g->from * (1000 - v) / 1000.0f;
    place_ring((int)(g - rings));
}

static void slide(int r, int dir)
{
    ring_t *g = &rings[r];
    lv_anim_delete(g, slide_cb);
    g->ofs += dir;   // the labels start where they were and slide to the new places
    if (g->ofs > 2) g->ofs = 2;
    if (g->ofs < -2) g->ofs = -2;
    g->from = g->ofs;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, g);
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_duration(&a, 180);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, slide_cb);
    lv_anim_start(&a);
}

static void update_centre(void)
{
    char b[32];
    ui_set_text(wday_l, weekday_name(cur));
    day_month(cur, b, sizeof(b));
    ui_set_text(date_l, b);
    lv_obj_set_style_text_color(date_l, date_holiday_it(cur) ? ui_accent() : C_TEXT, 0);
    snprintf(b, sizeof(b), "%d", cur.y);
    ui_set_text(year_l, b);
    for (int r = 0; r < RINGS; r++) {
        lv_obj_set_size(dots[r], r == mode ? 9 : 6, r == mode ? 9 : 6);
        lv_obj_set_style_bg_color(dots[r], r == mode ? ui_accent() : C_FAINT, 0);
        lv_obj_align(dots[r], LV_ALIGN_CENTER, (r - 1) * 18, 54);
    }
}

static void refresh(void)
{
    rings[RING_D].n = date_days_in_month(cur.y, cur.m);
    for (int r = 0; r < RINGS; r++) { style_ring(r); place_ring(r); }
    update_centre();
}

static void go(int dir)
{
    date_t old = cur;
    if (mode == RING_D) cur = date_add_days(cur, dir);
    else if (mode == RING_M) cur = date_add_months(cur, dir);
    else cur = date_add_years(cur, dir);
    if (same_date(old, cur)) return;   // at the end of the supported range
    refresh();
    // every ring that moved slides by one step in the direction of travel
    if (mode == RING_D) slide(RING_D, dir);
    if (cur.m != old.m) slide(RING_M, dir);
    if (cur.y != old.y) slide(RING_Y, dir);
}

static void set_mode(int m)
{
    mode = (m % RINGS + RINGS) % RINGS;
    for (int r = 0; r < RINGS; r++) style_ring(r);
    update_centre();
}

static void enter(lv_obj_t *root, void *arg)
{
    today = now_date();
    if (!keep_date) { cur = today; mode = RING_D; }
    keep_date = false;
    for (int r = 0; r < RINGS; r++) {
        ring_t *g = &rings[r];
        g->ofs = 0;
        g->track = lv_arc_create(root);
        int d = ring_r[r] * 2;
        lv_obj_set_size(g->track, d + 32, d + 32);
        lv_obj_center(g->track);
        lv_arc_set_bg_angles(g->track, 0, 360);
        lv_obj_remove_style(g->track, NULL, LV_PART_KNOB);
        lv_obj_remove_style(g->track, NULL, LV_PART_INDICATOR);
        lv_obj_remove_flag(g->track, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_arc_width(g->track, 32, LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(g->track, false, LV_PART_MAIN);
    }
    // the window at 12 o'clock across the three rings
    win = lv_obj_create(root);
    lv_obj_remove_style_all(win);
    int top = SCR_C - ring_r[RING_D] - 19, bottom = SCR_C - ring_r[RING_Y] + 19;
    lv_obj_set_size(win, WIN_W, bottom - top);
    lv_obj_set_pos(win, SCR_C - WIN_W / 2, top);
    lv_obj_set_style_radius(win, WIN_W / 2, 0);
    lv_obj_set_style_bg_color(win, C_WIN, 0);
    lv_obj_set_style_bg_opa(win, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(win, C_FAINT, 0);
    lv_obj_set_style_border_width(win, 2, 0);
    for (int r = 0; r < RINGS; r++) {
        rings[r].n = r == RING_D ? 31 : 12;
        for (int i = 0; i < SLOTS; i++) rings[r].lbl[i] = ui_label(root, &font_xs, C_DIM, "");
    }
    wday_l = ui_label(root, &font_xs, C_DIM, "");
    lv_obj_align(wday_l, LV_ALIGN_CENTER, 0, -30);
    date_l = ui_label(root, &font_m, C_TEXT, "");
    lv_obj_align(date_l, LV_ALIGN_CENTER, 0, 0);
    year_l = ui_label(root, &font_xs, C_DIM, "");
    lv_obj_align(year_l, LV_ALIGN_CENTER, 0, 30);
    for (int r = 0; r < RINGS; r++) dots[r] = ui_circle(root, 6, C_FAINT);
    refresh();
}

static void leave(void)
{
    for (int r = 0; r < RINGS; r++) lv_anim_delete(&rings[r], slide_cb);
}

static bool event(ev_t e, int x, int y)
{
    switch (e) {
    case EV_DIAL_CW: go(1); return true;
    case EV_DIAL_CCW: go(-1); return true;
    case EV_SWIPE_LEFT: case EV_SWIPE_UP: set_mode(mode + 1); return true;
    case EV_SWIPE_RIGHT: case EV_SWIPE_DOWN: set_mode(mode - 1); return true;
    case EV_LONG_PRESS:
        if (same_date(cur, today)) return true;
        cur = today;
        refresh();
        return true;
    case EV_TAP: {
        int dx = x - SCR_C, dy = y - SCR_C, d = (int)sqrtf((float)(dx * dx + dy * dy));
        if (d < ring_r[RING_Y] - 18) {
            keep_date = true;
            ui_push(&scr_calendar_day, &cur);
            return true;
        }
        for (int r = 0; r < RINGS; r++)
            if (abs(d - ring_r[r]) <= 27) { if (r != mode) set_mode(r); return true; }
        return true;
    }
    default: return false;
    }
}

const screen_t scr_calendar = {"calendar", enter, leave, event, NULL, SCR_NO_STATUS};

/* ---------------- the day page ---------------- */

static lv_obj_t *d_wday, *d_num, *d_month, *d_kind, *d_rel, *d_next, *d_week;
static date_t *dp;

static void day_update(void)
{
    char b[64], m[24];
    date_t d = *dp;
    snprintf(b, sizeof(b), "%s", weekday_name(d));
    for (char *p = b; *p; p++) *p = (char)toupper((unsigned char)*p);
    ui_set_text(d_wday, b);
    snprintf(b, sizeof(b), "%d", d.d);
    ui_set_text(d_num, b);
    snprintf(b, sizeof(b), "%s %d", i18n_month(d.m - 1), d.y);
    ui_set_text(d_month, b);
    bool off = date_holiday_it(d) || date_weekday(d) == 6;
    ui_set_text(d_kind, T(off ? S_CAL_HOLIDAY : S_CAL_WORKDAY));
    lv_obj_set_style_text_color(d_kind, off ? ui_accent() : C_DIM, 0);
    int diff = date_to_days(d) - date_to_days(today);
    if (diff == 0) snprintf(b, sizeof(b), "%s", T(S_CAL_TODAY));
    else if (diff == 1) snprintf(b, sizeof(b), "%s", T(S_CAL_TOMORROW));
    else if (diff == -1) snprintf(b, sizeof(b), "%s", T(S_CAL_YESTERDAY));
    else if (diff > 0) snprintf(b, sizeof(b), T(S_CAL_IN_DAYS), diff);
    else snprintf(b, sizeof(b), T(S_CAL_DAYS_AGO), -diff);
    ui_set_text(d_rel, b);
    date_t n = date_next_holiday_it(d);
    day_month(n, m, sizeof(m));
    snprintf(b, sizeof(b), T(S_CAL_NEXT), m, (int)(date_to_days(n) - date_to_days(d)));
    ui_set_text(d_next, b);
    snprintf(b, sizeof(b), T(S_CAL_WEEK), date_iso_week(d), date_yday(d), date_leap(d.y) ? 366 : 365);
    ui_set_text(d_week, b);
}

static void day_enter(lv_obj_t *root, void *arg)
{
    dp = arg ? arg : &cur;
    d_wday = ui_label(root, &font_s, C_DIM, "");
    lv_obj_align(d_wday, LV_ALIGN_CENTER, 0, -128);
    d_num = ui_label(root, &font_xl, C_TEXT, "");
    lv_obj_align(d_num, LV_ALIGN_CENTER, 0, -50);
    d_month = ui_label(root, &font_m, C_TEXT, "");
    lv_obj_align(d_month, LV_ALIGN_CENTER, 0, 28);
    lv_obj_t *line = lv_obj_create(root);
    lv_obj_remove_style_all(line);
    lv_obj_set_size(line, 170, 2);
    lv_obj_set_style_bg_color(line, C_FAINT, 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    lv_obj_align(line, LV_ALIGN_CENTER, 0, 64);
    d_kind = ui_label(root, &font_s, C_DIM, "");
    lv_obj_align(d_kind, LV_ALIGN_CENTER, 0, 92);
    d_rel = ui_label(root, &font_xs, C_TEXT, "");
    lv_obj_align(d_rel, LV_ALIGN_CENTER, 0, 120);
    d_next = ui_label(root, &font_xs, ui_accent(), "");
    lv_obj_align(d_next, LV_ALIGN_CENTER, 0, 144);
    d_week = ui_label(root, &font_xs, C_DIM, "");
    lv_obj_align(d_week, LV_ALIGN_CENTER, 0, 168);
    day_update();
}

static bool day_event(ev_t e, int x, int y)
{
    if (e == EV_DIAL_CW || e == EV_DIAL_CCW) {
        *dp = date_add_days(*dp, e == EV_DIAL_CW ? 1 : -1);
        day_update();
        return true;
    }
    if (e == EV_TAP) { ui_pop(); return true; }
    return false;
}

const screen_t scr_calendar_day = {"calendar-day", day_enter, NULL, day_event, NULL, SCR_NO_STATUS};
